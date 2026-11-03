"""
Local HTTP Server for Petra Virtual Display
-------------------------------------------
Mimics firmware/epaper-panel/src/main.cpp and download.cpp:
- POST /photo: receives raw 40,000 bytes 1-bit bitmap, X-Caption, X-Frame
- GET /status: returns {"ok": true, "busy": false}
- GET /postcard.png: downloads the current postcard PNG
- GET /: captive portal / digital copy page with Download button
"""

import http.server
import json
import logging
import socketserver
import threading
import urllib.parse
from io import BytesIO

logger = logging.getLogger("VirtualDisplay.HTTP")

class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class EpaperHTTPHandler(http.server.BaseHTTPRequestHandler):
    # Overridden by server setup
    display_manager = None

    def log_message(self, format, *args):
        # Clean logging
        logger.info(f"{self.client_address[0]} - {format % args}")

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, X-Caption, X-Frame")
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/status":
            busy = self.display_manager.is_busy() if self.display_manager else False
            body = json.dumps({"ok": True, "busy": busy}).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(body)
            return

        if path in ("/postcard.png", "/photo.png"):
            png_bytes = self.display_manager.get_postcard_png() if self.display_manager else None
            if not png_bytes:
                self.send_error(404, "No postcard available")
                return
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(png_bytes)))
            self.send_header("Content-Disposition", "inline; filename=plastibytes-postcard.png")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(png_bytes)
            return

        if path in ("/", "/index.html", "/index"):
            # Digital copy captive page matching download.cpp
            html = """<!doctype html>
<html>
<head>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>Your PlastiBytes postcard</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; text-align: center; padding: 16px; max-width: 540px; margin: auto; background: #f8f8f6; color: #111; }
        h2 { margin-top: 10px; font-weight: 700; }
        img { width: 100%; border: 1px solid #ccc; border-radius: 8px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); }
        a.btn { display: block; margin: 20px 0; padding: 14px; background: #111; color: #fff; border-radius: 8px; text-decoration: none; font-size: 18px; font-weight: 600; }
        a.btn:hover { background: #333; }
        small { color: #666; line-height: 1.5; display: inline-block; }
        .footer { margin-top: 24px; color: #555; }
    </style>
</head>
<body>
    <h2>Your PlastiBytes Postcard</h2>
    <img src="/postcard.png" alt="postcard">
    <a class="btn" href="/postcard.png" download="plastibytes-postcard.png">Download Postcard</a>
    <p><small>Mobile: tap and hold the picture, then select <b>Save to Photos</b>.<br>
    Rendered on Petra Virtual ePaper Display.</small></p>
    <div class="footer"><p>Follow us: <b>plastibytes.com</b></p></div>
</body>
</html>"""
            body = html.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        # Redirect probes to home page
        self.send_response(302)
        self.send_header("Location", "/")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/photo":
            if self.display_manager and self.display_manager.is_busy():
                self.send_response(503, "Busy")
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                self.wfile.write(b"busy")
                return

            try:
                content_length = int(self.headers.get("Content-Length", 0))
            except ValueError:
                content_length = 0

            if content_length != 40000:
                self.send_response(400, "Bad Request")
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                self.wfile.write(b"bad size")
                return

            # Read exactly 40,000 bytes
            raw_bitmap = self.rfile.read(40000)
            if len(raw_bitmap) != 40000:
                self.send_response(400, "Bad Request")
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                self.wfile.write(b"bad size")
                return

            # Read headers
            raw_caption = self.headers.get("X-Caption", "")
            caption = urllib.parse.unquote(raw_caption)[:80]
            frame = self.headers.get("X-Frame", "classic").strip()

            logger.info(f"Received photo: frame={frame}, caption='{caption}'")

            if self.display_manager:
                self.display_manager.post_photo(raw_bitmap, caption, frame)

            self.send_response(200, "OK")
            self.send_header("Content-Type", "text/plain")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(b"ok")
            return

        self.send_error(404, "Not Found")


class LocalHttpServer:
    def __init__(self, port=80, display_manager=None):
        self.requested_port = port
        self.active_port = port
        self.display_manager = display_manager
        self.server = None
        self.thread = None
        self._running = False

    def start(self):
        # Try requested port (80), fallback to 8080 if unavailable
        ports_to_try = [self.requested_port]
        if self.requested_port != 8080:
            ports_to_try.append(8080)
        ports_to_try.append(8888)

        for p in ports_to_try:
            try:
                handler = type("ConfiguredHandler", (EpaperHTTPHandler,), {"display_manager": self.display_manager})
                self.server = ThreadedHTTPServer(("0.0.0.0", p), handler)
                self.active_port = p
                break
            except Exception as e:
                logger.warning(f"Could not bind to port {p}: {e}")
                self.server = None

        if not self.server:
            raise RuntimeError(f"Failed to bind to any HTTP port in {ports_to_try}")

        self._running = True
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        logger.info(f"Local HTTP server listening on port {self.active_port}")

    def stop(self):
        if self.server:
            self.server.shutdown()
            self.server.server_close()
            self._running = False
            logger.info("Local HTTP server stopped.")

