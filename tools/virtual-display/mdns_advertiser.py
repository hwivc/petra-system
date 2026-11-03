"""
mDNS Advertiser for Petra Virtual Display
-----------------------------------------
Advertises the virtual display on LAN as epaper.local on port 80 (or active port).
Petra's firmware uses mdns_query_a("epaper", ...) to find the panel.
"""

import logging
import socket
import threading
from zeroconf import IPVersion, ServiceInfo, Zeroconf

logger = logging.getLogger("VirtualDisplay.mDNS")

def get_lan_ip():
    """Find the local IPv4 address of this machine."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = "127.0.0.1"
    finally:
        s.close()
    return ip


class MDNSAdvertiser:
    def __init__(self, port=80, hostname="epaper"):
        self.port = port
        self.hostname = hostname
        self.lan_ip = get_lan_ip()
        self.zeroconf = None
        self.service_info = None
        self._running = False

    def start(self):
        try:
            self.lan_ip = get_lan_ip()
            ip_bytes = socket.inet_aton(self.lan_ip)

            # Service: epaper._http._tcp.local.
            # Host: epaper.local.
            service_name = f"{self.hostname}._http._tcp.local."
            server_host = f"{self.hostname}.local."

            self.service_info = ServiceInfo(
                type_="_http._tcp.local.",
                name=service_name,
                addresses=[ip_bytes],
                port=self.port,
                properties={"model": "ePaper-800x480", "device": "virtual-panel"},
                server=server_host,
            )

            self.zeroconf = Zeroconf(ip_version=IPVersion.V4Only)
            self.zeroconf.register_service(self.service_info)
            self._running = True
            logger.info(f"mDNS active: {server_host} -> {self.lan_ip}:{self.port}")
        except Exception as e:
            logger.warning(f"mDNS advertising could not start (non-fatal): {e}")
            self.zeroconf = None
            self._running = False

    def is_active(self):
        return self._running

    def stop(self):
        if self.zeroconf and self.service_info:
            try:
                self.zeroconf.unregister_service(self.service_info)
                self.zeroconf.close()
            except Exception as e:
                logger.warning(f"Error stopping mDNS: {e}")
            self.zeroconf = None
            self.service_info = None
            self._running = False
            logger.info("mDNS service unregistered.")

