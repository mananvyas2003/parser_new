#!/usr/bin/env python3
"""Localhost demo UI for the circuit-synthesis compiler."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[2]
API = ROOT / "build" / "synth_api.exe"
HTML = Path(__file__).resolve().parent / "index.html"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
_SYNTH_LOCK = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        sys.stdout.write("[%s] %s\n" % (self.log_date_time_string(), fmt % args))
        sys.stdout.flush()

    def _send(self, code: int, ctype: str, body: bytes, extra: list[tuple[str, str]] | None = None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        if extra:
            for k, v in extra:
                self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            data = HTML.read_bytes()
            self._send(200, "text/html; charset=utf-8", data)
        elif path == "/download/schematic":
            p = ROOT / "build" / "demo_output.kicad_sch"
            if not p.exists():
                self._send(404, "text/plain", b"No schematic yet. Run synthesize first.")
                return
            self._send(
                200,
                "application/octet-stream",
                p.read_bytes(),
                [("Content-Disposition", 'attachment; filename="design.kicad_sch"')],
            )
        elif path == "/download/netlist":
            p = ROOT / "build" / "demo_netlist.json"
            if not p.exists():
                self._send(404, "text/plain", b"No netlist yet. Run synthesize first.")
                return
            self._send(
                200,
                "application/json",
                p.read_bytes(),
                [("Content-Disposition", 'attachment; filename="netlist.json"')],
            )
        else:
            self._send(404, "text/plain", b"Not found")

    def do_POST(self):
        path = urlparse(self.path).path
        if path != "/api/synth":
            self._send(404, "text/plain", b"Not found")
            return

        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            payload = json.loads(raw.decode("utf-8") or "{}")
        except json.JSONDecodeError:
            self._send(400, "application/json", b'{"ok":false,"error":"invalid JSON"}')
            return

        prompt = payload.get("prompt") or "design a 12 V to 5 V buck converter at 3 A"
        controller = bool(payload.get("controller"))

        if not API.exists():
            msg = json.dumps({"ok": False, "error": f"missing {API}"}).encode()
            self._send(500, "application/json", msg)
            return

        cmd = [str(API)]
        if controller:
            cmd.append("--controller")
        cmd.append(prompt)

        try:
            with _SYNTH_LOCK:
                proc = subprocess.run(
                    cmd,
                    cwd=str(ROOT),
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
            out = (proc.stdout or "").strip()
            if not out:
                err = (proc.stderr or "no output").strip()
                out = json.dumps({"ok": False, "error": err})
            self._send(200, "application/json; charset=utf-8", out.encode("utf-8"))
            print(f"synth exit={proc.returncode} prompt={prompt!r} controller={controller}")
            sys.stdout.flush()
        except Exception as e:
            msg = json.dumps({"ok": False, "error": str(e)}).encode()
            self._send(500, "application/json", msg)


def main():
    os.chdir(ROOT)
    if not API.exists():
        print(f"ERROR: {API} not found. Build it first.")
        sys.exit(1)

    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    url = f"http://127.0.0.1:{PORT}"
    print("=" * 57)
    print("  Circuit Synthesis Compiler — local demo")
    print("=" * 57)
    print(f"  Open:  {url}")
    print(f"  API:   {API}")
    print("  Ctrl+C to stop")
    print("=" * 57)
    sys.stdout.flush()

    threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
        server.server_close()


if __name__ == "__main__":
    main()
