#!/usr/bin/env python3
"""
Petra Virtual ePaper Display Launcher
-------------------------------------
Launches the virtual ePaper display window and services.

Usage:
    python run_virtual_display.py
    python run_virtual_display.py --mcp "wss://api.xiaozhi.me/mcp/your-endpoint"
    python run_virtual_display.py --port 80 --event "CITS26 - Yaounde"
"""

import argparse
import sys
import tkinter as tk

from virtual_screen import VirtualDisplayApp, load_config, save_config

def main():
    parser = argparse.ArgumentParser(description="Petra Virtual ePaper Display")
    parser.add_argument("--mcp", type=str, default="", help="Xiaozhi MCP WebSocket endpoint URL")
    parser.add_argument("--port", type=int, default=0, help="Local HTTP server port (default: 80)")
    parser.add_argument("--event", type=str, default="", help="Event name (default: CITS26 - Yaounde)")
    parser.add_argument("--no-flash", action="store_true", help="Disable E-Ink refresh simulation flash")
    args = parser.parse_args()

    config = load_config()
    changed = False

    if args.mcp:
        config["mcp_endpoint"] = args.mcp.strip()
        changed = True
    if args.port > 0:
        config["http_port"] = args.port
        changed = True
    if args.event:
        config["event_name"] = args.event.strip()
        changed = True
    if args.no_flash:
        config["simulate_eink_flash"] = False
        changed = True

    if changed:
        save_config(config)

    root = tk.Tk()
    app = VirtualDisplayApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()

