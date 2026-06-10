#!/usr/bin/env python3

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse
import argparse
import json
import sys
import threading
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime


POSE_NAMES = {
    0: "Freestyle",
    1: "Frog pose",
}


def _parse_int(value):
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


@dataclass
class DashboardState:
    started_at: datetime = field(default_factory=datetime.now)
    request_count: int = 0
    last_client: str = "-"
    last_accel: dict | None = None
    last_prediction: dict | None = None
    events: deque = field(default_factory=lambda: deque(maxlen=12))


class Dashboard:
    def __init__(self, history_size=12, use_tui=True):
        self.state = DashboardState(events=deque(maxlen=history_size))
        self.use_tui = use_tui
        self.lock = threading.Lock()
        self.stop_event = threading.Event()

    def record(self, received):
        query = received["query"]
        path = received["path"]
        event = {
            "time": received["time"],
            "client": received["client"],
            "path": path,
            "query": query,
        }

        with self.lock:
            self.state.request_count += 1
            self.state.last_client = received["client"]

            if path == "/accel":
                self.state.last_accel = {
                    "time": received["time"],
                    "x": _parse_int(query.get("x")),
                    "y": _parse_int(query.get("y")),
                    "z": _parse_int(query.get("z")),
                }
            elif path == "/prediction":
                pose_class = _parse_int(query.get("class"))
                p1_milli = _parse_int(query.get("p1_milli"))
                self.state.last_prediction = {
                    "time": received["time"],
                    "class": pose_class,
                    "pose": POSE_NAMES.get(pose_class, f"Unknown ({pose_class})"),
                    "p1_milli": p1_milli,
                    "frog_confidence": None
                    if p1_milli is None
                    else max(0.0, min(1.0, p1_milli / 1000.0)),
                }

            self.state.events.appendleft(event)

        if not self.use_tui:
            print(json.dumps(received, indent=2), flush=True)

    def snapshot(self):
        with self.lock:
            return DashboardState(
                started_at=self.state.started_at,
                request_count=self.state.request_count,
                last_client=self.state.last_client,
                last_accel=dict(self.state.last_accel)
                if self.state.last_accel is not None
                else None,
                last_prediction=dict(self.state.last_prediction)
                if self.state.last_prediction is not None
                else None,
                events=deque(self.state.events, maxlen=self.state.events.maxlen),
            )

    def run(self, server):
        if not self.use_tui:
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                print("\nShutting down.", flush=True)
            return

        try:
            TextualDashboardApp = build_textual_app()
        except ModuleNotFoundError as exc:
            if exc.name != "textual":
                raise
            print(
                "Textual is required for the TUI. Install it with: "
                "python3 -m pip install textual",
                file=sys.stderr,
            )
            return

        app = TextualDashboardApp(self, server.server_address)
        app.run()


def build_textual_app():
    from textual.app import App, ComposeResult
    from textual.containers import Container, Horizontal
    from textual.widgets import Footer, Header, Static

    class TextualDashboardApp(App):
        CSS = """
        Screen {
            background: #101418;
        }

        Header {
            background: #19324a;
            color: white;
        }

        #main {
            height: 1fr;
            padding: 1 2;
        }

        #status {
            height: 3;
            margin-bottom: 1;
            padding: 0 1;
            border: solid #4d6b7c;
            background: #172029;
        }

        #sections {
            height: 12;
            margin-bottom: 1;
        }

        .panel {
            height: 100%;
            padding: 1 2;
            border: round #4d6b7c;
            background: #172029;
        }

        #prediction {
            width: 1fr;
            margin-right: 1;
        }

        #accelerometer {
            width: 1fr;
        }

        #events {
            height: 1fr;
            padding: 1 2;
            border: round #4d6b7c;
            background: #172029;
        }
        """

        BINDINGS = [
            ("q", "quit", "Quit"),
            ("ctrl+c", "quit", "Quit"),
        ]

        def __init__(self, dashboard, server_address):
            super().__init__()
            self.dashboard = dashboard
            self.server_address = server_address
            self.title = "Swimming Pose Server"
            self.sub_title = "Live prediction and accelerometer dashboard"

        def compose(self) -> ComposeResult:
            yield Header(show_clock=True)
            with Container(id="main"):
                yield Static(id="status")
                with Horizontal(id="sections"):
                    yield Static(id="prediction", classes="panel")
                    yield Static(id="accelerometer", classes="panel")
                yield Static(id="events")
            yield Footer()

        def on_mount(self) -> None:
            self.refresh_dashboard()
            self.set_interval(0.25, self.refresh_dashboard)

        def action_quit(self) -> None:
            self.dashboard.stop_event.set()
            self.exit()

        def refresh_dashboard(self) -> None:
            state = self.dashboard.snapshot()
            host, port = self.server_address

            self.query_one("#status", Static).update(
                "[b]Swimming Pose Server[/b]\n"
                f"Listening on [cyan]http://{host}:{port}[/cyan]    "
                f"Requests: [b]{state.request_count}[/b]    "
                f"Last client: [b]{state.last_client}[/b]    "
                f"Started: {state.started_at.isoformat(timespec='seconds')}"
            )
            self.query_one("#prediction", Static).update(
                self.format_prediction(state.last_prediction)
            )
            self.query_one("#accelerometer", Static).update(
                self.format_accelerometer(state.last_accel)
            )
            self.query_one("#events", Static).update(self.format_events(state.events))

        @staticmethod
        def format_prediction(prediction):
            if prediction is None:
                return "[b]Pose Prediction[/b]\n\n[dim]Waiting for /prediction data...[/dim]"

            confidence = prediction["frog_confidence"]
            confidence_text = "-"
            if confidence is not None:
                confidence_text = f"{confidence * 100:5.1f}% frog pose"

            pose_style = "green" if prediction["class"] == 0 else "yellow"
            return (
                "[b]Pose Prediction[/b]\n\n"
                f"Pose: [{pose_style}][b]{prediction['pose']}[/b][/{pose_style}]\n"
                f"Class: [b]{prediction['class']}[/b]\n"
                f"Confidence: [b]{confidence_text}[/b]\n"
                f"Updated: {prediction['time']}"
            )

        @staticmethod
        def format_accelerometer(accel):
            if accel is None:
                return "[b]Accelerometer[/b]\n\n[dim]Waiting for /accel data...[/dim]"

            return (
                "[b]Accelerometer[/b]\n\n"
                f"x: [b]{accel['x']}[/b]\n"
                f"y: [b]{accel['y']}[/b]\n"
                f"z: [b]{accel['z']}[/b]\n"
                f"Updated: {accel['time']}"
            )

        @staticmethod
        def format_events(events):
            lines = ["[b]Recent Requests[/b]"]
            if not events:
                lines.append("")
                lines.append("[dim]No requests received yet.[/dim]")
                return "\n".join(lines)

            for event in events:
                query = " ".join(
                    f"{key}={value}" for key, value in event["query"].items()
                )
                lines.append(
                    f"{event['time']}  [cyan]{event['client']}[/cyan]  "
                    f"[b]{event['path']}[/b]  {query}"
                )
            return "\n".join(lines)

    return TextualDashboardApp


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

        self.server.dashboard.record(received)

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
    parser.add_argument(
        "--no-tui",
        action="store_true",
        help="disable the terminal dashboard and print received JSON instead",
    )
    parser.add_argument(
        "--history",
        type=int,
        default=12,
        help="number of recent requests shown in the dashboard",
    )
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), BoardRequestHandler)
    use_tui = (not args.no_tui) and sys.stdout.isatty()
    server.dashboard = Dashboard(history_size=args.history, use_tui=use_tui)
    if not use_tui:
        print(f"Listening on http://{args.host}:{args.port}", flush=True)

    server_thread = None
    try:
        if use_tui:
            server_thread = threading.Thread(target=server.serve_forever, daemon=True)
            server_thread.start()
        server.dashboard.run(server)
    finally:
        server.dashboard.stop_event.set()
        server.shutdown()
        server.server_close()
        if server_thread is not None:
            server_thread.join(timeout=1)


if __name__ == "__main__":
    main()
