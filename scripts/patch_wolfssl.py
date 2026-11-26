from pathlib import Path

Import("env")


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
MARKER = "/* CrossPoint wolfSSL compatibility overrides */"
OVERRIDES = f"""

{MARKER}
#undef NO_DH
#ifndef HAVE_FFDHE_2048
#define HAVE_FFDHE_2048
#endif
#undef FP_MAX_BITS
#define FP_MAX_BITS 8192
/* Arduino-wolfSSL always defines DEBUG_WOLFSSL, but SecureClient turns the trace on only in
   FREEINK_WOLFSSL_DEBUG builds. Drop it otherwise so the trace code and strings are not linked. */
#ifndef FREEINK_WOLFSSL_DEBUG
#undef DEBUG_WOLFSSL
#endif
"""


def patch_user_settings(path: Path) -> None:
    original_text = path.read_text()
    text = original_text
    if MARKER in text:
        text = text.split(MARKER, 1)[0].rstrip()
    patched_text = text + OVERRIDES + "\n"
    if original_text == patched_text:
        return
    path.write_text(patched_text)
    print(f"Patched wolfSSL settings: {path.relative_to(PROJECT_DIR)}")


for settings in PROJECT_DIR.glob(".pio/libdeps/*/Arduino-wolfSSL/src/user_settings.h"):
    patch_user_settings(settings)


# SecureClient::reserveFullRecord() grows the input buffer once per download
# connection. Stock wolfSSL frees it again after every record, so these patches
# keep a reserved buffer until wolfSSL_free(), and free an empty buffer before
# growing it so a 16KB record does not need both blocks at once (crosspoint #4576).
SHRINK_BEFORE = """    if (!forcedFree && (usedLength > STATIC_BUFFER_LEN ||
            ssl->buffers.clearOutputBuffer.length > 0))
        return;
"""

SHRINK_AFTER = SHRINK_BEFORE + """
#ifdef FREEINK_WOLFSSL_RETAIN_INPUT_BUFFER
    /* Keep only SecureClient's reservation; must equal kFullRecordCapacity. */
    if (!forcedFree && usedLength == 0 &&
            ssl->buffers.inputBuffer.bufferSize == MAX_RECORD_SIZE + 256 + 64) {
        ForceZero(ssl->buffers.inputBuffer.buffer,
            ssl->buffers.inputBuffer.length);
        ssl->buffers.inputBuffer.idx = 0;
        ssl->buffers.inputBuffer.length = 0;
        return;
    }
#endif
"""

GROW_BEFORE = """    if (usedLength < 0 || size < 0) {
        WOLFSSL_MSG("GrowInputBuffer() called with negative number");
        return BAD_FUNC_ARG;
    }
"""

GROW_AFTER = GROW_BEFORE + """
#ifdef FREEINK_WOLFSSL_RELEASE_EMPTY_INPUT_BUFFER
    /* Free a fully read buffer first, so the old and new blocks do not coexist. */
    if (usedLength == 0 && ssl->buffers.inputBuffer.dynamicFlag &&
            ssl->buffers.inputBuffer.idx == ssl->buffers.inputBuffer.length)
        ShrinkInputBuffer(ssl, FORCED_FREE);
#endif
"""

# The retained size above must match the SDK's reservation size.
SDK_CAPACITY = "constexpr int kFullRecordCapacity = (1 << 14) + 256 + 64;"
SDK_CLIENT = PROJECT_DIR / "freeink-sdk/libs/network/SecureNet/src/SecureClient.cpp"


def patch_internal(env, node):
    upstream = Path(node.srcnode().get_abspath()).resolve()
    library = upstream.parents[2]
    if upstream.parent.parent.name != "src" or upstream.parent.name != "src" or library.name != "Arduino-wolfSSL":
        return node
    source = upstream.read_text(encoding="utf-8")
    version = (library / "library.properties").read_text(encoding="utf-8")
    # InkOptimum pins Arduino-wolfSSL 5.8.4 (platformio.ini); the anchors were checked against that source.
    if "\nversion=5.8.4\n" not in version or source.count(SHRINK_BEFORE) != 1 or source.count(GROW_BEFORE) != 1:
        raise RuntimeError("Review the wolfSSL input buffer patch for this wolfSSL version")
    if SDK_CAPACITY not in SDK_CLIENT.read_text(encoding="utf-8"):
        raise RuntimeError("Review the wolfSSL input buffer patch for this SDK's kFullRecordCapacity")

    patched = source.replace(SHRINK_BEFORE, SHRINK_AFTER).replace(GROW_BEFORE, GROW_AFTER)
    target = Path(env.subst("$BUILD_DIR")) / "patched/wolfssl_internal.c"
    if not target.is_file() or target.read_text(encoding="utf-8") != patched:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(patched, encoding="utf-8")
        print(f"Patched wolfSSL input buffer: {target}")
    return env.File(str(target))


env.AddBuildMiddleware(patch_internal, "*internal.c")
