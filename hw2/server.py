#!/usr/bin/env python3
"""
Simple HTTP server for board connectivity testing.

By default this binds to 0.0.0.0:8002 so devices on the same network
(including your board) can reach it using this machine's IP address.
"""

from http.server import BaseHTTPRequestHandler, HTTPServer
import socket
from urllib.parse import parse_qs, urlparse


HOST = "0.0.0.0"
PORT = 8002


class BoardVisibleHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        accel_x = query.get("x", ["N/A"])[0]
        accel_y = query.get("y", ["N/A"])[0]
        accel_z = query.get("z", ["N/A"])[0]
        src = query.get("src", ["N/A"])[0]
        event = query.get("event", ["N/A"])[0]

        if parsed.path == "/accel":
            print(
                "Accelerometer from board "
                f"(client {self.client_address[0]}:{self.client_address[1]}): "
                f"x={accel_x}, y={accel_y}, z={accel_z}"
            )
        elif parsed.path == "/motion":
            print(
                "Significant motion event from board "
                f"(client {self.client_address[0]}:{self.client_address[1]}): "
                f"event={event}, x={accel_x}, y={accel_y}, z={accel_z}, src={src}"
            )

        body = (
            "STM32 host test server is running.\n"
            f"Client: {self.client_address[0]}:{self.client_address[1]}\n"
            f"Path: {self.path}\n"
            f"Event: {event}\n"
            f"Accel: x={accel_x} y={accel_y} z={accel_z}\n"
            f"FuncSrc: {src}\n"
        ).encode("utf-8")

        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        # Keep logs concise for embedded test loops.
        print(f"[{self.client_address[0]}] {format % args}")


def get_local_ip():
    """Best-effort local LAN IP discovery."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


def main():
    server = HTTPServer((HOST, PORT), BoardVisibleHandler)
    local_ip = get_local_ip()
    print(f"Server listening on {HOST}:{PORT}")
    print(f"Board URL: http://{local_ip}:{PORT}/")
    print("Press Ctrl+C to stop.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server...")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
