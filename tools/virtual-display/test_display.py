#!/usr/bin/env python3
"""
Test script for Petra Virtual Display
-------------------------------------
1. Live Mode: Sends test requests to the running virtual screen:
   - GET /status
   - POST /photo with raw 40,000 bytes 1-bit bitmap
   - GET /postcard.png

2. Offline Self-Test Mode (--self-test):
   Tests rendering engine directly without needing a running server.
"""

import os
import sys
import urllib.request
import urllib.error
import urllib.parse
from PIL import Image, ImageDraw

def find_active_port(ports=(80, 8080)):
    for p in ports:
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{p}/status", timeout=1) as resp:
                if resp.status == 200:
                    return p
        except Exception:
            continue
    return None


def run_live_tests(port):
    base_url = f"http://127.0.0.1:{port}"
    print(f"Connecting to live Virtual Display at {base_url} ...\n")

    # 1. Test GET /status
    print(f"1. Testing GET {base_url}/status ...")
    try:
        with urllib.request.urlopen(f"{base_url}/status", timeout=5) as resp:
            data = resp.read().decode("utf-8")
            print(f"   -> Status {resp.status}: {data}")
            assert resp.status == 200
            assert "ok" in data
            print("   [PASSED] Display is online and responding.\n")
    except Exception as e:
        print(f"   [FAILED] {e}\n")
        return False

    # 2. Test POST /photo
    print(f"2. Testing POST {base_url}/photo (40,000 bytes 1-bit bitmap) ...")
    img = Image.new("1", (800, 400), 1)
    d = ImageDraw.Draw(img)
    d.rectangle([20, 20, 780, 380], outline=0, width=4)
    d.ellipse([100, 50, 700, 350], outline=0, width=6)
    d.text((260, 180), "LIVE TEST PHOTO", fill=0)

    # Invert so 1 = black ink
    raw_bitmap = bytes([~b & 0xFF for b in img.tobytes()])
    assert len(raw_bitmap) == 40000

    req = urllib.request.Request(
        f"{base_url}/photo",
        data=raw_bitmap,
        headers={
            "Content-Type": "application/octet-stream",
            "Content-Length": "40000",
            "X-Caption": urllib.parse.quote("Live Test Postcard from Petra!"),
            "X-Frame": "polaroid",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = resp.read().decode("utf-8")
            print(f"   -> Status {resp.status}: {data}")
            assert resp.status == 200
            print("   [PASSED] Photo received by virtual screen window.\n")
    except Exception as e:
        print(f"   [FAILED] {e}\n")
        return False

    # 3. Test GET /postcard.png
    print(f"3. Testing GET {base_url}/postcard.png (Digital Copy) ...")
    try:
        with urllib.request.urlopen(f"{base_url}/postcard.png", timeout=5) as resp:
            data = resp.read()
            print(f"   -> Status {resp.status}: Received {len(data):,} bytes PNG")
            assert resp.status == 200
            assert len(data) > 1000
            print("   [PASSED] Postcard digital copy downloaded successfully.\n")
    except Exception as e:
        print(f"   [FAILED] {e}\n")
        return False

    print("===================================================")
    print("  ALL LIVE TESTS PASSED! Virtual Display is ready. ")
    print("===================================================")
    return True


def run_self_test():
    print("Running Internal Component Self-Test (Offline Mode) ...\n")
    import renderer
    
    print("1. Testing Welcome Screen rendering...")
    w = renderer.render_welcome("Online 192.168.1.38")
    assert w.size == (800, 480)
    print("   [PASSED] Welcome screen rendered (800x480).")

    print("2. Testing Card Screen rendering...")
    c = renderer.render_card("PET bottle", ["Drinks bottle", "Shred, extrude", "Becomes filament"], "yes")
    assert c.size == (800, 480)
    print("   [PASSED] Card screen rendered (800x480).")

    print("3. Testing Steps Screen rendering...")
    s = renderer.render_steps("Filament Process", ["Collect", "Shred", "Extrude", "Spool", "Print"])
    assert s.size == (800, 480)
    print("   [PASSED] Steps screen rendered (800x480).")

    print("4. Testing Diagram Screen rendering...")
    d = renderer.render_diagram("LED Circuit", "Power 3.3V -> Resistor 220 Ohm -> LED -> GND")
    assert d.size == (800, 480)
    print("   [PASSED] Diagram screen rendered (800x480).")

    print("5. Testing Photo & Frame rendering...")
    dummy_bits = bytes([0x55] * 40000)
    disp, copy = renderer.render_photo(dummy_bits, "Test Caption", "polaroid", download_qr="http://192.168.1.38/")
    assert disp.size == (800, 480)
    assert copy.size == (800, 480)
    print("   [PASSED] Photo screen & postcard copy rendered (800x480).")

    print("\n===================================================")
    print("  ALL OFFLINE COMPONENT TESTS PASSED!              ")
    print("===================================================")


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        run_self_test()
        sys.exit(0)

    port = None
    if len(sys.argv) > 1 and sys.argv[1].isdigit():
        port = int(sys.argv[1])
    else:
        port = find_active_port((80, 8080))

    if port:
        run_live_tests(port)
    else:
        print("==================================================================")
        print("  Virtual display screen is not currently running.")
        print("==================================================================")
        print()
        print("To start the virtual display screen:")
        print("   1. Open a PowerShell terminal and run:")
        print("      cd C:\\petra-system\\tools\\virtual-display")
        print("      python run_virtual_display.py")
        print("      (or double-click start_virtual_display.bat)")
        print()
        print("   2. Once the window is open, you can run this test in another terminal:")
        print("      python test_display.py")
        print()
        print("Running offline self-test in the meantime...")
        print("------------------------------------------------------------------")
        run_self_test()
