"""Plugin Hub core: scans a plugins/ directory and answers catalog queries. No network code here."""
import json
import os

HIDDEN_PREFIXES = (".",)
MAX_FILES = 64
MAX_FILE_BYTES = 2 * 1024 * 1024


def _read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def _plugin_files(root):
    """Relative file names (forward slashes) that belong to the plugin, sorted. Hub-only files are skipped."""
    out = []
    for base, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if not d.startswith(HIDDEN_PREFIXES))
        for name in sorted(files):
            if name.startswith(HIDDEN_PREFIXES) or name == "hub.json":
                continue
            full = os.path.join(base, name)
            if os.path.getsize(full) > MAX_FILE_BYTES:
                continue
            out.append(os.path.relpath(full, root).replace(os.sep, "/"))
    return out[:MAX_FILES]


def load_plugins(plugins_dir, base_url):
    """Catalog rows for every plugin folder. `base_url` ends without a slash, e.g. https://host/plugins."""
    rows = []
    if not os.path.isdir(plugins_dir):
        return rows
    for pid in sorted(os.listdir(plugins_dir)):
        root = os.path.join(plugins_dir, pid)
        if pid.startswith(".") or not os.path.isdir(root):
            continue
        manifest = _read_json(os.path.join(root, "manifest.json"))
        hub = _read_json(os.path.join(root, "hub.json"))  # optional extra catalog metadata
        files = _plugin_files(root)
        if "manifest.json" not in files:
            continue  # a bundle must carry its manifest: the device reads the installed version from it
        rows.append({
            "id": pid,
            "title": manifest.get("title") or pid,
            "author": manifest.get("author", ""),
            "version": manifest.get("version", "0.0.0"),
            "description": manifest.get("description", ""),
            "base": f"{base_url}/{pid}/",
            "files": files,
            "families": hub.get("families", "inkmod,crosspoint"),
            "api_min": hub.get("api_min", 1),
            "requires": hub.get("requires", ""),
            "lists": hub.get("lists", ["all"]),
        })
    return rows


def lists_of(rows):
    names = sorted({name for row in rows for name in row["lists"]})
    out = [{"title": "All plugins", "url": "", "body": ""}]  # url "" = the device's browse URL
    for name in names:
        if name != "all":
            out.append({"title": name, "url": f"?list={name}"})
    return out


def page(rows, page_no, limit, list_name="", query=""):
    """Rows for one device page. The device sends limit = page_size + 1; page_size rows are shown and the
    extra one only proves another page exists. So the offset advances by limit - 1."""
    if list_name and list_name != "all":
        rows = [r for r in rows if list_name in r["lists"]]
    if query:
        q = query.lower()
        rows = [r for r in rows if q in r["title"].lower() or q in r["description"].lower()]
    limit = max(2, min(int(limit), 17))
    page_no = max(1, int(page_no))
    size = limit - 1
    start = (page_no - 1) * size
    return rows[start:start + limit]
