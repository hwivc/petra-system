# Petra System — Build Plan for Claude Code

**Owner:** PlastiBytes / HIVC
**Goal:** A voice assistant called **Petra** that talks to visitors and controls other independent devices (ePaper display first, more later) through the Xiaozhi cloud's **MCP endpoint**. Must be demo-ready for a hardware exhibition.

---

## How to use this plan (instructions for Claude Code)

- Work **one objective at a time**, in order. Do not start Objective 2 until Objective 1's acceptance tests pass.
- Inside each objective, work task by task. After each task: build, fix errors, then tick the box in this file.
- **Never guess pin numbers or library APIs.** Check the Seeed wiki, the library's README/examples, or the existing source code first. If something is unclear, stop and ask.
- **Never commit secrets.** Wi-Fi passwords and the MCP endpoint token go in `secrets.h` / `.env` files that are in `.gitignore`. Provide a `secrets.example.h` / `.env.example` instead.
- Keep each device **independent**: no device may depend on another device to boot or work. Devices only talk to each other over local Wi-Fi for data transfers (e.g., photos), and must handle the other device being offline.
- Ask the user before flashing a board (they need to plug it in and confirm the port).

---

## System overview

```
                    ┌──────────────────────────┐
                    │ xiaozhi.me cloud (AI)     │
                    │  + MCP endpoint (wss://)  │
                    └───────▲─────────▲────────┘
            voice (Xiaozhi) │         │ MCP tools (WebSocket)
                            │         │
        ┌───────────────────┴──┐   ┌──┴──────────────────────┐
        │ PETRA                │   │ ePAPER PANEL            │
        │ XIAO ESP32S3 Sense   │   │ Seeed XIAO 7.5" ePaper  │
        │ mic + speaker +      │   │ Arduino + MCP library   │
        │ CAMERA (OV2640)      │   │ (PlatformIO)            │
        │ Xiaozhi firmware     │   │                         │
        │ (ESP-IDF)            │   │                         │
        └──────────┬───────────┘   └──▲──────────────────────┘
                   │  HTTP POST /photo │
                   └───(local Wi-Fi)───┘
```

**Approach chosen:**
- **Petra** runs Xiaozhi firmware with a custom *board config* (pins), and uses its **own on-board camera**. In Objective 3 we add **one custom MCP tool inside Petra's firmware** (`self.photo.send_to_display`) that takes a photo and sends it to the ePaper over local Wi-Fi.
- **Other devices** (ePaper now, more later) are separate and register their own **MCP tools** on the xiaozhi.me endpoint with the Arduino MCP library.

---

## Repository layout

```
petra-system/
├── PLAN.md                     ← this file
├── README.md                   ← how to set up, build, flash each device
├── .gitignore                  ← secrets.h, .env, build/, .pio/
├── firmware/
│   ├── petra/                  ← fork/clone of github.com/78/xiaozhi-esp32 (ESP-IDF)
│   │   └── main/boards/petra-xiao-s3/   ← our custom board
│   └── epaper-panel/           ← PlatformIO project
├── tools/
│   └── pcb-test/               ← Arduino/PlatformIO PCB test (OLED eyes + speaker)
├── shared/
│   └── protocol.md             ← local HTTP API between devices
└── docs/
    ├── wiring.md               ← wiring tables + photos
    ├── demo-script.md          ← tested voice commands for the exhibition
    └── troubleshooting.md
```

---

## Hardware

| Device | Board | Parts |
|---|---|---|
| Petra | Seeed XIAO ESP32S3 Sense (8 MB PSRAM, OV2640 camera, PDM mic) | MAX98357A I2S amp, 4–8 Ω speaker, 1000 mAh LiPo, slide switch, (optional) push button |
| ePaper panel | Seeed XIAO 7.5" ePaper Panel (800×480, black & white) | — (verify which XIAO is on it, likely ESP32-C3) |

### Petra wiring

> **Updated 2026-10-11 to match the built PCB** (verified with `tools/pcb-test/src/main.cpp`). The speaker moved from D1–D3 to D0–D2, and an OLED was added.

| MAX98357A | XIAO ESP32S3 |
|---|---|
| BCLK | D0 (GPIO1) |
| LRC / WS | D1 (GPIO2) |
| DIN | D2 (GPIO3) |
| VIN | Battery + after switch (2.5–5.5 V) |
| GND | GND |
| SD (shutdown) | leave unconnected (or tie high) |

| OLED SSD1306 128×64 (I2C) | XIAO ESP32S3 |
|---|---|
| SDA | D4 (GPIO5) |
| SCL | D5 (GPIO6) |
| Address | 0x3C (auto-detected in the PCB test; 0x3D possible) |

- Mic: on-board **PDM** mic: CLK = GPIO42, DATA = GPIO41 (mono, 16-bit only).
- Camera: on-board OV2640 on the Sense board (internal pins, no wiring). Take the exact camera pin map from the Seeed wiki and confirm none of it overlaps GPIO1/2/3, GPIO5/6 or the mic.
- **Talking: wake word for now** (decided 2026-10-11); BOOT (GPIO0) also starts/stops a chat. Push-to-talk button kept for later: D0 is now the speaker, so the optional external button goes on **D3 (GPIO4)** to GND.
- RGB LED: hard-wired so it lights whenever the system is on. The firmware does not control it.
- Do **not** use D8–D10 (SD card) or camera pins.
- Battery on the BAT pads under the XIAO; switch on the + wire.

---

## OBJECTIVE 1: Build Petra (she talks and converses)

**Outcome:** Press the button, speak in English or French, and Petra answers out loud through the speaker using the xiaozhi.me cloud.

### Tasks

- [x] **1.1 Set up the toolchain** _(ESP-IDF 6.1, Xiaozhi v2.5.0 on branch `petra`)_
  - Install ESP-IDF (the version required in the xiaozhi-esp32 README: **v6.0.1 minimum, v6.1 recommended**; 5.x is not supported) and the VS Code ESP-IDF extension. The project path must not contain spaces.
  - Clone `github.com/78/xiaozhi-esp32` into `firmware/petra/` (as a fork or git submodule so upstream updates can be pulled).
  - Build one existing board config unchanged to confirm the toolchain works.

- [x] **1.2 Research before coding** _(see `docs/troubleshooting.md`)_
  - Check whether `main/boards/` already has a XIAO ESP32S3 / XIAO Sense board. If yes, base our board on it.
  - Read `docs/custom-board.md` in the repo.
  - Find whether the codebase already has a **PDM simplex audio codec** class (search for "Pdm"). Reuse it if it exists; otherwise write a minimal one modelled on the existing simplex I2S codec.
  - Find how existing boards with cameras set up the camera (search for "camera" / `esp32_camera`) and how they register board-specific MCP tools (search for `McpServer` / `AddTool`). We will use both patterns.
  - Write findings into `docs/troubleshooting.md` under "Petra firmware notes".

- [x] **1.3 Create the custom board `petra-xiao-s3`** _(builds OK 2026-10-11; not yet tested on hardware)_
  - `config.h`: PDM mic pins (41/42), I2S speaker pins (GPIO1/2/3), sample rates, button GPIO0, built-in LED GPIO21, SSD1306 128×64 OLED on I2C GPIO5/6.
  - `config.json`: target esp32s3, 8 MB flash + 8 MB octal PSRAM (verify against the Seeed datasheet).
  - `petra_board.cc`: board class wiring the codec, button (press to talk), LED status, and **camera initialisation** (OV2640, frame buffers in PSRAM) following the pattern of existing camera boards.
  - Register the board in the build system (Kconfig / CMake) the same way other boards are registered.

- [ ] **1.4 Build, flash, and connect**
  - `idf.py set-target esp32s3` → `idf.py menuconfig` (select petra-xiao-s3) → `idf.py build flash monitor`.
  - Connect Petra to Wi-Fi using Xiaozhi's provisioning flow.
  - Bind the device on xiaozhi.me with the code it speaks.

- [ ] **1.5 Configure Petra on xiaozhi.me**
  - Name: **Petra**. Languages: English and French.
  - Write a personality prompt (save a copy in `docs/petra-prompt.md`): friendly assistant from PlastiBytes (Cameroon) that turns plastic waste into 3D-printing filament for STEM education; can introduce PlastiBytes in about 20 seconds; short spoken answers.
  - Choose a clear voice for English and French.

- [ ] **1.6 Audio tuning**
  - Tune mic gain and speaker volume; test in a noisy room (play crowd noise from a phone).
  - Confirm no clipping or distortion on both 4 Ω and 8 Ω speakers.

- [ ] **1.7 Power**
  - Confirm battery charging over USB-C and that the switch fully powers off Petra.
  - Measure battery runtime during continuous conversation and record it in `docs/`.

### Acceptance tests
- [ ] 10 conversations in a row without a crash or reboot.
- [ ] Petra understands and answers in English **and** French.
- [ ] "Petra, what is PlastiBytes?" gives a correct, short answer.
- [ ] Works on battery and on USB power.
- [ ] Camera initialises at boot without breaking audio (log shows camera OK; if Xiaozhi's built-in camera tool is available, "Petra, what do you see?" works).
- [ ] Wiring and build steps documented in `README.md` and `docs/wiring.md`.

---

## OBJECTIVE 2: Add the ePaper project

**Outcome:** "Petra, write *Welcome to PlastiBytes* on the display" → the ePaper shows the message. The panel works fully on its own and connects directly to the MCP endpoint.

### Tasks

- [ ] **2.1 Research before coding**
  - Confirm which XIAO board is on the 7.5" ePaper Panel and its pins (Seeed wiki).
  - Confirm the right GxEPD2 driver class for this panel.
  - Read the `xiaozhi-esp32-mcp` library README and examples (github.com/toddpan/xiaozhi-esp32-mcp). Pin a specific commit in `platformio.ini`. Note its exact API in `docs/troubleshooting.md`.
  - Check the xiaozhi.me limits for MCP endpoint connections and tool count.

- [ ] **2.2 Create the PlatformIO project `firmware/epaper-panel/`**
  - `platformio.ini` with the correct board, `framework = arduino`, and libs: xiaozhi-esp32-mcp (pinned), ArduinoJson, GxEPD2.
  - `include/secrets.example.h` with `WIFI_SSID`, `WIFI_PASS`, `MCP_ENDPOINT`.

- [ ] **2.3 Display layer (`display.cpp/.h`)**
  - `showMessage(text)`: word-wrapped, large readable font, centred; long text shrinks or truncates cleanly.
  - `showPhoto(bitmap, caption)`: 800×400 1-bit image on top, caption in the bottom 80 px.
  - `showIdle()`: PlastiBytes logo or name + "Ask Petra!" + Wi-Fi status.
  - Full refresh every N updates to avoid ghosting.

- [ ] **2.4 MCP tools**
  - `show_message(text)`: "Show a short text message on the black-and-white ePaper display."
  - `clear_display()`: "Clear the ePaper display and show the PlastiBytes idle screen."
  - Tool descriptions must be clear: they are how the AI decides which tool to call.

- [ ] **2.5 Local HTTP server (for Objective 3)**
  - Set up mDNS as `epaper.local`.
  - `POST /photo` exactly as defined in `shared/protocol.md` (write that file now).
  - `GET /status` → JSON `{ "ok": true, "busy": false }`.
  - Reject the photo if the display is busy refreshing (HTTP 503).

- [ ] **2.6 Robustness**
  - Auto-reconnect Wi-Fi and the MCP WebSocket.
  - The panel shows an error screen if Wi-Fi is lost for more than 60 s.

### `shared/protocol.md` (to create)
- `POST http://epaper.local/photo`
  - Body: raw 1-bit bitmap, 800×400, MSB-first, 1 = black (40,000 bytes).
  - Header `X-Caption`: URL-encoded UTF-8 caption (max 80 chars).
  - Responses: `200 ok`, `400` bad size, `503` busy.
- `GET http://epaper.local/status`

### Acceptance tests
- [ ] Voice command to Petra changes the ePaper text within ~10 s.
- [ ] Panel recovers by itself after Wi-Fi is switched off and on.
- [ ] Panel works even when Petra is switched off (the tool is still listed on xiaozhi.me).
- [ ] A test script (`tools/send_test_photo.py`) can POST a sample image to `/photo` and it displays correctly.

---

## OBJECTIVE 3: Add other functions to the ePaper and Petra system

**Outcome:** "Petra, take a picture and put it on the display with *Welcome to PlastiBytes*" works, plus more demo features for the exhibition.

### 3A: Petra's camera → ePaper (custom tool inside Petra's firmware)

All of this lives in `firmware/petra/main/boards/petra-xiao-s3/`, so upstream Xiaozhi code stays untouched and easy to update.

- [ ] **3A.1** Create `photo_to_display.cc/.h` in the board folder for the image pipeline: capture (grayscale or JPEG, e.g., VGA) → decode in PSRAM → resize/crop to 800×400 → contrast stretch → **Floyd–Steinberg dither** → pack to 1-bit (format from `shared/protocol.md`).
- [ ] **3A.2** HTTP client: resolve `epaper.local` (mDNS), `GET /status` first, then `POST /photo` with the bitmap and `X-Caption` header. Timeouts of a few seconds; never block audio for long.
- [ ] **3A.3** Register the MCP tool in `petra_board.cc` with `McpServer::GetInstance().AddTool(...)` (use the exact API found in Task 1.2):
  - Name: `self.photo.send_to_display`
  - Description: "Take a photo with Petra's camera and show it on the ePaper display with a caption underneath."
  - Parameter: `caption` (string, max 80 chars)
  - Returns: "Photo sent to the display" / "The display is offline" / "The display is busy, try again in a few seconds".
- [ ] **3A.4** Countdown before capture: LED blinks 3 times; the tool returns a message so Petra can confirm out loud.
- [ ] **3A.5** Make sure the photo pipeline runs in its own task and frees all PSRAM buffers afterwards (check free heap/PSRAM in logs before and after 20 photos).
- [ ] **3A.6** Test with good light and with poor light; adjust camera settings (exposure, brightness) and contrast before dithering.

### 3B: More ePaper functions (pick by priority)

- [ ] `show_qr(text_or_url)`: show a QR code (e.g., plastibytes.com, sign-up form).
- [ ] `show_big_number(label, value)`: e.g., "Bottles recycled today: 120".
- [ ] `show_list(title, items)`: short bullet list (e.g., today's workshop agenda).
- [ ] Rotating idle screen: PlastiBytes facts every few minutes.

### 3C: Petra improvements

- [ ] External push-to-talk button (D3 / GPIO4) for noisy halls.
- [ ] Prompt update so Petra knows about the display tools and offers them ("Want me to put that on the display?").
- [ ] Bilingual demo phrases tested and written to `docs/demo-script.md`.

### 3D: Exhibition readiness

- [ ] Use a travel router or 4G hotspot with fixed SSID/password for all devices.
- [ ] Prepare backup boards flashed with the same firmware.
- [ ] Record a fallback demo video.
- [ ] Add a camera consent sign and confirm photos are **not stored** on any device.
- [ ] Prepare a second xiaozhi.me account in case of free-tier limits.
- [ ] Make enclosures 3D-printed with PlastiBytes recycled filament.
- [ ] Keep the MCP endpoint token off any screen or poster.

### Acceptance tests
- [ ] Photo + caption appears on the ePaper within ~20 s of the voice command.
- [ ] 20 photo commands in a row without a crash.
- [ ] Every device works when any other device is switched off (with a polite error message from Petra).
- [ ] Full demo script runs end-to-end 3 times in a noisy room.

---

## Known risks

| Risk | Mitigation |
|---|---|
| PDM mic not supported out of the box in Xiaozhi | Reuse or write a PDM simplex codec (Task 1.2) |
| Camera + audio + Wi-Fi together run out of memory on Petra | Frame buffers and image work in PSRAM, free buffers after each photo, check heap in logs (3A.5) |
| Custom code inside the Xiaozhi fork breaks on upstream updates | Keep all custom code in our board folder; pin the Xiaozhi version |
| `xiaozhi-esp32-mcp` library is alpha | Pin a commit; wrap it in our own small module so it can be swapped |
| xiaozhi.me connection or tool limits | Check limits in Task 2.1; split across agents if needed |
| Venue Wi-Fi / no internet | Own 4G router (3D) |
| ePaper ghosting or slow refresh | Periodic full refresh; tell visitors it is normal |
| Data on Chinese servers | Fine for a demo; for a product, consider self-hosting `xiaozhi-esp32-server` |
