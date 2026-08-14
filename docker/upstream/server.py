import os
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Lock

BACKEND_ID = os.environ.get("BACKEND_ID", "backend")
MAX_DELAY_MS = 10000
count = 0
count_lock = Lock()


def next_count():
    global count
    with count_lock:
        count += 1
        return count


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        request_count = next_count()
        path = self.path.split("?", 1)[0]
        delay_ms = 0
        if path.startswith("/slow/"):
            delay_ms = parse_delay(path.removeprefix("/slow/"))
        elif path.startswith("/counted-slow/"):
            parts = path.removeprefix("/counted-slow/").split("/", 1)
            delay_ms = parse_delay(parts[0])
        if delay_ms:
            time.sleep(delay_ms / 1000)
        if path == "/identity":
            body = f"{BACKEND_ID}\n"
            self.respond(200, body)
        elif path.startswith("/status/"):
            self.respond(parse_status(path.removeprefix("/status/")), f"{BACKEND_ID}:{request_count}:{path}\n")
        else:
            self.respond(200, f"{BACKEND_ID}:{request_count}:{path}\n")
        print(f"{BACKEND_ID} GET {path} count={request_count}", flush=True)

    def respond(self, status, body):
        encoded = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def log_message(self, *_):
        pass


def parse_delay(value):
    try:
        return min(max(int(value), 0), MAX_DELAY_MS)
    except ValueError:
        return 0


def parse_status(value):
    try:
        status = int(value)
        return status if 100 <= status <= 599 else 400
    except ValueError:
        return 400


ThreadingHTTPServer(("0.0.0.0", 9000), Handler).serve_forever()
