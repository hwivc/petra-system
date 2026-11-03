"""
Xiaozhi MCP WebSocket Client for Petra Virtual Display
------------------------------------------------------
Connects to xiaozhi.me MCP WebSocket endpoint and registers the 5 panel tools:
- show_message(text)
- show_card(title, lines, verdict)
- show_steps(title, steps)
- show_qr(url, label)
- clear_display()

Protocol matches firmware/shared-libs/xiaozhi-mcp/src/WebSocketMCP.cpp
"""

import json
import logging
import ssl
import threading
import time
import websocket

logger = logging.getLogger("VirtualDisplay.MCP")

# Exact tool schemas matching firmware/epaper-panel/src/main.cpp
TOOLS_DEFINITIONS = [
    {
        "name": "show_message",
        "description": "Show a short text message on the large black-and-white ePaper display at the PlastiBytes stand (800x480). Use it when the visitor asks to write or display something. The screen takes about 3 seconds to refresh.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "text": {
                    "type": "string",
                    "description": "Message to show, max about 150 characters"
                }
            },
            "required": ["text"]
        }
    },
    {
        "name": "show_card",
        "description": "Show an educational card or quiz check on the ePaper display. Use it: (1) for concept summaries (verdict 'none', up to 4 key points); (2) for quizzes with a question in title and choices in lines; and (3) for feedback with verdict 'yes' (Correct! 🎉) or 'no' (Try again! 💡). Also used for recycling inspection (verdict 'yes' for recyclable).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "title": {
                    "type": "string",
                    "description": "Short title or question, max 35 characters"
                },
                "lines": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Up to 4 short lines or quiz options"
                },
                "verdict": {
                    "type": "string",
                    "enum": ["yes", "no", "none", "correct", "wrong"],
                    "description": "yes / correct = badge (Correct! or Recyclable), no / wrong = badge (Try again), none = standard card"
                },
                "badge_text": {
                    "type": "string",
                    "description": "Optional custom badge text (e.g. 'Correct!', 'Try again!', 'Recyclable')"
                }
            },
            "required": ["title", "lines"]
        }
    },
    {
        "name": "show_diagram",
        "description": "Show a flowchart, circuit schematic, or system flow on the ePaper display. Use it when explaining workflows, algorithms, electronics circuits (e.g. 'Power 3.3V -> Resistor -> LED -> GND'), logic gates, or 3D printing pipelines.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "title": {
                    "type": "string",
                    "description": "Short diagram title, max 30 characters"
                },
                "content": {
                    "type": "string",
                    "description": "Diagram flow or nodes separated by '->' or newlines, e.g. 'Source Code -> Compiler -> Machine Binary -> Flash to ESP32'"
                },
                "diagram_type": {
                    "type": "string",
                    "enum": ["flowchart", "circuit", "concept", "auto"],
                    "description": "Optional diagram style"
                }
            },
            "required": ["title", "content"]
        }
    },
    {
        "name": "show_steps",
        "description": "Show numbered steps on the ePaper display (up to 5, each a few words). Use it when explaining a process, e.g. 'How we make filament': ['Collect bottles', 'Wash and shred', 'Melt and extrude', 'Wind on spools', '3D print in class']. Call it before you start explaining, then talk through the steps.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "title": {
                    "type": "string",
                    "description": "Short title, max 25 characters"
                },
                "steps": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "2 to 5 short steps"
                }
            },
            "required": ["title", "steps"]
        }
    },
    {
        "name": "show_qr",
        "description": "Show a big QR code with a short label on the ePaper display, so visitors can scan it with their phone. Use it when a visitor wants to learn more or sign up, e.g. https://plastibytes.com/mentorship (mentorship programme), https://plastibytes.com (website), https://plastibytes.com/blog (stories), https://hwivc.org (Hardware Innovation Valley Community), https://citscm.com (CITS26 summit), https://chat.whatsapp.com/I8a9PIMVv76Am17iXg2b9Y (PlastiBytes WhatsApp community).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "url": {
                    "type": "string",
                    "description": "Full https:// link"
                },
                "label": {
                    "type": "string",
                    "description": "One short sentence shown next to the QR code"
                }
            },
            "required": ["url", "label"]
        }
    },
    {
        "name": "clear_display",
        "description": "Clear the ePaper display and go back to the PlastiBytes welcome screens.",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    }
]


class XiaozhiMCPClient:
    def __init__(self, endpoint_url="", on_tool_call=None, on_status_change=None):
        self.endpoint_url = endpoint_url
        self.on_tool_call = on_tool_call
        self.on_status_change = on_status_change
        
        self.ws = None
        self.thread = None
        self._running = False
        self._connected = False
        self._lock = threading.Lock()

    def set_endpoint(self, url):
        with self._lock:
            self.endpoint_url = url.strip()
            if self._connected and self.ws:
                self.ws.close()

    def is_connected(self):
        return self._connected

    def start(self):
        if self._running:
            return
        self._running = True
        self.thread = threading.Thread(target=self._run_loop, daemon=True)
        self.thread.start()

    def stop(self):
        self._running = False
        if self.ws:
            self.ws.close()
        self._set_connected(False)

    def _set_connected(self, connected):
        if self._connected != connected:
            self._connected = connected
            if self.on_status_change:
                try:
                    self.on_status_change(connected)
                except Exception as e:
                    logger.error(f"Error in on_status_change: {e}")

    def _send_json(self, payload):
        if not self.ws or not self._connected:
            return False
        try:
            msg = json.dumps(payload)
            logger.debug(f"MCP TX: {msg}")
            self.ws.send(msg)
            return True
        except Exception as e:
            logger.error(f"Error sending MCP payload: {e}")
            return False

    def _run_loop(self):
        backoff = 1.0
        while self._running:
            url = self.endpoint_url
            if not url:
                self._set_connected(False)
                time.sleep(2)
                continue

            logger.info(f"Connecting to Xiaozhi MCP WebSocket: {url}")
            try:
                sslopt = {"cert_reqs": ssl.CERT_NONE} if url.startswith("wss://") else None
                self.ws = websocket.WebSocketApp(
                    url,
                    on_open=self._on_open,
                    on_message=self._on_message,
                    on_error=self._on_error,
                    on_close=self._on_close,
                )
                self.ws.run_forever(sslopt=sslopt, ping_interval=20, ping_timeout=10)
            except Exception as e:
                logger.warning(f"WebSocket connection error: {e}")

            self._set_connected(False)
            if not self._running:
                break
            logger.info(f"Reconnecting to MCP in {backoff:.1f}s...")
            time.sleep(backoff)
            backoff = min(backoff * 1.5, 30.0)

    def _on_open(self, ws):
        logger.info("Xiaozhi MCP WebSocket connected.")
        self._set_connected(True)

    def _on_error(self, ws, error):
        logger.warning(f"Xiaozhi MCP WebSocket error: {error}")

    def _on_close(self, ws, close_status_code, close_msg):
        logger.info(f"Xiaozhi MCP WebSocket closed: code={close_status_code}, msg={close_msg}")
        self._set_connected(False)

    def _on_message(self, ws, message):
        logger.debug(f"MCP RX: {message}")
        try:
            data = json.loads(message)
        except Exception as e:
            logger.warning(f"Failed to parse JSON-RPC message: {e}")
            return

        method = data.get("method")
        msg_id = data.get("id")

        if method == "ping":
            self._send_json({"jsonrpc": "2.0", "id": msg_id, "result": {}})
            return

        if method == "initialize":
            # Match firmware response
            resp = {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {
                        "experimental": {},
                        "prompts": {"listChanged": False},
                        "resources": {"subscribe": False, "listChanged": False},
                        "tools": {"listChanged": False}
                    },
                    "serverInfo": {
                        "name": "ESP-HA",
                        "version": "1.0.0"
                    }
                }
            }
            self._send_json(resp)
            # Send initialized notification
            self._send_json({"jsonrpc": "2.0", "method": "notifications/initialized"})
            logger.info("Responded to initialize and sent initialized notification.")
            return

        if method == "tools/list":
            resp = {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "tools": TOOLS_DEFINITIONS
                }
            }
            self._send_json(resp)
            logger.info("Sent tools/list with 5 registered display tools.")
            return

        if method == "tools/call":
            params = data.get("params", {})
            tool_name = params.get("name", "")
            args = params.get("arguments", {})
            logger.info(f"MCP Tool call received: {tool_name}({args})")

            result_text = "OK"
            if self.on_tool_call:
                try:
                    result_text = self.on_tool_call(tool_name, args)
                except Exception as e:
                    logger.error(f"Error handling tool call: {e}")
                    result_text = f"Error: {e}"

            # Format tool response
            resp = {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "content": [
                        {
                            "type": "text",
                            "text": json.dumps({"success": True, "result": result_text})
                        }
                    ],
                    "isError": False
                }
            }
            self._send_json(resp)
            return

