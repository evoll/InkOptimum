"""
PlatformIO pre-build hook: apply the SdFat patches in scripts/sdfat_patches/ to the SdFat copy this env builds
(.pio/libdeps/<env>/SdFat).

Taken from witchhunt-reader (jpirnay, commit b19162165, "perf(sd): zero a new folder's cluster eight sectors a
write"); the hook is a trimmed, gentler version of theirs:

  0005  FatFile: a new folder's cluster is zeroed eight sectors a write from a zeroed block, not one sector a write
        through the cache.
  0006  ExFatFile: the same for exFAT, whose 128 KB clusters made a new folder 256 single-sector writes (X4 Pro:
        298 and 636 ms for a book's first open's two cache folders).

Safety gates, as upstream's: SdFat must be exactly the release the patches were reviewed against, and each file a
patch touches must hash to that release's bytes (or to the reviewed patched bytes). Everything is checked before
anything is written; an already-patched file is left untouched so incremental builds recompile nothing.

Unlike upstream's, any mismatch (another SdFat version, a file changed by hand, no copy found, git missing) only
prints a warning and leaves SdFat as it is: the firmware then builds exactly as before, just without the speed-up.

Runs as a PlatformIO pre: hook and standalone:
  python scripts/patch_sdfat.py              # every .pio/libdeps/*/SdFat
  python scripts/patch_sdfat.py DIR [DIR..]  # the given SdFat directories
"""

import glob
import hashlib
import os
import subprocess
import sys

try:
    Import("env")  # noqa: F821 -- provided by PlatformIO when run as a build hook
    PIO_ENV = env  # noqa: F821
    PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
except (NameError, Exception):  # not under PlatformIO -> standalone invocation
    PIO_ENV = None
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PATCH_DIR = os.path.join(PROJECT_DIR, "scripts", "sdfat_patches")

# The greiman/SdFat release the patches were reviewed against.
SDFAT_VERSION = "2.3.1"

# patch -> (file it changes, sha256 of SdFat 2.3.1's file, sha256 once patched). Applied in this order.
PATCHES = (
    (
        "0005-zero-new-folder-cluster-fat.patch",
        "src/FatLib/FatFile.cpp",
        "c1757a57ea134df2aab650d2db109b7d731f66cf309e5a070a9b50667c258948",
        "ab8864911b054edd59e8eeb427a33cb4379486b55f169fa9c5b7b6409f498616",
    ),
    (
        "0006-zero-new-folder-cluster-exfat.patch",
        "src/ExFatLib/ExFatFileWrite.cpp",
        "439e1fe566bc04c8e53962eeb8bdd91c89d5358cfff20bcf8be90c742e683ea4",
        "ad7adea85952502927ac62c1674a6e5f499efd9ae2b95000be433f231ceb0a4b",
    ),
)


def _warn(message):
    sys.stderr.write("WARNING: SdFat patches skipped: %s\n" % message)


def _sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def _read_patch(name):
    with open(os.path.join(PATCH_DIR, name), "rb") as f:
        return f.read().replace(b"\r\n", b"\n")


def _git_apply(sdfat_dir, patch_text, *args):
    git_env = dict(os.environ)
    for key in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT"):
        git_env.pop(key, None)
    git_env.update(
        # .pio/libdeps lives inside this project's work tree; without the ceiling git would run as that repo.
        GIT_CEILING_DIRECTORIES=os.path.dirname(os.path.abspath(sdfat_dir)),
        GIT_CONFIG_NOSYSTEM="1",
        GIT_CONFIG_GLOBAL=os.devnull,
    )
    return subprocess.run(
        ["git", "-c", "core.autocrlf=false", "apply"] + list(args) + ["-"],
        cwd=sdfat_dir,
        env=git_env,
        input=patch_text,
        capture_output=True,
    )


def patch_sdfat(sdfat_dir):
    """Bring one SdFat copy to the patched state; on any doubt leave it untouched and say why."""
    properties = os.path.join(sdfat_dir, "library.properties")
    if not os.path.isfile(properties):
        return _warn("%s is not an SdFat library" % sdfat_dir)
    with open(properties, encoding="utf-8", errors="replace") as f:
        if ("version=" + SDFAT_VERSION) not in f.read().splitlines():
            return _warn("%s is not SdFat %s, the release the patches were reviewed against" % (sdfat_dir, SDFAT_VERSION))

    pending = []
    for name, relative, upstream_sha, patched_sha in PATCHES:
        target = os.path.join(sdfat_dir, relative)
        patch_path = os.path.join(PATCH_DIR, name)
        if not os.path.isfile(target) or not os.path.isfile(patch_path):
            return _warn("%s or its patch is missing in %s" % (relative, sdfat_dir))
        current = _sha256(target)
        if current == patched_sha:
            continue
        if current != upstream_sha:
            return _warn("%s in %s is neither SdFat %s's file nor the patched one" % (relative, sdfat_dir, SDFAT_VERSION))
        patch_text = _read_patch(name)
        try:
            result = _git_apply(sdfat_dir, patch_text, "--check")
        except OSError as error:  # git not installed
            return _warn("cannot run git (%s)" % error)
        if result.returncode != 0:
            return _warn("%s does not apply cleanly to %s" % (name, sdfat_dir))
        pending.append((name, target, patched_sha, patch_text))

    for name, target, patched_sha, patch_text in pending:
        result = _git_apply(sdfat_dir, patch_text)
        if result.returncode != 0 or _sha256(target) != patched_sha:
            # Only this patch's file could be touched, and only by a bad apply: say so loudly.
            sys.stderr.write("ERROR: SdFat patch %s left %s in an unexpected state; delete the copy and rebuild\n"
                             % (name, target))
            raise SystemExit(1)
        print("Applied SdFat patch: %s (%s)" % (name, sdfat_dir))


def _pio_targets():
    # `pio run -t clean` runs pre: hooks without installing dependencies first.
    if PIO_ENV.IsCleanTarget():
        return []
    libdeps = os.path.join(PIO_ENV.subst("$PROJECT_LIBDEPS_DIR"), PIO_ENV["PIOENV"])
    if not os.path.isdir(libdeps):
        return []
    copies = sorted(n for n in os.listdir(libdeps)
                    if (n == "SdFat" or n.startswith("SdFat@")) and os.path.isdir(os.path.join(libdeps, n)))
    if len(copies) > 1:
        _warn("%s holds more than one SdFat (%s); which one gets compiled is up to the dependency finder"
              % (libdeps, ", ".join(copies)))
        return []
    return [os.path.join(libdeps, copies[0])] if copies else []


def _standalone_targets(argv):
    if argv:
        return [os.path.abspath(path) for path in argv]
    return sorted(glob.glob(os.path.join(PROJECT_DIR, ".pio", "libdeps", "*", "SdFat")))


def main():
    targets = _pio_targets() if PIO_ENV is not None else _standalone_targets(sys.argv[1:])
    for sdfat_dir in targets:
        patch_sdfat(sdfat_dir)


main()
