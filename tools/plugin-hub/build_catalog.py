#!/usr/bin/env python3
"""Static Plugin Hub: writes catalog pages and plugin copies for hosting on any static server
(nginx, GitHub Pages, S3). No server code needed.

  python3 build_catalog.py --dir ./plugins --out ./public --base-url https://example.github.io/hub --page-size 8

Output:  public/catalog/page-1.json ...  (each holds page_size + 1 rows: the extra row marks "more")
         public/catalog/lists.json       public/plugins/<id>/...
Point device.json at  <base-url>/catalog/page-{page}.json  and set browse.page_size to the same number.
"""
import argparse
import json
import os
import shutil

import hub_core


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default="plugins")
    ap.add_argument("--out", default="public")
    ap.add_argument("--base-url", required=True)
    ap.add_argument("--page-size", type=int, default=8)
    args = ap.parse_args()
    base = args.base_url.rstrip("/")
    size = max(1, min(args.page_size, 16))

    rows = hub_core.load_plugins(args.dir, base + "/plugins")
    cat = os.path.join(args.out, "catalog")
    shutil.rmtree(args.out, ignore_errors=True)
    os.makedirs(cat)
    pages = max(1, -(-len(rows) // size))
    for n in range(1, pages + 1):
        items = [dict(r) for r in hub_core.page(rows, n, size + 1)]
        for it in items:
            it.pop("lists", None)
        with open(os.path.join(cat, f"page-{n}.json"), "w", encoding="utf-8") as f:
            json.dump({"items": items}, f, ensure_ascii=False, separators=(",", ":"))
    for r in rows:
        shutil.copytree(os.path.join(args.dir, r["id"]), os.path.join(args.out, "plugins", r["id"]),
                        ignore=shutil.ignore_patterns("hub.json", ".*"))
    print(f"{len(rows)} plugins, {pages} pages -> {args.out}")


if __name__ == "__main__":
    main()
