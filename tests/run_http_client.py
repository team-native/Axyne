"""Exercise the native client against loopback only; no external server needed."""
import http.server
import subprocess
import sys
import threading

errors = []
seen = []
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass
    def do_POST(self):
        seen.append(self.path)
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if (self.path != "/echo?version=1.2.3&os=windows&arch=x64&channel=stable"
            or self.headers.get("Authorization") != "Bearer test-token"
            or self.headers.get("Accept") != "application/json"
            or self.headers.get("Content-Type") != "application/json"
            or body != b'{"hello":true}'):
            errors.append("request method/path/query/headers/body changed")
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"a\0b")
    def do_GET(self):
        seen.append(self.path)
        if self.headers.get("Authorization") is not None:
            errors.append("unexpected authorization header")
        if self.path == "/broken":
            self.connection.shutdown(2)
            self.connection.close()
            return
        self.send_response(204 if self.path == "/empty" else 404)
        self.end_headers()
        if self.path != "/empty":
            self.wfile.write(b"missing")

with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        result = subprocess.run([sys.argv[1], f"http://127.0.0.1:{server.server_port}"], timeout=40)
    finally:
        server.shutdown()
        thread.join()
if errors or set(seen) != {"/echo?version=1.2.3&os=windows&arch=x64&channel=stable", "/missing", "/empty", "/broken"}:
    raise SystemExit(str(errors or seen))
raise SystemExit(result.returncode)
