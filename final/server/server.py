#!/usr/bin/env python3

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse
import argparse
import json
from datetime import datetime


class BoardRequestHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        parsed = urlparse(self.path)
        query = {
            key: values[0] if len(values) == 1 else values
            for key, values in parse_qs(parsed.query).items()
        }
        received = {
            "time": datetime.now().isoformat(timespec="seconds"),
            "client": self.client_address[0],
            "path": parsed.path,
            "query": query,
        }

        print(json.dumps(received, indent=2), flush=True)

        body = b"OK\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        return


def main():
    parser = argparse.ArgumentParser(description="HTTP receiver for STM32 board data")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8002)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), BoardRequestHandler)
    print(f"Listening on http://{args.host}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.", flush=True)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
