#!/usr/bin/env python3

import os
import sys
import time
import argparse
import re
from urllib.parse import urlparse, parse_qs
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DEFAULT_PORT = 5000


class FirmwareHandler(BaseHTTPRequestHandler):
    firmware_path = None
    version_path = None

    @classmethod
    def _read_server_version(cls):
        """Читает firmware.version (одно число). Возвращает int или 0 если нет."""
        if cls.version_path and os.path.exists(cls.version_path):
            try:
                with open(cls.version_path, "r") as f:
                    return int(f.read().strip() or "0")
            except (OSError, ValueError):
                return 0
        return 0

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)
        current = int((query.get("current", ["0"])[0] or "0"))
        chipid = query.get("chipid", ["?"])[0]

        if path != "/getfirmware":
            self._send(404, b"")
            self._log(f"404 (bad path: {path})")
            return

        if not os.path.exists(self.firmware_path):
            self._send(404, b"")
            self._log(f"<- {chipid} current=v{current}: 404 (no firmware.bin)")
            return

        server_version = self._read_server_version()
        if server_version <= current:
            self._send(404, b"")
            self._log(f"<- {chipid} current=v{current} vs server=v{server_version}: 404 (up-to-date)")
            return

        size = os.path.getsize(self.firmware_path)

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.send_header("Connection", "close")
        self.end_headers()

        self._log(f"<- {chipid} current=v{current} vs server=v{server_version}: 200 ({size} bytes)")

        chunk_size = max(4096, size // 10)
        sent = 0
        next_pct = 10  # следующий десяток, который ещё не залогирован
        try:
            with open(self.firmware_path, "rb") as f:
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    self.wfile.flush()
                    sent += len(chunk)
                
                    while next_pct <= 100 and sent * 100 >= next_pct * size:
                        self._log(f"OTA PROGRESS <- {chipid}: {next_pct}%")
                        next_pct += 10
                    time.sleep(0.2)  # ~200мс на весь файл — даём ESP шанс писать
        except (BrokenPipeError, ConnectionResetError):
            self._log(f"client {chipid} disconnected early ({sent}/{size})")

    def do_POST(self):

        path = self.path.split("?", 1)[0]
        length = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(length).decode("utf-8", errors="replace") if length else ""

        if path == "/ack":
            m = re.search(r'"chipid"\s*:\s*"([^"]+)"', body)
            chipid = m.group(1) if m else "?"
            m2 = re.search(r'"status"\s*:\s*"([^"]+)"', body)
            status = m2.group(1) if m2 else "?"
            m3 = re.search(r'"error"\s*:\s*"([^"]+)"', body)
            err = f" ({m3.group(1)})" if m3 else ""
            self._send(200, b'{"ok":true}')
            self._log(f"OTA ACK <- {chipid}: {status}{err}")
            return

        self._send(404, b"")
        self._log(f"404 (bad POST path: {path})")

    def _send(self, code, body):
        self.send_response(code)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _log(self, msg):

        print(f"[{self.log_date_time_string()}] {self.client_address[0]:>15}  {msg}",
              flush=True)

    def log_message(self, fmt, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description="Test firmware server for WebLed2.0")
    parser.add_argument("port", nargs="?", type=int, default=DEFAULT_PORT,
                        help=f"порт (по умолчанию {DEFAULT_PORT})")
    parser.add_argument("firmware", nargs="?", default="firmware.bin",
                        help="путь к .bin (по умолчанию ./firmware.bin)")
    parser.add_argument("version", nargs="?", default="firmware.version",
                        help="путь к .version (по умолчанию ./firmware.version)")
    args = parser.parse_args()

    def _resolve(p):
        if os.path.isabs(p):
            return os.path.abspath(p)
        return os.path.join(os.path.dirname(os.path.abspath(__file__)), p)

    firmware_path = _resolve(args.firmware)
    version_path = _resolve(args.version)
    port = args.port

    FirmwareHandler.firmware_path = firmware_path
    FirmwareHandler.version_path = version_path

    print("=" * 60)
    print(" WebLed2.0 — test firmware server")
    print("=" * 60)
    print(f"  Listen:    http://0.0.0.0:{port}/getfirmware")
    print(f"  Firmware:  {firmware_path}")
    print(f"  Version:   {version_path}")
    print()
    if os.path.exists(firmware_path):
        print(f"  [OK]  firmware.bin: {os.path.getsize(firmware_path)} bytes")
    else:
        print(f"  [--]  firmware.bin not found")
    if os.path.exists(version_path):
        try:
            with open(version_path, "r") as f:
                v = f.read().strip()
            print(f"  [OK]  firmware.version: {v}")
        except OSError:
            print(f"  [--]  firmware.version unreadable")
    else:
        print(f"  [--]  firmware.version not found, server version = 0 (any ESP gets update)")
    print("=" * 60)
    print()

    server = ThreadingHTTPServer(("0.0.0.0", port), FirmwareHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down...")
        server.shutdown()


if __name__ == "__main__":
    main()
