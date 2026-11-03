# Petra System

Voice assistant **Petra** (XIAO ESP32S3 Sense + Xiaozhi firmware) plus independent MCP devices (ePaper first).

- `PLAN.md`: goals, objectives and acceptance tests
- `IMPLEMENTATION_PLAN.md`: step-by-step ESP-IDF implementation plan
- `docs/troubleshooting.md`: firmware research notes and known issues
- `docs/petra-prompt.md`: personality prompt to paste on xiaozhi.me
- **Section 8 below: what we learned (quick fixes)**

---

## 1. Where everything is

| What | Folder |
|---|---|
| Project root | `C:\petra-system` |
| Xiaozhi firmware (ESP-IDF project, **build from here**) | `C:\petra-system\firmware\petra` |
| Our custom board (all Petra code) | `C:\petra-system\firmware\petra\main\boards\petra-xiao-s3` |
| Build output (`.bin` files) | `C:\petra-system\firmware\petra\build` |
| PCB test (Arduino / PlatformIO) | `C:\petra-system\tools\pcb-test` |
| Docs | `C:\petra-system\docs` |
| ESP-IDF v6.1 (after install) | `C:\esp\v6.1\esp-idf` |
| ESP-IDF tools + activation scripts | `C:\Espressif\tools` |

> Keep every path **without spaces**: ESP-IDF does not support them.

---

## 2. Install ESP-IDF v6.1 from the command line (one time)

Xiaozhi v2.5.0 needs **ESP-IDF v6.0.1 or newer, and v6.1 is recommended**. Version 5.x does not work.
These steps use Espressif's **EIM** (ESP-IDF Installation Manager) command-line tool.

Open **PowerShell** (normal user, not admin) and run:

```powershell
# 1. Install the EIM command-line tool
winget install Espressif.EIM-CLI
#    (or use the copy already downloaded at C:\esp\eim\eim.exe instead of "eim" below)

# 2. Close and reopen PowerShell so "eim" is on the PATH, then check it
eim --version

# 3. Install ESP-IDF v6.1 for the ESP32-S3 into C:\esp
#    (about 2-3 GB download, 20-40 min; -a true installs missing prerequisites)
eim install -i v6.1 -t esp32s3 -p C:\esp -n true -a true

# 4. Check what was installed
eim list
```

If PowerShell refuses to run scripts later (`running scripts is disabled on this system`), run this once:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

---

## 3. Open an ESP-IDF terminal (every time)

`idf.py` only works in a terminal where ESP-IDF is **activated**. Use either of these:

- Double-click the desktop icon **IDF_PowerShell** created by EIM, **or**
- In any PowerShell window, run:

```powershell
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
idf.py --version          # should print ESP-IDF v6.1
```

(The script name follows `Microsoft.<version>.PowerShell_profile.ps1`. If the name is different, run `ls C:\Espressif\tools\*.ps1` to find it.)

---

## 4. Build Petra

In the ESP-IDF terminal:

```powershell
cd C:\petra-system\firmware\petra

# Option A (recommended): build script, applies main\boards\petra-xiao-s3\config.json
#   (8 MB flash, custom partitions.csv, OV2640/OV3660 camera).
#   --language sets the device's built-in prompts (en-US or fr-FR);
#   the conversation language itself is set on xiaozhi.me.
#   Wake word "Hey Petra" is set in config.json (custom wake word, English MultiNet mn7_en).
python scripts\build.py petra-xiao-s3 --language en-US

# Fallback if "Hey Petra" is unreliable: a pre-trained wake word ("Hi ESP")
python scripts\build.py petra-xiao-s3 --language en-US --wake-word wn9_hiesp

# List every pre-trained wake word:
python scripts\build.py --list-wake-words

# Option B: by hand
idf.py set-target esp32s3
idf.py menuconfig
#   Xiaozhi Assistant -> Target Board -> "Petra (PlastiBytes, XIAO ESP32S3 Sense)"
#   Serial flasher config -> Flash size -> 8 MB
#   Partition Table -> Custom partition CSV file -> main/boards/petra-xiao-s3/partitions.csv
idf.py build
```

The first build downloads the managed components and takes 10-20 minutes.

To test the toolchain on an unchanged board first (no flashing needed): `python scripts\build.py dfrobot/df-s3-ai-cam`.

---

## 5. Flash and monitor

1. Plug Petra in over USB-C and find the port in **Device Manager → Ports (COM & LPT)**, for example `COM15`.
2. In the ESP-IDF terminal:

```powershell
cd C:\petra-system\firmware\petra
idf.py -p COM15 flash monitor      # Ctrl+] to leave the monitor
```

If flashing will not start: hold **BOOT**, tap **RESET**, release **BOOT**, then run it again.

3. First boot: Petra opens a Wi-Fi hotspot `Xiaozhi-XXXX`. Connect your phone to it and enter your Wi-Fi details.
4. Petra speaks a 6-digit code. Add the device at **xiaozhi.me** → Console with that code.

---

## 6. Using VS Code instead (optional)

1. **File → Open Folder** → `C:\petra-system\firmware\petra` (open this folder, not the project root).
2. `Ctrl+Shift+P` → **ESP-IDF: Configure ESP-IDF Extension** → choose the existing install at `C:\esp\v6.1\esp-idf`.
3. Use the status-bar buttons for target (esp32s3), port, build, flash and monitor.

---

## 7. Updating Xiaozhi from upstream

The firmware is a git clone with remote `upstream` (`github.com/78/xiaozhi-esp32`), on branch `petra` based on tag `v2.5.0`.
Our changes are only `main/boards/petra-xiao-s3/` plus a few lines in `main/Kconfig.projbuild` and `main/CMakeLists.txt`.

```powershell
cd C:\petra-system\firmware\petra
git fetch upstream --tags
git rebase <new-tag>
```

---

## 8. What we learned (quick reference)

| Problem | Cause | Fix |
|---|---|---|
| ESP-IDF build fails | Path had a space (`PETra Sytems`) | Project moved to `C:\petra-system` |
| Wi-Fi not found / weak (-95 dBm) | Antenna not connected | **Click the U.FL antenna onto the XIAO** |
| Upload: "COM port busy / not found" | Another serial monitor app is open, or board boot-looping | Close the other app; hold **BOOT** while plugging in, flash, press **RESET** |
| "waiting for download" after flash | Board still in download mode | Press **RESET** |
| Firmware too big | "Hey Petra" model adds ~0.6 MB code + 2.7 MB model | `partitions.csv`: 3.5 MB app + 4.4 MB assets, **no OTA** (update via USB only) |
| OLED blank / white / frozen | OLED needs ~5.5 s after power-up; Wi-Fi current spikes reset it; I2C waited forever | Board waits 5.5 s, I2C 100 kHz + 250 ms timeout, `oled_io_guard` retries and re-inits OLED |
| Crash loop after Wi-Fi connects | Font loaded while OLED (LVGL) was off | `lv_init()` before falling back to no display |
| Camera "get sensor ID failed" | Board has an **OV3660**, not OV2640 | Both enabled in `config.json` |
| Speaker quiet | Audio only on left channel; volume 70 squared | Both channels, volume 100, +2.5x gain (`AUDIO_OUTPUT_GAIN`) |
| Petra sarcastic ("dear", "how boring") | xiaozhi.me default voice template | Replace with `docs/petra-prompt.md` |

**Using Petra**
- Wake: say **"Hey Petra"** (or press BOOT).
- Stop: say **"stop listening" / "goodbye"** (tool `self.conversation.end`) or press BOOT.
- First setup: OLED shows a 6-digit code -> xiaozhi.me -> Console -> Agents -> Add device.
- Volume: "set the volume to 80". Too loud/quiet overall: change `AUDIO_OUTPUT_GAIN` in `config.h`.

**Hardware to-do (makes the OLED reliable)**
- 4.7 kΩ pull-ups: SDA (D4) and SCL (D5) to 3.3 V.
- 10-100 µF capacitor on the OLED VCC/GND.
- Optional louder speaker: MAX98357A GAIN pin to GND (+12 dB) or via 100 kΩ to GND (+15 dB).

Details: `docs/troubleshooting.md`.

---

## 9. ePaper panel (works with Petra)

Code: `C:\petra-system\firmware\epaper-panel` (PlatformIO, XIAO ESP32-C3). Protocol: `shared/protocol.md`.

**Setup (one time)**
1. Upload (plain PowerShell, not the ESP-IDF one):
```powershell
cd C:\petra-system\firmware\epaper-panel
~\.platformio\penv\Scripts\pio.exe run -t upload
~\.platformio\penv\Scripts\pio.exe device monitor
```
2. The panel shows **"Display setup"** with a QR code. Scan it (or join Wi-Fi `PlastiBytes-Display-XXXX`).
3. The setup page opens by itself (else open `192.168.4.1`): pick the Wi-Fi (same as Petra), type the password,
   paste the **MCP endpoint** (xiaozhi.me -> Console -> Agents -> Petra -> MCP endpoint). Save -> panel restarts and connects.
4. Paste the updated prompt from `docs/petra-prompt.md` into the Petra agent.

**Change Wi-Fi / MCP key later (new venue)**
- Saved Wi-Fi not found for 30 s -> setup screen opens by itself.
- Or switch the panel on and **press BOOT within 3 seconds** (not while plugging in: that is download mode).
- Nobody joins the setup hotspot for 5 min -> it restarts and retries the saved Wi-Fi.
- Petra: same idea. If her Wi-Fi is not found she opens `Xiaozhi-XXXX` after ~1 min, or press BOOT while she is searching.

**What it does**

| Say to Petra | What happens | Cloud? |
|---|---|---|
| "Take my picture" | 3-2-1 on OLED -> postcard on ePaper (random style + frame) | No |
| "Make me a comic / sketch / woodcut / engraving / stipple" | That art style | No |
| "Put me on a magazine cover" (also: polaroid, hero certificate, wanted poster, comic, film, CITS26 event) | That frame | No |
| "Now do it as a comic" (within 3 min) | Same photo, new style, no new picture | No |
| Scan the small QR on the postcard | Phone joins `PlastiBytes-Photo-XXXX`, page opens, Download | No |
| "Can this be recycled?" (show an item) | Petra looks, ePaper shows YES/NO card | Yes (photo analysed) |
| "How do you make filament?" | Numbered steps on ePaper while Petra explains | Text only |
| "How do I join the mentorship?" | QR code to plastibytes.com/mentorship | Text only |
| "Write ... on the display" | Text on ePaper | Text only |

- Idle: rotates every 90 s: welcome -> mentorship QR -> website QR -> CITS26 -> HWIVC QR -> WhatsApp community QR.
- Event name on postcards: set on the panel's setup page (default "CITS26 - Yaounde").
- Knowledge for Petra: `docs/knowledge-base.md` (upload to the agent's Knowledge Base; fill in the HWIVC part).
- Logos: `tools/make_logos.py` regenerates `epaper-panel/src/logos.h` from the website logos.
- ePaper goes back to the welcome screen after 2 min. Petra and ePaper work without each other.
- Petra finds the panel as `epaper.local` (mDNS). Both must be on the **same Wi-Fi**.
- Camera image upside down / mirrored: `PHOTO_MIRROR` in `config.h` (Petra).

## 10. Smart bulb (works with Petra)

`firmware/bulb`: XIAO ESP32-C3, presence sensor OUT -> **D2**, relay IN -> **D3**, relay switches a mains lamp
(on/off, no dimming). Build/upload like the panel: `pio run -t upload` in `firmware/bulb`.

**Setup (same as the panel):** first power-up, or no remembered Wi-Fi found within 30 s, or BOOT held 5 s:
the lamp blinks twice and opens hotspot `PlastiBytes-Bulb-XXXX`. Join it, the page opens: Wi-Fi, MCP
endpoint (same Petra key as the panel), lamp name, relay type, how long a voice command lasts.

**How it behaves**

| Event | Lamp |
|---|---|
| Sensor output HIGH | On (AUTO, same as the original sketch) |
| Sensor output LOW | Off (AUTO; on-time = the sensor's own time knob) |
| "Petra, turn off the stand light" | Off now, sensor ignored while people are there |
| "Petra, turn on the stand light" | On now, sensor ignored while people are there |
| Nobody detected for 30 min after a voice command | Back to AUTO by itself (set on the setup page) |
| "Put the light back on auto" | AUTO now |
| BOOT short press | Toggle (same as a voice command) |

- The lamp works on its sensor without Wi-Fi or Petra.
- Relay held OFF from boot; logs only on change.
- Several bulbs: give each a different name; each adds its own tool (`light_<name>`) to Petra.

**Moving to a new place:** the panel and the bulb remember the **last 5 Wi-Fi networks** and join the
strongest one they can see. A new place opens the setup hotspot by itself (no remembered network
found in 30 s); going back to a known place needs nothing. Petra (Xiaozhi) does the same with its own
`Xiaozhi-XXXX` hotspot.

Shared code: `firmware/shared-libs` (WifiStore, xiaozhi-mcp), used by the panel and the bulb.
