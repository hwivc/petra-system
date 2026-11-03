"""
Petra Virtual ePaper Display GUI & Controller
--------------------------------------------
Desktop Tkinter window that renders the 800x480 ePaper screen.
Enhanced for high-refresh-rate PC displays:
  - Soft Modern Paper presentation mode (warm book paper #FAF9F5 + carbon charcoal #1C1E21)
  - 60fps smooth ink-settle cross-fade transitions
  - Lithographic photo smoothing
  - Window maximization, auto-scaling, and Fullscreen mode (F11 / button)
  - Interactive STEM education tools: show_diagram, show_card (quizzes), show_message (code), show_steps
  - One-click Theme toggle: 🌿 Soft Modern Paper vs ⚡ Classic Hardware E-Ink
"""

import json
import logging
import math
import os
import queue
import sys
import threading
import time
import tkinter as tk
from io import BytesIO
from tkinter import messagebox, simpledialog, ttk
from PIL import Image, ImageTk

import renderer
from http_server import LocalHttpServer
from mcp_client import XiaozhiMCPClient
from mdns_advertiser import MDNSAdvertiser, get_lan_ip

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(name)s] %(levelname)s: %(message)s")
logger = logging.getLogger("VirtualDisplay.App")

CONFIG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")

DEFAULT_CONFIG = {
    "mcp_endpoint": "",
    "event_name": "CITS26 - Yaounde",
    "http_port": 80,
    "idle_rotate_sec": 90,
    "idle_timeout_sec": 120,
    "theme": "soft",             # "soft" or "hardware"
    "transition": "fade",        # "fade", "flash", or "none"
}

IDLE_SCREENS = ["welcome", "mentorship", "website", "event", "hwivc", "whatsapp"]


def load_config():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                cfg = json.load(f)
                res = dict(DEFAULT_CONFIG)
                res.update(cfg)
                return res
        except Exception as e:
            logger.warning(f"Failed to read {CONFIG_FILE}: {e}")
    return dict(DEFAULT_CONFIG)


def save_config(cfg):
    try:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
    except Exception as e:
        logger.error(f"Failed to write {CONFIG_FILE}: {e}")


class VirtualDisplayApp:
    def __init__(self, root):
        self.root = root
        self.config = load_config()

        # Apply saved theme to renderer
        renderer.set_theme(self.config.get("theme", "soft"))

        self.root.title("Petra Virtual Display (800x480)")
        self.root.configure(bg="#1e2022")
        self.root.geometry("880x640")
        self.root.minsize(840, 560)
        self.root.resizable(True, True)

        # Fullscreen state
        self.is_fullscreen = False
        self.root.bind("<F11>", lambda e: self.toggle_fullscreen())
        self.root.bind("<Escape>", lambda e: self.exit_fullscreen())

        # Thread-safe event queue for GUI updates
        self.gui_queue = queue.Queue()

        # State tracking
        self.is_busy_flag = False
        self.current_screen_type = "idle"
        self.current_idle_idx = 0
        self.last_user_content_time = 0
        self.last_idle_rotate_time = time.time()
        self.mcp_connected = False

        # Current image tracking
        self.current_display_img = renderer.new_canvas()
        self.current_postcard_img = None
        self.tk_photo_image = None
        self._resize_job = None
        self._fade_job = None

        # Build UI layout
        self._build_ui()

        # Initialize Services
        self._init_services()

        # Initial screen
        if not self.config.get("mcp_endpoint"):
            self.show_setup_screen("Setup MCP Endpoint")
        else:
            self.show_idle_screen("welcome")

        # Start periodic GUI queue processor and idle rotation timer
        self.root.after(100, self._process_gui_queue)
        self.root.after(1000, self._check_idle_timer)

        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def _build_ui(self):
        # 1. Bezel frame around display
        self.bezel = tk.Frame(self.root, bg="#2a2d32", bd=3, relief="ridge")
        self.bezel.pack(fill="both", expand=True, padx=12, pady=10)

        # Responsive Canvas
        self.canvas = tk.Canvas(
            self.bezel,
            bg="#181a1c",
            highlightthickness=0,
        )
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Configure>", self._on_canvas_configure)

        # 2. Status Panel below screen
        status_frame = tk.Frame(self.root, bg="#141618", padx=10, pady=6)
        status_frame.pack(fill="x", padx=12, pady=(0, 4))

        # Row 1: Network & Protocol indicators
        row1 = tk.Frame(status_frame, bg="#141618")
        row1.pack(fill="x")

        self.lbl_mdns = tk.Label(
            row1,
            text="mDNS: epaper.local:80",
            fg="#4CAF50",
            bg="#141618",
            font=("Segoe UI", 9, "bold"),
        )
        self.lbl_mdns.pack(side="left", padx=(0, 15))

        self.lbl_http = tk.Label(
            row1,
            text="LAN URL: http://...",
            fg="#b0b0b0",
            bg="#141618",
            font=("Segoe UI", 9),
        )
        self.lbl_http.pack(side="left", padx=10)

        self.lbl_mcp = tk.Label(
            row1,
            text="Xiaozhi MCP: Connecting...",
            fg="#FFA000",
            bg="#141618",
            font=("Segoe UI", 9, "bold"),
        )
        self.lbl_mcp.pack(side="right")

        # Row 2: Action buttons and controls
        btn_frame = tk.Frame(self.root, bg="#1e2022", padx=8, pady=5)
        btn_frame.pack(fill="x", padx=12, pady=(0, 8))

        # Configuration button
        btn_cfg = tk.Button(
            btn_frame,
            text="⚙ Configure MCP",
            command=self.prompt_mcp_endpoint,
            bg="#363a40",
            fg="#ffffff",
            activebackground="#4a5058",
            activeforeground="#ffffff",
            relief="flat",
            padx=8,
            pady=3,
            font=("Segoe UI", 9),
        )
        btn_cfg.pack(side="left", padx=(0, 6))

        # Simulation / Demo Buttons
        demos = [
            ("📷 Photo", self.demo_photo),
            ("📊 Diagram", self.demo_diagram),
            ("📋 Card / Quiz", self.demo_card),
            ("🔢 Steps", self.demo_steps),
            ("🔗 QR", self.demo_qr),
            ("🏠 Welcome", lambda: self.show_idle_screen("welcome")),
        ]

        for text, cmd in demos:
            b = tk.Button(
                btn_frame,
                text=text,
                command=cmd,
                bg="#2a2d32",
                fg="#dcdcdc",
                activebackground="#3e434a",
                activeforeground="#ffffff",
                relief="flat",
                padx=6,
                pady=3,
                font=("Segoe UI", 8),
            )
            b.pack(side="left", padx=3)

        # Fullscreen / Maximize toggle button
        self.btn_fullscreen = tk.Button(
            btn_frame,
            text="⛶ Fullscreen (F11)",
            command=self.toggle_fullscreen,
            bg="#363a40",
            fg="#ffffff",
            activebackground="#4a5058",
            activeforeground="#ffffff",
            relief="flat",
            padx=7,
            pady=3,
            font=("Segoe UI", 8, "bold"),
        )
        self.btn_fullscreen.pack(side="right", padx=(6, 0))

        # Theme Switcher Button (Soft Paper vs Hardware E-Ink)
        self.btn_theme = tk.Button(
            btn_frame,
            text="🌿 Soft Mode" if self.config.get("theme", "soft") == "soft" else "⚡ E-Ink Mode",
            command=self.toggle_theme,
            bg="#2a2d32",
            fg="#64B5F6" if self.config.get("theme", "soft") == "soft" else "#FFB74D",
            activebackground="#363a40",
            activeforeground="#ffffff",
            relief="flat",
            padx=7,
            pady=3,
            font=("Segoe UI", 8, "bold"),
        )
        self.btn_theme.pack(side="right", padx=6)

        # Transition Selector
        self.var_trans = tk.StringVar(value=self.config.get("transition", "fade"))
        trans_menu = ttk.Combobox(
            btn_frame,
            textvariable=self.var_trans,
            values=["fade", "flash", "none"],
            state="readonly",
            width=7,
            font=("Segoe UI", 8),
        )
        trans_menu.bind("<<ComboboxSelected>>", self._on_trans_change)
        trans_menu.pack(side="right", padx=4)

        lbl_trans = tk.Label(btn_frame, text="FX:", bg="#1e2022", fg="#888888", font=("Segoe UI", 8))
        lbl_trans.pack(side="right")

    def toggle_theme(self):
        current = self.config.get("theme", "soft")
        new_theme = "hardware" if current == "soft" else "soft"
        self.config["theme"] = new_theme
        save_config(self.config)
        renderer.set_theme(new_theme)
        self.btn_theme.config(
            text="🌿 Soft Mode" if new_theme == "soft" else "⚡ E-Ink Mode",
            fg="#64B5F6" if new_theme == "soft" else "#FFB74D",
        )
        # Redraw current screen in new theme
        self._refresh_current_screen()

    def _on_trans_change(self, event=None):
        self.config["transition"] = self.var_trans.get()
        save_config(self.config)

    def _refresh_current_screen(self):
        if self.current_screen_type == "idle":
            self.show_idle_screen(IDLE_SCREENS[self.current_idle_idx])
        elif self.current_screen_type == "welcome":
            self.show_idle_screen("welcome")
        else:
            self._redraw_scaled_display()

    def toggle_fullscreen(self):
        self.is_fullscreen = not self.is_fullscreen
        self.root.attributes("-fullscreen", self.is_fullscreen)
        if self.is_fullscreen:
            self.btn_fullscreen.config(text="✕ Exit Fullscreen (Esc)")
        else:
            self.btn_fullscreen.config(text="⛶ Fullscreen (F11)")
        self.root.after(50, self._redraw_scaled_display)

    def exit_fullscreen(self):
        if self.is_fullscreen:
            self.is_fullscreen = False
            self.root.attributes("-fullscreen", False)
            self.btn_fullscreen.config(text="⛶ Fullscreen (F11)")
            self.root.after(50, self._redraw_scaled_display)

    def _on_canvas_configure(self, event):
        """Debounced redraw on canvas resize."""
        if self._resize_job is not None:
            self.root.after_cancel(self._resize_job)
        self._resize_job = self.root.after(40, self._redraw_scaled_display)

    def _redraw_scaled_display(self):
        self._resize_job = None
        if not self.current_display_img:
            return

        canvas_w = self.canvas.winfo_width()
        canvas_h = self.canvas.winfo_height()

        if canvas_w < 50 or canvas_h < 50:
            return

        scale = min(canvas_w / renderer.SCREEN_WIDTH, canvas_h / renderer.SCREEN_HEIGHT)
        new_w = max(100, int(renderer.SCREEN_WIDTH * scale))
        new_h = max(60, int(renderer.SCREEN_HEIGHT * scale))

        scaled = self.current_display_img.resize((new_w, new_h), Image.Resampling.BILINEAR)
        self.tk_photo_image = ImageTk.PhotoImage(scaled)

        self.canvas.delete("all")
        cx = canvas_w // 2
        cy = canvas_h // 2
        self.canvas.create_image(cx, cy, anchor="center", image=self.tk_photo_image)

    def _init_services(self):
        port = self.config.get("http_port", 80)
        self.http_server = LocalHttpServer(port=port, display_manager=self)
        try:
            self.http_server.start()
            lan_ip = get_lan_ip()
            active_p = self.http_server.active_port
            self.lbl_http.config(text=f"LAN URL: http://{lan_ip}:{active_p}")
            self.lbl_mdns.config(text=f"mDNS: epaper.local:{active_p}")
        except Exception as e:
            logger.error(f"Failed to start HTTP server: {e}")
            self.lbl_http.config(text="HTTP: Error binding port", fg="#f44336")

        active_p = self.http_server.active_port if self.http_server else 80
        self.mdns = MDNSAdvertiser(port=active_p, hostname="epaper")
        threading.Thread(target=self.mdns.start, daemon=True).start()

        endpoint = self.config.get("mcp_endpoint", "")
        self.mcp = XiaozhiMCPClient(
            endpoint_url=endpoint,
            on_tool_call=self.handle_mcp_tool_call,
            on_status_change=self.on_mcp_status_change,
        )
        self.mcp.start()
        self._update_mcp_status_label()

    def _update_mcp_status_label(self):
        if not self.config.get("mcp_endpoint"):
            self.lbl_mcp.config(text="Xiaozhi MCP: Not configured", fg="#888888")
        elif self.mcp_connected:
            self.lbl_mcp.config(text="Xiaozhi MCP: Online", fg="#4CAF50")
        else:
            self.lbl_mcp.config(text="Xiaozhi MCP: Connecting...", fg="#FFA000")

    def on_mcp_status_change(self, connected):
        self.mcp_connected = connected
        self.gui_queue.put(("mcp_status", connected))

    def prompt_mcp_endpoint(self):
        current = self.config.get("mcp_endpoint", "")
        res = simpledialog.askstring(
            "Xiaozhi MCP WebSocket Endpoint",
            "Paste your Xiaozhi MCP WebSocket URL:\n(Found in xiaozhi.me -> Console -> Agents -> Petra -> MCP endpoint)",
            initialvalue=current,
            parent=self.root,
        )
        if res is not None:
            endpoint = res.strip()
            self.config["mcp_endpoint"] = endpoint
            save_config(self.config)
            self.mcp.set_endpoint(endpoint)
            self._update_mcp_status_label()
            if endpoint:
                messagebox.showinfo("Saved", "MCP endpoint saved. Connecting to Xiaozhi...", parent=self.root)
                self.show_idle_screen("welcome")
            else:
                self.show_setup_screen("Configure MCP Endpoint")

    # ------------------------------------------------------------- Display API
    def is_busy(self):
        return self.is_busy_flag

    def get_postcard_png(self):
        if self.current_postcard_img:
            buf = BytesIO()
            self.current_postcard_img.save(buf, format="PNG")
            return buf.getvalue()
        return None

    def post_photo(self, raw_bitmap, caption, frame):
        self.gui_queue.put(("photo", (raw_bitmap, caption, frame)))

    def handle_mcp_tool_call(self, tool_name, args):
        self.gui_queue.put(("mcp_tool", (tool_name, args)))
        if tool_name == "show_message":
            return "Message will appear on the display in a few seconds"
        elif tool_name == "show_card":
            return "Card will appear on the display in a few seconds"
        elif tool_name == "show_steps":
            return "Steps will appear on the display in a few seconds"
        elif tool_name == "show_diagram":
            return "Diagram will appear on the display in a few seconds"
        elif tool_name == "show_qr":
            return "QR code will appear on the display in a few seconds"
        elif tool_name == "clear_display":
            return "Display cleared"
        return "OK"

    # ----------------------------------------------------------- Screen Draws
    def _status_text(self):
        lan_ip = get_lan_ip()
        status_str = "Online" if self.mcp_connected else "Petra link: connecting"
        return f"{status_str}  {lan_ip}"

    def show_idle_screen(self, screen_type):
        self.current_screen_type = "idle"
        st = self._status_text()
        event_name = self.config.get("event_name", "CITS26 - Yaounde")

        if screen_type == "mentorship":
            img = renderer.render_qr_screen(
                "https://plastibytes.com/mentorship",
                "Become Africa's next hardware innovator: join the PlastiBytes Mentorship",
                event_name,
            )
        elif screen_type == "website":
            img = renderer.render_qr_screen(
                "https://plastibytes.com",
                "We turn plastic bottles into 3D-printing filament. Discover PlastiBytes",
                event_name,
            )
        elif screen_type == "event":
            img = renderer.render_event_screen(st, event_name)
        elif screen_type == "hwivc":
            img = renderer.render_qr_screen(
                "https://hwivc.org",
                "Join the Hardware Innovation Valley Community",
                event_name,
            )
        elif screen_type == "whatsapp":
            img = renderer.render_qr_screen(
                "https://chat.whatsapp.com/I8a9PIMVv76Am17iXg2b9Y",
                "Scan to join the PlastiBytes WhatsApp community",
                event_name,
            )
        else:
            img = renderer.render_welcome(st, event_name)

        self._render_to_canvas(img)

    def show_setup_screen(self, detail="Configure MCP"):
        self.current_screen_type = "setup"
        img = renderer.render_setup("PlastiBytes-Display-Virtual", detail)
        self._render_to_canvas(img)

    def show_photo_screen(self, raw_bitmap, caption, frame):
        self.current_screen_type = "photo"
        self.last_user_content_time = time.time()
        event_name = self.config.get("event_name", "CITS26 - Yaounde")
        lan_ip = get_lan_ip()
        port = self.http_server.active_port if self.http_server else 80
        download_url = f"http://{lan_ip}:{port}/"

        disp_img, copy_img = renderer.render_photo(
            raw_bitmap, caption, frame, event_name=event_name, download_qr=download_url
        )
        self.current_postcard_img = copy_img
        self._render_to_canvas(disp_img)

    def show_message_screen(self, text):
        self.current_screen_type = "message"
        self.last_user_content_time = time.time()
        img = renderer.render_message(text, status='Say "Hey Petra"')
        self._render_to_canvas(img)

    def show_card_screen(self, title, lines, verdict="none", badge_text=""):
        self.current_screen_type = "card"
        self.last_user_content_time = time.time()
        img = renderer.render_card(title, lines, verdict, badge_text=badge_text, status="Interactive STEM Guide")
        self._render_to_canvas(img)

    def show_steps_screen(self, title, steps):
        self.current_screen_type = "steps"
        self.last_user_content_time = time.time()
        img = renderer.render_steps(title, steps, status="Explained by Petra")
        self._render_to_canvas(img)

    def show_diagram_screen(self, title, content, diagram_type="auto"):
        self.current_screen_type = "diagram"
        self.last_user_content_time = time.time()
        img = renderer.render_diagram(title, content, diagram_type, status="Visual Diagram by Petra")
        self._render_to_canvas(img)

    def show_qr_screen(self, url, label):
        self.current_screen_type = "qr"
        self.last_user_content_time = time.time()
        event_name = self.config.get("event_name", "CITS26 - Yaounde")
        img = renderer.render_qr_screen(url, label, event_name)
        self._render_to_canvas(img)

    def _render_to_canvas(self, target_image):
        """
        Transitions to new image using:
        - 'fade' (default on PC): 60fps smooth ink cross-dissolve over 140ms
        - 'flash': hardware e-paper black blink
        - 'none': instant update
        """
        trans = self.var_trans.get()

        if trans == "fade" and self.current_display_img:
            # 60 FPS smooth ink dissolve (8 frames over 130ms)
            start_img = self.current_display_img
            steps = 8
            step_ms = 16  # 60Hz frame interval

            def do_step(step_idx):
                if step_idx >= steps:
                    self.current_display_img = target_image
                    self._redraw_scaled_display()
                    self.is_busy_flag = False
                    return
                # Cosine easing for smooth ink settling
                progress = (step_idx + 1) / steps
                alpha = 0.5 - 0.5 * math.cos(math.pi * progress)
                blended = Image.blend(start_img, target_image, alpha)
                self.current_display_img = blended
                self._redraw_scaled_display()
                self._fade_job = self.root.after(step_ms, lambda: do_step(step_idx + 1))

            self.is_busy_flag = True
            do_step(0)

        elif trans == "flash":
            self.is_busy_flag = True
            canvas_w = max(100, self.canvas.winfo_width())
            canvas_h = max(60, self.canvas.winfo_height())
            scale = min(canvas_w / renderer.SCREEN_WIDTH, canvas_h / renderer.SCREEN_HEIGHT)
            nw, nh = int(renderer.SCREEN_WIDTH * scale), int(renderer.SCREEN_HEIGHT * scale)

            black_img = Image.new("RGB", (nw, nh), renderer.INK_COLOR)
            tk_black = ImageTk.PhotoImage(black_img)
            self.canvas.delete("all")
            self.canvas.create_image(canvas_w // 2, canvas_h // 2, anchor="center", image=tk_black)
            self.canvas.image = tk_black
            self.root.update_idletasks()

            def finish_flash():
                self.current_display_img = target_image
                self._redraw_scaled_display()
                self.is_busy_flag = False

            self.root.after(140, finish_flash)

        else:
            self.current_display_img = target_image
            self._redraw_scaled_display()

    # --------------------------------------------------------- Queue & Timers
    def _process_gui_queue(self):
        try:
            while not self.gui_queue.empty():
                event_type, data = self.gui_queue.get_nowait()
                if event_type == "mcp_status":
                    self._update_mcp_status_label()
                    if self.current_screen_type == "idle":
                        self.show_idle_screen(IDLE_SCREENS[self.current_idle_idx])
                elif event_type == "photo":
                    raw_bitmap, caption, frame = data
                    self.show_photo_screen(raw_bitmap, caption, frame)
                elif event_type == "mcp_tool":
                    name, args = data
                    if name == "show_message":
                        self.show_message_screen(args.get("text", ""))
                    elif name == "show_card":
                        self.show_card_screen(
                            args.get("title", "Concept Summary"),
                            args.get("lines", []),
                            args.get("verdict", "none"),
                            args.get("badge_text", ""),
                        )
                    elif name == "show_steps":
                        self.show_steps_screen(
                            args.get("title", "Process Steps"),
                            args.get("steps", []),
                        )
                    elif name == "show_diagram":
                        self.show_diagram_screen(
                            args.get("title", "System Flow"),
                            args.get("content", ""),
                            args.get("diagram_type", "auto"),
                        )
                    elif name == "show_qr":
                        self.show_qr_screen(
                            args.get("url", "https://plastibytes.com"),
                            args.get("label", "PlastiBytes Mentorship"),
                        )
                    elif name == "clear_display":
                        self.current_idle_idx = 0
                        self.show_idle_screen("welcome")
        except Exception as e:
            logger.error(f"Error processing GUI queue: {e}")

        self.root.after(100, self._process_gui_queue)

    def _check_idle_timer(self):
        now = time.time()
        rotate_sec = self.config.get("idle_rotate_sec", 90)
        timeout_sec = self.config.get("idle_timeout_sec", 120)

        if self.current_screen_type != "idle" and self.current_screen_type != "setup":
            if self.last_user_content_time > 0 and (now - self.last_user_content_time > timeout_sec):
                logger.info("Returning to idle rotation after user content timeout.")
                self.current_idle_idx = 0
                self.show_idle_screen("welcome")
                self.last_idle_rotate_time = now

        elif self.current_screen_type == "idle":
            if now - self.last_idle_rotate_time > rotate_sec:
                self.current_idle_idx = (self.current_idle_idx + 1) % len(IDLE_SCREENS)
                self.show_idle_screen(IDLE_SCREENS[self.current_idle_idx])
                self.last_idle_rotate_time = now

        self.root.after(1000, self._check_idle_timer)

    # ------------------------------------------------------------- Demos
    def demo_photo(self):
        img = Image.new("L", (renderer.PHOTO_WIDTH, renderer.PHOTO_HEIGHT), 255)
        d = ImageDraw.Draw(img)
        for r in range(20, 200, 25):
            d.ellipse([400 - r, 200 - r, 400 + r, 200 + r], outline=40, width=4)
        d.rectangle([100, 80, 700, 320], outline=80, width=2)
        d.text((320, 185), "CAMERA CAPTURE", fill=0, font=renderer.FONT_TITLE_BOLD)

        dithered = img.convert("1")
        inv_bytes = bytes([~b & 0xFF for b in dithered.tobytes()])
        frames = ["classic", "polaroid", "magazine", "hero", "wanted", "comic", "film", "event"]
        import random
        frame = random.choice(frames)
        self.show_photo_screen(inv_bytes, f"Live from Yaounde! ({frame.capitalize()})", frame)

    def demo_diagram(self):
        demos = [
            ("LED Circuit Schematic", "Power 3.3V -> Resistor 220Ω -> LED Anode -> LED Cathode -> GND"),
            ("How Code Compiles", "C++ Source -> ESP-IDF Compiler -> Machine Binary -> Flash to ESP32"),
            ("3D Printing Pipeline", "Collect Bottles -> Shred to Flakes -> Extrude Filament -> 3D Print"),
        ]
        import random
        title, content = random.choice(demos)
        self.show_diagram_screen(title, content)

    def demo_card(self):
        cards = [
            (
                "Quiz: Which LED leg is positive?",
                ["Longer leg (Anode)", "Shorter leg (Cathode)", "Flat side of lens", "Both are identical"],
                "correct",
                "CORRECT! 🎉",
            ),
            (
                "Concept: Ohm's Law",
                ["V = I x R (Voltage = Current x Resistance)", "Voltage pushes electric charges", "Resistors limit current flow"],
                "none",
                "",
            ),
            (
                "PET Bottle Inspection",
                ["Type 1 clear plastic", "Shred, wash, and extrude", "Becomes 3D printer filament"],
                "yes",
                "",
            ),
        ]
        import random
        title, lines, verdict, badge = random.choice(cards)
        self.show_card_screen(title, lines, verdict=verdict, badge_text=badge)

    def demo_steps(self):
        self.show_steps_screen(
            "How we make filament",
            [
                "Collect plastic bottles",
                "Wash and shred flakes",
                "Melt and extrude into wire",
                "Wind onto recycled spools",
                "3D print STEM models in class",
            ],
        )

    def demo_qr(self):
        self.show_qr_screen(
            "https://plastibytes.com/mentorship",
            "Become Africa's next hardware innovator: join the PlastiBytes Mentorship programme",
        )

    def on_close(self):
        logger.info("Shutting down Virtual Display...")
        if hasattr(self, "mcp"):
            self.mcp.stop()
        if hasattr(self, "http_server"):
            self.http_server.stop()
        if hasattr(self, "mdns"):
            self.mdns.stop()
        self.root.destroy()
        sys.exit(0)
