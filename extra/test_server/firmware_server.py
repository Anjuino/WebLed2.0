#!/usr/bin/env python3

import os
import sys
import ssl
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
    parser.add_argument("--cert", default="certs/server.crt",
                        help="сертификат сервера (по умолчанию ./certs/server.crt, см. generate_certs.sh)")
    parser.add_argument("--key", default="certs/server.key",
                        help="приватный ключ сервера (по умолчанию ./certs/server.key)")
    parser.add_argument("--no-tls", action="store_true",
                        help="запустить по обычному HTTP, без TLS (только для отладки)")
    args = parser.parse_args()

    def _resolve(p):
        if os.path.isabs(p):
            return os.path.abspath(p)
        return os.path.join(os.path.dirname(os.path.abspath(__file__)), p)

    firmware_path = _resolve(args.firmware)
    version_path = _resolve(args.version)
    cert_path = _resolve(args.cert)
    key_path = _resolve(args.key)
    port = args.port
    use_tls = not args.no_tls

    FirmwareHandler.firmware_path = firmware_path
    FirmwareHandler.version_path = version_path

    scheme = "https" if use_tls else "http"

    print("=" * 60)
    print(" WebLed2.0 — test firmware server")
    print("=" * 60)
    print(f"  Listen:    {scheme}://0.0.0.0:{port}/getfirmware")
    print(f"  Firmware:  {firmware_path}")
    print(f"  Version:   {version_path}")
    if use_tls:
        print(f"  Cert:      {cert_path}")
        print(f"  Key:       {key_path}")
    else:
        print("  TLS:       ВЫКЛЮЧЕН (--no-tls) — трафик не шифруется")
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

    if use_tls:
        if not os.path.exists(cert_path) or not os.path.exists(key_path):
            print()
            print(f"  [!!]  Не найден сертификат/ключ ({cert_path} / {key_path}).")
            print(f"        Сгенерируйте их: ./generate_certs.sh <IP-сервера>")
            print(f"        Либо запустите с --no-tls для обычного HTTP.")
            sys.exit(1)
        print(f"  [OK]  сертификат и ключ найдены")
    print("=" * 60)
    print()

    server = ThreadingHTTPServer(("0.0.0.0", port), FirmwareHandler)

    if use_tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(certfile=cert_path, keyfile=key_path)
        server.socket = ctx.wrap_socket(server.socket, server_side=True)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down...")
        server.shutdown()


if __name__ == "__main__":
    main()
