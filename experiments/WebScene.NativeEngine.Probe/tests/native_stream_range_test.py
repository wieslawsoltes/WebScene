#!/usr/bin/env python3
"""Exercise native streaming of a large sparse MP4 locally and over HTTP ranges."""
import http.server
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import threading
import time

binary, fixture = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="webscene-stream-") as directory:
    path = pathlib.Path(directory) / "large.mp4"
    data = pathlib.Path(fixture).read_bytes()
    size = 512 * 1024 * 1024
    with path.open("wb") as stream:
        stream.write(data)
        stream.write(struct.pack(">I4s", size - len(data), b"free"))
        stream.truncate(size)
    subprocess.run([binary, str(path)], check=True, timeout=45)

    class Handler(http.server.BaseHTTPRequestHandler):
        transferred = 0
        ranges = 0
        lock = threading.Lock()

        def log_message(self, *args):
            pass

        def do_HEAD(self):
            self.serve(False)

        def do_GET(self):
            self.serve(True)

        def serve(self, body):
            value = self.headers.get("Range")
            match = re.fullmatch(r"bytes=(\d+)-(\d*)", value or "")
            if value and not match:
                self.send_error(416)
                return
            start = int(match[1]) if match else 0
            end = min(int(match[2]), size - 1) if match and match[2] else size - 1
            if start > end:
                self.send_error(416)
                return
            self.send_response(206 if match else 200)
            self.send_header("Content-Type", "video/mp4")
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Length", str(end - start + 1))
            if match:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
                with self.lock:
                    Handler.ranges += 1
            self.end_headers()
            if not body:
                return
            try:
                with path.open("rb") as stream:
                    stream.seek(start)
                    remaining = end - start + 1
                    while remaining:
                        block = stream.read(min(65536, remaining))
                        if not block:
                            break
                        self.wfile.write(block)
                        remaining -= len(block)
                        with self.lock:
                            Handler.transferred += len(block)
                        # Bound this loopback connection to roughly 8 MiB/s.
                        # A whole-file loader cannot finish inside the timeout.
                        time.sleep(len(block) / (8 * 1024 * 1024))
            except (BrokenPipeError, ConnectionResetError):
                pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        subprocess.run([binary, f"http://127.0.0.1:{server.server_port}/large.mp4"],
                       check=True, timeout=45)
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    assert Handler.ranges > 0, "Native player did not request byte ranges"
    assert Handler.transferred < size // 2, "Player downloaded most of the sparse source"
    print(f"512 MiB local/HTTP streaming passed; {Handler.transferred} HTTP bytes sent")
