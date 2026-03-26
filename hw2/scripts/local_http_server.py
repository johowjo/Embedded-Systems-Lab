#!/usr/bin/env python3
"""
HTTP server for STM32 http-sender: prints accelerometer x/y/z from each POST (terminal only).
"""

import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

HOST = "0.0.0.0"
PORT = 8080
PATH = "/sensor_data"


def extract_accelerometer(payload: dict):
    items = payload.get("iot2tangle")
    if not isinstance(items, list):
        return None
    for block in items:
        if not isinstance(block, dict):
            continue
        if block.get("sensor") != "Accelerometer":
            continue
        data = block.get("data")
        if not data or not isinstance(data, list):
            return None
        row = data[0]
        if not isinstance(row, dict):
            return None
        try:
            x = float(row.get("x", 0))
            y = float(row.get("y", 0))
            z = float(row.get("z", 0))
        except (TypeError, ValueError):
            return None
        return (x, y, z)
    return None


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def do_POST(self):
        if self.path != PATH:
            self.send_error(404, "Not Found")
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""
        try:
            data = json.loads(body.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.send_response(400)
            self.end_headers()
            return
        acc = extract_accelerometer(data)
        if acc is not None:
            x, y, z = acc
            print(f"Accelerometer  x={x:10.2f}  y={y:10.2f}  z={z:10.2f}", flush=True)
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"ok\n")

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(
            b"POST JSON to " + PATH.encode() + b" (accelerometer values printed here)\n"
        )


def main():
    global HOST, PORT, PATH
    if len(sys.argv) >= 2:
        PORT = int(sys.argv[1])
    if len(sys.argv) >= 3:
        PATH = sys.argv[2] if sys.argv[2].startswith("/") else "/" + sys.argv[2]

    server = HTTPServer((HOST, PORT), Handler)
    print(f"Listening on http://{HOST}:{PORT}{PATH} — accelerometer x/y/z in this terminal")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
        server.server_close()


if __name__ == "__main__":
    main()
