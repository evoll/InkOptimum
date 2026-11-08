#!/usr/bin/env python3
"""Plugin Hub: a small stdlib-only HTTP(S) server that hosts inkMOD/CrossPoint SD plugins.

  python3 server.py --dir ./plugins --port 8443 --cert fullchain.pem --key privkey.pem
  HUB_TOKEN=secret python3 server.py --dir ./plugins --public-url https://hub.example.com

Endpoints
  GET /catalog.json?page=N&limit=M[&list=NAME][&q=TEXT]   rows for the on-device catalog
  GET /lists.json                                          list index (browse.lists_url)
  GET /plugins/<id>/<file>                                 plugin files (what a bundle downloads)
  GET /health
With HUB_TOKEN set every endpoint except /health needs "Authorization: Bearer <token>".
"""
import argparse
import hmac
import json
import mimetypes
import os
import ssl
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

import hub_core


class Hub(BaseHTTPRequestHandler):
    server_version = "PluginHub/1"
    plugins_dir = "plugins"
    public_url = ""
    token = ""

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    # -- helpers
    def _send(self, code, body, ctype="application/json", extra=None):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj, ensure_ascii=False, separators=(",", ":")))

    def _authorized(self):
        if not self.token:
            return True
        sent = self.headers.get("Authorization", "")
        return hmac.compare_digest(sent, "Bearer " + self.token)

    def _base_url(self):
        if self.public_url:
            return self.public_url.rstrip("/")
        host = self.headers.get("Host", "localhost")
        scheme = "https" if isinstance(self.connection, ssl.SSLSocket) else "http"
        return f"{scheme}://{host}"

    # -- routes
    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/health":
            return self._json({"ok": True})
        if not self._authorized():
            return self._send(401, '{"error":"unauthorized"}', extra={"WWW-Authenticate": "Bearer"})
        if url.path == "/catalog.json":
            return self._catalog(parse_qs(url.query))
        if url.path == "/lists.json":
            rows = hub_core.load_plugins(self.plugins_dir, self._base_url() + "/plugins")
            lists = hub_core.lists_of(rows)
            for entry in lists:
                if entry["url"].startswith("?"):
                    entry["url"] = f"{self._base_url()}/catalog.json?page={{page}}&limit={{limit}}&list=" + entry["url"][6:]
                else:
                    entry["url"] = f"{self._base_url()}/catalog.json?page={{page}}&limit={{limit}}"
            return self._json({"lists": lists})
        if url.path.startswith("/plugins/"):
            return self._file(unquote(url.path[len("/plugins/"):]))
        self._send(404, '{"error":"not found"}')

    def _catalog(self, q):
        def one(name, default=""):
            return q.get(name, [default])[0]
        try:
            rows = hub_core.load_plugins(self.plugins_dir, self._base_url() + "/plugins")
            items = hub_core.page(rows, one("page", "1"), one("limit", "9"), one("list"), one("q"))
        except ValueError:
            return self._send(400, '{"error":"bad paging"}')
        for it in items:
            it.pop("lists", None)
        self._json({"items": items})

    def _file(self, rel):
        parts = rel.split("/")
        if len(parts) < 2 or any(p in ("", ".", "..") or p.startswith(".") or "\\" in p for p in parts):
            return self._send(404, '{"error":"not found"}')
        full = os.path.realpath(os.path.join(self.plugins_dir, *parts))
        root = os.path.realpath(self.plugins_dir)
        if not full.startswith(root + os.sep) or not os.path.isfile(full) or parts[-1] == "hub.json":
            return self._send(404, '{"error":"not found"}')
        ctype = mimetypes.guess_type(full)[0] or "application/octet-stream"
        with open(full, "rb") as f:
            self._send(200, f.read(), ctype)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default="plugins", help="folder with one sub-folder per plugin")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--cert", help="TLS certificate chain (PEM); with --key enables HTTPS")
    ap.add_argument("--key", help="TLS private key (PEM)")
    ap.add_argument("--public-url", default="", help="externally visible base URL, e.g. https://hub.example.com")
    args = ap.parse_args()

    Hub.plugins_dir = args.dir
    Hub.public_url = args.public_url
    Hub.token = os.environ.get("HUB_TOKEN", "")
    httpd = ThreadingHTTPServer((args.host, args.port), Hub)
    if args.cert and args.key:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.load_cert_chain(args.cert, args.key)
        httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
    print(f"Plugin Hub on {'https' if args.cert else 'http'}://{args.host}:{args.port}  dir={args.dir}  auth={'on' if Hub.token else 'off'}")
    httpd.serve_forever()


if __name__ == "__main__":
    main()
