# Troubleshooting

## Petra firmware notes (Task 1.2, Xiaozhi v2.5.0)

- **ESP-IDF:** v6.0.1 minimum, v6.1 recommended (installed at `C:\esp\v6.1`). 5.x is not supported. The project path must not contain spaces.
- **XIAO board in Xiaozhi:** none upstream. Ours is `main/boards/petra-xiao-s3/`, based on `dfrobot/df-s3-ai-cam` (PDM + camera) and `bread-compact-wifi` (SSD1306).
- **PDM codec:** `NoAudioCodecSimplexPdm` in `main/audio/codecs/no_audio_codec.h` (speaker BCLK/WS/DOUT + mic SCK/DIN). Reused unchanged.
- **Camera:** `EspVideo` (`main/boards/common/esp_video.h`) with a DVP config. The OV2640 is enabled through `config.json` (`CONFIG_CAMERA_OV2640*`), and SCCB runs on I2C port 1. Mirror and flip are set in menuconfig: Xiaozhi Assistant → Camera → Override camera mirror settings. When `GetCamera()` returns a camera, Xiaozhi registers its own camera MCP tool (`main/mcp_server.cc`).
- **MCP tools:** `McpServer::GetInstance().AddTool(name, description, PropertyList, callback)` (`main/mcp_server.h`, `docs/mcp-usage.md`).
- **Registration:** `main/Kconfig.projbuild` (`BOARD_TYPE_PETRA_XIAO_S3`) + `main/CMakeLists.txt` (`BOARD_DIR "petra-xiao-s3"`). These are the only upstream files we touch.
- **Flash:** Xiaozhi defaults to 16 MB. The XIAO S3 has 8 MB, so `config.json` sets 8 MB + `partitions/v2/8m.csv`. Octal PSRAM is already the S3 default.
- **Pins** verified against arduino-esp32 `variants/XIAO_ESP32S3/pins_arduino.h` and the CameraWebServer `camera_pins.h` (`CAMERA_MODEL_XIAO_ESP32S3`).

- **Wake word "Hey Petra":** set in the board's `config.json` (`CONFIG_USE_CUSTOM_WAKE_WORD`, `CONFIG_CUSTOM_WAKE_WORD="hey petra"`, `CONFIG_SR_MN_EN_MULTINET7_QUANT`). At build time, `scripts/build_default_assets.py` packs the mn7_en model plus the command into the assets partition. At runtime, `AfeAudioEngine` sees a MultiNet model and uses `CustomWakeWord`.
- **Flash layout:** mn7_en is about 2.7 MB and doesn't fit in the 2 MB assets partition of `partitions/v2/8m.csv`, so the board uses `main/boards/petra-xiao-s3/partitions.csv`: one 3.5 MB `factory` app (the app is 3.1 MB with MultiNet linked in) and a 4.4 MB `assets` partition, with no OTA slots. Firmware updates go over USB only. `Ota::Upgrade` just logs "Failed to get update partition".

## Known issues

- **"Hey Petra" unreliable:** a MultiNet custom word is less accurate than Espressif's pre-trained WakeNet words. Tune `CONFIG_CUSTOM_WAKE_WORD_THRESHOLD` (lower = more sensitive, default 20), or fall back with `--wake-word wn9_hiesp`.
- **Custom wake word disappears:** if xiaozhi.me pushes a new assets package to the device (Console → custom assets), it replaces our assets, including the "Hey Petra" model. Generate any custom assets with the MultiNet English wake word included, or reflash over USB.

- **OLED blank (fixed 2026-10-11):** in the PCB test, the OLED only answered on I2C about 5.4 s after power-up, and the test also tried SDA/SCL swapped. The board code now does the same: it scans 0x3C/0x3D with both pin orders until 12 s after boot (`DISPLAY_PROBE_DEADLINE_MS`), then falls back to no display instead of crashing. Look for `OLED found at 0x3C (SDA=GPIO5 SCL=GPIO6)` in the log.
- **Speaker quiet (fixed 2026-10-11):** Xiaozhi's PDM codec sent audio on the LEFT I2S slot only, and the MAX98357A (SD floating) plays (L+R)/2, which costs 6 dB. On top of that, the default volume of 70 is squared, giving 0.49 amplitude (another 6 dB). The board now uses `I2S_STD_SLOT_BOTH` and a default volume of 100 (`AUDIO_DEFAULT_OUTPUT_VOLUME`, only used when no volume is saved). The PCB test warned that a very loud speaker can disturb the OLED power. If the OLED glitches while Petra talks, lower the volume ("Petra, set the volume to 80").

- **Crash loop after Wi-Fi connects (fixed 2026-10-11):** `Guru Meditation ... lv_tlsf_malloc ... LvglCBinFont ... Assets::LvglStrategy::Apply`. If the OLED fails, the board uses `NoDisplay`, but Xiaozhi still loads the LVGL font from the assets partition, and LVGL was never initialised. The board now calls `lv_init()` before falling back to `NoDisplay` (`UseNoDisplay()`).
- **OLED ACKs but init fails part-way (fixed 2026-10-11):** the address probe succeeds at about 50 ms, but SSD1306 init commands fail at random points. Like the PCB test, the board now waits until 5.5 s after boot (`DISPLAY_POWER_UP_MS`) before touching the OLED and camera, runs I2C at 100 kHz (internal pull-ups only), and retries init 3 times.
- **Camera `ov2640: get sensor ID failed`:** seen when the camera was initialised at about 250 ms. Re-check after the power-up wait. If it still fails, check the camera ribbon is seated on the Sense board.
- **Weak Wi-Fi:** RSSI around -92 to -97 dBm with WPA3 needed several auth retries. Below about -80 dBm, voice streaming may stutter. Keep Petra close to the router (or the exhibition's own router).
- **White OLED, watchdog in `taskLVGL` / `wait_for_flushing`, Wi-Fi timeout (fixed 2026-10-11):** one failed I2C transfer (`panel_ssd1306_draw_bitmap ... i2c transaction failed`) left LVGL waiting forever, because the stock I2C panel IO only signals flush-done on success (`esp_lcd_panel_io_i2c.c`). The blocked display lock then stalled the Wi-Fi event task. Fix: `oled_io_guard.cc/.h` in the board folder wraps the panel IO. It retries each transfer 3 times, always signals flush-done, and re-initialises the OLED (at most every 2 s) with a full redraw, like the PCB test's recovery loop. Look for `OledIoGuard: OLED re-initialised after N failed transfers`; a high N means the OLED wiring or power needs attention.
- **Camera:** newer XIAO ESP32S3 Sense boards ship with an OV3660 instead of the OV2640. Both are now auto-detected (`CONFIG_CAMERA_OV3660*` in `config.json`). If the OV3660 is detected, the image may need flipping (menuconfig → Camera → mirror settings).
- **Boot freezes right after `OLED found at 0x3C` (fixed 2026-10-11):** ESP-IDF's I2C panel IO waits forever by default (`transaction_timeout_ms` 0 = infinite), so an OLED holding the bus blocked boot. Now `DISPLAY_I2C_TIMEOUT_MS` = 50 (the PCB test used `Wire.setTimeOut(20)`), and the guard calls `i2c_master_bus_reset()` between retries. **Hardware fix for the root cause:** add 4.7 kΩ pull-ups from SDA (GPIO5) and SCL (GPIO6) to 3.3 V if the OLED module has none. The ESP32's internal pull-ups (~45 kΩ) are weak for I2C.
- **Confirmed 2026-10-11:** this board has an **OV3660** camera (`ov3660: Detected Camera sensor PID=0x3660`, `EspVideo: Camera init success`).
- **OLED I2C errors line up with Wi-Fi activity:** single `i2c transaction failed` lines appear at audio start, Wi-Fi start and each Wi-Fi scan. These are current spikes, so the OLED supply dips. The guard's retry succeeds, so the screen keeps working. Hardware fix: a 10-100 µF capacitor across the OLED VCC/GND, plus the 4.7 kΩ pull-ups.
- **`No AP found` / RSSI around -95 dBm:** check the **U.FL antenna** is clicked onto the XIAO ESP32S3; without it the range is only a few metres. The camera clock (20 MHz XCLK) also adds some noise. Test close to the router or with a phone hotspot (2.4 GHz).
- **OLED blank until ~44 s, then on after `OledIoGuard: OLED re-initialised` (fixed 2026-10-11):** this proved that supply dips at boot reset the SSD1306 to display-off while I2C retries still succeeded. The guard now re-sends the init sequence after *any* I2C error (at most once per second) and every 30 s, and the board forces a redraw every 30 s so this also runs on a static screen.
- **OLED re-init loop every ~1 s (fixed 2026-10-11):** the 50 ms I2C timeout was shorter than a full-frame transfer (1 KB at 100 kHz is about 92 ms), so every full redraw timed out and triggered another re-init. The timeout is now 250 ms.
- **Low volume:** `PetraAudioCodec` adds `AUDIO_OUTPUT_GAIN` (2.5x) with a 4:1 soft limiter above 24000. Hardware option: the MAX98357A GAIN pin floating is 9 dB; GAIN to GND is 12 dB; GAIN through 100 kΩ to GND is 15 dB.
- **Camera tool gave no log:** upstream `EspVideo::Capture()` blocks in `VIDIOC_DQBUF` with no timeout and logs nothing on success. `PetraCamera` now logs `Camera: capturing photo...`, `capture OK/FAILED in N ms`, and `explained in N ms` / `explain failed`.
- **Ending a conversation by voice:** board MCP tool `self.conversation.end`. It waits until Petra has finished speaking (or 10 s), then calls `ToggleChatState()`, same as pressing BOOT.
- **`MQTT: Received audio packet with wrong sequence`:** UDP audio arrived out of order. This is harmless and caused by a weak or busy Wi-Fi link.

## ePaper + Petra (Objective 2/3, code written 2026-10-11, not yet tested on hardware)
- **Panel:** XIAO ESP32-C3, UC8179 800x480. Library **Seeed_GFX** (official, `BOARD_SCREEN_COMBO=502`, `USE_XIAO_EPAPER_DRIVER_BOARD`) instead of GxEPD2. Pins from Seeed_GFX `EPaper_Board_Pins_Setups.h`: SCK D8, MOSI D10, CS D1, DC D3, BUSY D2, RST D0.
- **xiaozhi-esp32-mcp** at commit d669aee does not compile (two broken `Serial.println` lines), so it is vendored in `firmware/epaper-panel/lib/xiaozhi-mcp` with that fix.
- **Petra upstream changes:** `esp_video.h` `private:` → `protected:` (so `PetraCamera` can read the frame), and `espressif/mdns` added to `main/idf_component.yml`.
- **Camera:** OV3660 at 640x480 YUYV. The photo is centre-cropped to 2:1 and scaled to 800x400, contrast-stretched, then Floyd–Steinberg dithered (photo) or Sobel edges at the top 12% (sketch).
- **Panel not found:** check both devices are on the same Wi-Fi and that the router allows mDNS/multicast (some guest networks block it).

## Fixes 2026-10-13
- **Photo froze at `Camera: capturing photo...`:** 640x480 YUYV frames arrived truncated (`E:RX:614400-414720`), and upstream `Capture()` waits forever in `VIDIOC_DQBUF`. The camera now sends **640x480 JPEG** (`CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT`), which Petra decodes with esp_new_jpeg. `PetraCamera::Capture()` runs in a helper task with a 6 s timeout (`CAMERA_CAPTURE_TIMEOUT_MS`), so no camera tool can freeze Petra.
- **OLED `probe device timeout`:** something was holding the I2C bus. The battery keeps the OLED powered while the ESP32 resets, so an interrupted transfer can leave it stuck. Petra now does an I2C bus clear (9 SCL pulses + STOP) before and while searching. If it still fails, switch the battery off **and** unplug USB for 5 s.
- **ePaper offline:** the photo tool fails fast (mDNS 1.5 s, HTTP 8 s) and tells the AI the display is offline; Petra keeps working.
- **Digital copy:** the PNG is copied before the "Scan: your copy" QR is drawn, so the QR is not in the visitor's copy.
