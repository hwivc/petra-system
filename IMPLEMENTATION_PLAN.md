# Petra: ESP-IDF Implementation Plan

Companion to `PLAN.md`, last updated 2026-10-11. `PLAN.md` says *what* to build. This file says *how* to build it with ESP-IDF and the Xiaozhi firmware.

## 0. Current state

| Item | Status |
|---|---|
| Project folder | `C:\petra-system` (no spaces; ESP-IDF needs this) |
| Xiaozhi firmware | cloned to `firmware/petra`, branch `petra` at tag **v2.5.0**; remote is `upstream` |
| VS Code ESP-IDF extension | installed (v2.3.0) |
| ESP-IDF framework | **not installed yet**. Xiaozhi v2.5.0 needs **ESP-IDF >= 6.0.1, 6.1 recommended** (5.x is not supported) |
| PCB | built and tested with `tools/pcb-test` (OLED eyes + speaker OK) |

## 1. Final pin map (from the PCB test plus the Seeed wiki)

| Function | GPIO | XIAO pin | Notes |
|---|---|---|---|
| Speaker BCLK (MAX98357A) | 1 | D0 | |
| Speaker LRC/WS | 2 | D1 | |
| Speaker DIN | 3 | D2 | |
| OLED SDA (SSD1306 128x64) | 5 | D4 | I2C port 0, address 0x3C |
| OLED SCL | 6 | D5 | |
| PDM mic CLK | 42 | internal | |
| PDM mic DATA | 41 | internal | |
| BOOT button (push-to-talk) | 0 | internal | |
| User LED (orange, active low) | 21 | internal | shared with the SD card CS; we don't use the SD card |
| External PTT button (optional) | 4 | D3 | to GND |
| Camera OV2640 | XCLK 10, SIOD 40, SIOC 39, D7..D0 = 48,11,12,14,16,18,17,15, VSYNC 38, HREF 47, PCLK 13, PWDN/RESET NC | internal | **re-check against the Seeed wiki in Task 1.2** |

No conflicts: the camera (10-18, 38-40, 47, 48), the mic (41, 42), the speaker (1-3) and the OLED (5, 6) are all separate.
The camera's SCCB uses I2C port 1, and the OLED uses port 0.

RGB LED: it's hard-wired to power and always on, so the firmware does not need it. Petra's state (listening, speaking) shows on the OLED and the orange user LED instead. The only cost is a few mA of battery, which is worth measuring in Task 1.7.

## 2. What the research found in Xiaozhi v2.5.0

| Need | What exists | Use |
|---|---|---|
| XIAO S3 board | **none** | create `main/boards/petra-xiao-s3/` |
| PDM mic + I2S amp codec | `NoAudioCodecSimplexPdm` (`main/audio/codecs/no_audio_codec.h`) | reuse, no new codec needed |
| Closest reference board | `main/boards/dfrobot/df-s3-ai-cam/` (S3 + PDM mic + I2S speaker + DVP camera + LED) | copy as our starting point |
| SSD1306 OLED | `main/boards/bread-compact-wifi/` (`OledDisplay`, `esp_lcd_new_panel_ssd1306`) | copy the display init from it |
| Camera driver | `EspVideo` (`boards/common/esp_video.cc`) or `Esp32Camera` (`boards/common/esp32_camera.cc`), both built for S3 | start with `EspVideo` like df-s3-ai-cam; fall back to `Esp32Camera` if OV2640 detection fails |
| MCP tools on the board | `McpServer::GetInstance().AddTool(name, desc, props, callback)` (`main/mcp_server.h:434`, `docs/mcp-usage.md`) | Objective 3A |
| Board registration | `main/Kconfig.projbuild` (`choice BOARD_TYPE`) + `main/CMakeLists.txt` (`elseif(CONFIG_BOARD_TYPE_...)`) | 2 small edits outside our folder |
| Flash/partitions | project default is 16 MB, and the XIAO S3 has **8 MB** | `config.json`: `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`, `main/boards/petra-xiao-s3/partitions.csv` (3.5 MB factory app + 4.4 MB assets for "Hey Petra"), `CONFIG_SPIRAM_MODE_OCT=y` |

## 3. Step-by-step

### Step A: Install ESP-IDF 6.1 (you, about 20 min)
1. VS Code -> `Ctrl+Shift+P` -> **ESP-IDF: Open ESP-IDF Install Manager** (or "Configure ESP-IDF Extension").
2. Choose **Express**, version **v6.1** (or the latest 6.x), install path `C:\esp` (short, no spaces).
3. When it finishes, check it in the **ESP-IDF 6.1 CMD/PowerShell** terminal: `idf.py --version`.

### Step B: Toolchain smoke test (PLAN 1.1)
1. VS Code -> **File -> Open Folder** -> `C:\petra-system\firmware\petra`.
2. Set the target to `esp32s3` (status bar chip icon, or `idf.py set-target esp32s3`).
3. `idf.py menuconfig` -> *Xiaozhi Assistant -> Board Type* -> **DFRobot ESP32-S3 AI Camera Module** (an unchanged board).
4. `idf.py build`. Don't flash it. This only proves the toolchain works. The first build downloads managed components and takes 10-20 min.

### Step C: Create the `petra-xiao-s3` board (PLAN 1.3)
Files in `firmware/petra/main/boards/petra-xiao-s3/`:
- `config.h`: pin map from section 1, `AUDIO_INPUT_SAMPLE_RATE 16000`, `AUDIO_OUTPUT_SAMPLE_RATE 24000`, `DISPLAY_WIDTH 128`, `DISPLAY_HEIGHT 64`, camera pins.
- `config.json`: `"type": "petra-xiao-s3"`, target `esp32s3`, sdkconfig_append: 8 MB flash, `main/boards/petra-xiao-s3/partitions.csv` (3.5 MB factory app + 4.4 MB assets for "Hey Petra"), octal PSRAM, `CONFIG_OLED_SSD1306_128X64=y`, OV2640 DVP options (copy the style from df-s3-ai-cam, but enable OV2640).
- `petra_xiao_s3_board.cc`: `class PetraXiaoS3 : public WifiBoard`
  - `InitializeI2c()` + `InitializeSsd1306Display()`, copied from bread-compact-wifi. **Must not crash if the OLED is missing**: the PCB test needed ~5 s after power-up before the OLED answered. Probe the address first, retry for up to ~6 s, and fall back to `NoDisplay` instead of `ESP_ERROR_CHECK` aborting (otherwise Petra boot-loops).
  - `InitializeButtons()`: BOOT is a click to toggle chat, or enter Wi-Fi config while starting. GPIO4 is an optional external button.
  - `InitializeCamera()`: copied from df-s3-ai-cam with the XIAO pins.
  - `GetAudioCodec()`: `NoAudioCodecSimplexPdm(16000, 24000, GPIO1, GPIO2, GPIO3, GPIO42, GPIO41)`.
  - `GetLed()`: `GpioLed(GPIO_NUM_21, 1)` (active low, verify on the board).
  - `DECLARE_BOARD(PetraXiaoS3)`.
- `README.md`: wiring and menuconfig path.
- Registration: add `config BOARD_TYPE_PETRA_XIAO_S3` to `main/Kconfig.projbuild`, and add an `elseif(CONFIG_BOARD_TYPE_PETRA_XIAO_S3)` branch with `BOARD_DIR "petra-xiao-s3"` and a small OLED font (copy the bread-compact-wifi 128x64 font settings) to `main/CMakeLists.txt`.
- Build: `python scripts/build.py petra-xiao-s3` (applies config.json) or menuconfig -> Board Type -> Petra.

### Step D: Flash and connect (PLAN 1.4)
1. **Ask before flashing.** Plug in Petra, find the COM port (Device Manager), then run `idf.py -p COMx flash monitor`.
   If it won't enter download mode, hold BOOT, tap RESET, then release BOOT.
2. Petra opens the hotspot `Xiaozhi-XXXX` -> connect from your phone -> enter the Wi-Fi details.
3. Petra speaks a 6-digit code -> bind it at **xiaozhi.me** -> Console -> Add device.

### Step E: Configure on xiaozhi.me (PLAN 1.5)
Name **Petra**, English and French, voice, and the prompt (saved in `docs/petra-prompt.md`).

### Step F: Tuning and power (PLAN 1.6, 1.7)
Output volume from the Xiaozhi volume setting. If the mic is quiet, add a PDM gain (do not edit the shared codec, subclass it in the board folder instead). Measure battery runtime, including the always-on RGB LED.

### Later: OLED "eyes" (optional, after Objective 1 passes)
Xiaozhi's `OledDisplay` shows text and emotion icons, not your animated eyes. Port the eye drawing from `tools/pcb-test/src/main.cpp` into a `PetraEyesDisplay` (subclass of `OledDisplay` in the board folder) that maps Xiaozhi's `SetEmotion("happy" | "sad" | ...)` to the eye moods. Keep this in the board folder.

## 4. Risks specific to this setup

| Risk | Mitigation |
|---|---|
| OLED not ready at boot leads to an `ESP_ERROR_CHECK` abort and a boot loop | probe and retry, fall back to `NoDisplay` (Step C) |
| Camera (DVP) + Wi-Fi + AFE use too much memory | octal PSRAM enabled; frame buffers in PSRAM; watch the heap log |
| Windows path length (managed_components are deep) | project is at `C:\petra-system`; if it still fails, enable Windows long paths (`git config --global core.longpaths true` + registry LongPathsEnabled) |
| OTA overwrites our firmware with a stock board | unique `type` `petra-xiao-s3` (never reuse an existing board name) |
| Upstream changes | all code in our board folder plus 2 registration lines; branch `petra` rebases onto new tags |
