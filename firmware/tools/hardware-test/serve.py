"""Gladiator hardware tester: local static UI and a restricted robot API bridge."""
from __future__ import annotations
import argparse, ipaddress, json, mimetypes, socket, threading, urllib.error, urllib.parse, urllib.request, uuid, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PRIVATE = tuple(ipaddress.ip_network(n) for n in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"))
GETS = {"/api/status", "/api/field", "/api/mast", "/api/mast/setup", "/api/visual", "/api/camera", "/api/robot"}
POSTS = {"/api/mast", "/api/mast/setup", "/api/mast/config", "/api/visual", "/api/camera", "/api/control"}
ASSETS = {"/": "index.html", "/index.html": "index.html", "/app.js": "app.js", "/styles.css": "styles.css"}
def validate_target(value):
    parsed = urllib.parse.urlsplit(value if "://" in value else "http://" + value)
    if parsed.scheme != "http" or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ("", "/"):
        raise ValueError("Use a robot IPv4 address, such as http://192.168.8.1")
    try: address = ipaddress.ip_address(parsed.hostname or "")
    except ValueError: raise ValueError("Use the gateway's numeric IPv4 address")
    if address.version != 4 or not any(address in network for network in PRIVATE):
        raise ValueError("Only private LAN IPv4 addresses are supported")
    port = parsed.port or 80
    if port < 1 or port > 65535: raise ValueError("Invalid port")
    return f"http://{address}" + (f":{port}" if port != 80 else "")

def validate_body(path, body):
    if not isinstance(body, dict): raise ValueError("Expected a JSON object")
    if path == "/api/control" and (body.get("operation") != 0 or body.get("deadman") != 0 or body.get("left") != 0 or body.get("right") != 0):
        raise ValueError("This tester can only DISARM tracks; driving uses the field page")
    if path == "/api/mast/config" and body.get("action") not in ("test", "jog-test", "calibrate", "stop"):
        raise ValueError("Unsupported commissioning action")
    maximum = 2048 if path == "/api/mast/setup" else 768 if path == "/api/mast/config" else 384 if path == "/api/camera" else 512
    data = json.dumps(body, separators=(",", ":"), allow_nan=False).encode()
    if len(data) > maximum: raise ValueError(f"Request exceeds the firmware's {maximum}-byte limit")
    return data

class State:
    def __init__(self):
        self.target = None
        self.generation = None
        self.lock = threading.Lock()
        self.writes = threading.Lock()
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    def connect(self, value):
        target = validate_target(value)
        with self.lock:
            self.target, self.generation = target, uuid.uuid4().hex
            return {"target": target, "generation": self.generation}
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs): return None

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *_): pass
    def reply(self, status, data, content_type="application/json"):
        if not isinstance(data, bytes): data = json.dumps(data, ensure_ascii=True, allow_nan=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' http: data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'")
        self.end_headers()
        try: self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError): pass
    def read_json(self):
        try: size = int(self.headers.get("Content-Length", "0"))
        except ValueError: raise ValueError("Invalid body length")
        if not 0 < size <= 4096: raise ValueError("Expected a JSON body of at most 4096 bytes")
        if "application/json" not in self.headers.get("Content-Type", ""): raise ValueError("Expected application/json")
        return json.loads(self.rfile.read(size), parse_constant=lambda _: (_ for _ in ()).throw(ValueError("Nonfinite value")))
    def origin_ok(self):
        origin = self.headers.get("Origin")
        return not origin or urllib.parse.urlsplit(origin).netloc == self.headers.get("Host")
    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path in ASSETS:
            file = ROOT / ASSETS[path]
            return self.reply(200, file.read_bytes(), mimetypes.guess_type(file.name)[0] or "application/octet-stream")
        if path == "/health": return self.reply(200, {"service": "gladiator-hardware-test", "motionOnStartup": False})
        if path.startswith("/robot/"): return self.proxy(path.removeprefix("/robot"), None)
        self.reply(404, {"error": "Not found"})
    def do_POST(self):
        if not self.origin_ok(): return self.reply(403, {"error": "Cross-origin command rejected"})
        try:
            path = urllib.parse.urlsplit(self.path).path
            body = self.read_json()
            if path == "/connection":
                if not isinstance(body, dict): raise ValueError("Expected a JSON object")
                return self.reply(200, self.server.state.connect(body.get("target", "")))
            if path.startswith("/robot/"): return self.proxy(path.removeprefix("/robot"), body)
            self.reply(404, {"error": "Not found"})
        except (ValueError, TypeError, OverflowError) as error: self.reply(400, {"error": str(error)})
    def proxy(self, path, body):
        if path not in (GETS if body is None else POSTS):
            return self.reply(403, {"error": "Endpoint is outside the hardware tester"})
        state = self.server.state
        with state.lock: target, generation = state.target, state.generation
        if not target or self.headers.get("X-Hardware-Generation") != generation:
            return self.reply(409, {"error": "Connect to the robot before using its APIs"})
        data = validate_body(path, body) if body is not None else None
        request = urllib.request.Request(target + path, data=data, headers={"Content-Type": "application/json", "Connection": "close"})
        def forward():
            try:
                with state.lock:
                    if generation != state.generation: return self.reply(409, {"error": "Robot connection changed"})
                with state.opener.open(request, timeout=1.2) as response:
                    result = response.read(131073)
                    if len(result) > 131072: return self.reply(502, {"error": "Robot response exceeded the diagnostic limit"})
                    return self.reply(response.status, result, response.headers.get_content_type())
            except urllib.error.HTTPError as error:
                return self.reply(error.code, {"error": error.read(2048).decode("utf-8", "replace")})
            except (OSError, urllib.error.URLError) as error:
                return self.reply(502, {"error": "Robot unavailable. Check gateway address, Wi-Fi and power.", "detail": str(error)[:180]})
        if data is None: return forward()
        with state.writes: return forward()

def make_server(host="127.0.0.1", port=8765):
    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    server.state = State()
    return server
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--open", action="store_true")
    args = parser.parse_args()
    url = f"http://127.0.0.1:{args.port}/"
    try: server = make_server(args.listen, args.port)
    except OSError:
        if args.open:
            try:
                opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
                with opener.open(url + "health", timeout=1) as response:
                    if json.load(response).get("service") == "gladiator-hardware-test":
                        webbrowser.open(url)
                        print("Opened the already running hardware tester.")
                        return
            except (OSError, ValueError): pass
        raise
    url = f"http://127.0.0.1:{server.server_port}/"
    print(f"Gladiator hardware test: {url}", flush=True)
    print("Connect in the page. Starting this server sends no commands to Gladiator.", flush=True)
    if args.listen != "127.0.0.1": print("LAN access enabled explicitly; keep this PC on your trusted robot network.", flush=True)
    if args.open: webbrowser.open(url)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: server.server_close()
if __name__ == "__main__": main()
