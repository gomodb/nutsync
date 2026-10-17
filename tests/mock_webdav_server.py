#!/usr/bin/env python3
"""Small WebDAV test server used by NutSync's integration tests.

It intentionally implements only the subset NutSync uses: PROPFIND,
GET, PUT, MKCOL, DELETE and Basic auth. It is not a production server.
"""

from __future__ import annotations

import argparse
import base64
import email.utils
import hashlib
import os
import shutil
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(".")
MOUNT = "/dav"
AUTH = None
VERBOSE = False


def local_path(url_path: str) -> Path | None:
    decoded = urllib.parse.unquote(url_path)
    if not decoded.startswith(MOUNT):
        return None
    relative = decoded[len(MOUNT):].lstrip("/")
    parts = [part for part in relative.split("/") if part]
    if any(part in (".", "..") for part in parts):
        return None
    return ROOT.joinpath(*parts)


def href_for(path: Path) -> str:
    relative = "" if path == ROOT else path.relative_to(ROOT).as_posix()
    href = MOUNT + "/"
    if relative:
        href += urllib.parse.quote(relative, safe="/")
    if path.is_dir() and not href.endswith("/"):
        href += "/"
    return href


def etag_for(path: Path) -> str:
    stat = path.stat()
    digest = hashlib.md5()
    if path.is_file():
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    else:
        digest.update(str(stat.st_mtime_ns).encode())
        try:
            for child in sorted(path.iterdir()):
                digest.update(child.name.encode())
        except OSError:
            pass
    digest.update(str(stat.st_mtime_ns).encode())
    return '"' + digest.hexdigest() + '"'


def propfind_xml(paths: list[Path]) -> bytes:
    responses = []
    for path in paths:
        stat = path.stat()
        is_dir = path.is_dir()
        resource_type = "<d:collection/>" if is_dir else ""
        length = "" if is_dir else f"<d:getcontentlength>{stat.st_size}</d:getcontentlength>"
        display = escape(path.name or "dav")
        responses.append(
            "<d:response>"
            f"<d:href>{escape(href_for(path))}</d:href>"
            "<d:propstat><d:prop>"
            f"<d:displayname>{display}</d:displayname>"
            f"<d:resourcetype>{resource_type}</d:resourcetype>"
            f"{length}"
            f"<d:getlastmodified>{email.utils.formatdate(stat.st_mtime, usegmt=True)}</d:getlastmodified>"
            f"<d:getetag>{escape(etag_for(path))}</d:getetag>"
            "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>"
            "</d:response>"
        )
    return (
        '<?xml version="1.0" encoding="utf-8"?>'
        '<d:multistatus xmlns:d="DAV:">'
        + "".join(responses)
        + "</d:multistatus>"
    ).encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args) -> None:
        if VERBOSE:
            super().log_message(fmt, *args)

    def _authorized(self) -> bool:
        if AUTH is None:
            return True
        header = self.headers.get("Authorization", "")
        if not header.startswith("Basic "):
            return False
        try:
            decoded = base64.b64decode(header[6:]).decode("utf-8")
        except Exception:
            return False
        user, _, password = decoded.partition(":")
        return (user, password) == AUTH

    def _require_auth(self) -> bool:
        if self._authorized():
            return True
        self.close_connection = True
        self.send_response(401)
        self.send_header("WWW-Authenticate", 'Basic realm="NutSync Test"')
        self.send_header("Content-Length", "0")
        self.end_headers()
        return False

    def _target(self) -> Path | None:
        return local_path(self.path.split("?", 1)[0])

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length", "0") or "0")
        return self.rfile.read(length) if length else b""

    def _send_bytes(self, status: int, body: bytes, content_type: str,
                    headers: dict[str, str] | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_PROPFIND(self) -> None:
        self._read_body()
        if not self._require_auth():
            return
        target = self._target()
        if target is None:
            self._send_bytes(403, b"", "text/plain")
            return
        if not target.exists():
            self._send_bytes(404, b"directory not found", "text/plain")
            return
        depth = self.headers.get("Depth", "1")
        paths = [target]
        if depth != "0" and target.is_dir():
            try:
                paths.extend(sorted(target.iterdir(), key=lambda item: item.name))
            except OSError:
                pass
        body = propfind_xml(paths)
        self._send_bytes(207, body, "application/xml; charset=utf-8",
                         {"DAV": "1, 2"})

    def do_GET(self) -> None:
        if not self._require_auth():
            return
        target = self._target()
        if target is None or not target.is_file():
            self._send_bytes(404, b"file not found", "text/plain")
            return
        data = target.read_bytes()
        stat = target.stat()
        self._send_bytes(200, data, "application/octet-stream", {
            "ETag": etag_for(target),
            "Last-Modified": email.utils.formatdate(stat.st_mtime, usegmt=True),
        })

    def do_HEAD(self) -> None:
        self.do_GET()

    def do_PUT(self) -> None:
        if not self._require_auth():
            return
        data = self._read_body()
        target = self._target()
        if target is None:
            self._send_bytes(403, b"", "text/plain")
            return
        exists = target.exists()
        if not target.parent.is_dir():
            self._send_bytes(409, b"parent missing", "text/plain")
            return
        if self.headers.get("If-None-Match") == "*" and exists:
            self._send_bytes(412, b"precondition failed", "text/plain")
            return
        if_match = self.headers.get("If-Match")
        if if_match and (not exists or etag_for(target) != if_match):
            self._send_bytes(412, b"precondition failed", "text/plain")
            return
        temp = target.with_name(target.name + ".nutsync-tmp")
        with temp.open("wb") as handle:
            handle.write(data)
        os.replace(temp, target)
        self._send_bytes(204 if exists else 201, b"", "text/plain",
                         {"ETag": etag_for(target)})

    def do_MKCOL(self) -> None:
        if not self._require_auth():
            return
        target = self._target()
        if target is None:
            self._send_bytes(403, b"", "text/plain")
            return
        if target.exists():
            self._send_bytes(405, b"already exists", "text/plain")
            return
        if not target.parent.is_dir():
            self._send_bytes(409, b"parent missing", "text/plain")
            return
        target.mkdir()
        self._send_bytes(201, b"", "text/plain")

    def do_DELETE(self) -> None:
        if not self._require_auth():
            return
        target = self._target()
        if target is None:
            self._send_bytes(403, b"", "text/plain")
            return
        if not target.exists():
            self._send_bytes(404, b"not found", "text/plain")
            return
        if_match = self.headers.get("If-Match")
        if if_match and etag_for(target) != if_match:
            self._send_bytes(412, b"precondition failed", "text/plain")
            return
        if target.is_dir():
            shutil.rmtree(target)
        else:
            target.unlink()
        self._send_bytes(204, b"", "text/plain")

    def do_OPTIONS(self) -> None:
        self.send_response(200)
        self.send_header("DAV", "1, 2")
        self.send_header("Allow", "OPTIONS, PROPFIND, GET, PUT, MKCOL, DELETE")
        self.send_header("Content-Length", "0")
        self.end_headers()


def main() -> int:
    global ROOT, AUTH, VERBOSE
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--user")
    parser.add_argument("--password")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    ROOT = Path(args.root).resolve()
    ROOT.mkdir(parents=True, exist_ok=True)
    VERBOSE = args.verbose
    if args.user is not None:
        AUTH = (args.user, args.password or "")

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    port = server.server_address[1]
    print(f"PORT={port}", flush=True)

    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        thread.join()
    except KeyboardInterrupt:
        pass
    finally:
        server.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
