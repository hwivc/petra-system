# Petra (Seeed XIAO ESP32S3 Sense)

PlastiBytes voice assistant: XIAO ESP32S3 Sense + MAX98357A + SSD1306 128x64 OLED.

## Wiring

| Part | Signal | XIAO pin | GPIO |
|---|---|---|---|
| MAX98357A | BCLK | D0 | 1 |
| MAX98357A | LRC | D1 | 2 |
| MAX98357A | DIN | D2 | 3 |
| Push-to-talk (optional, to GND) | | D3 | 4 |
| OLED SSD1306 | SDA | D4 | 5 |
| OLED SSD1306 | SCL | D5 | 6 |
| PDM mic | CLK / DATA | internal | 42 / 41 |
| OV2640 camera | DVP | internal | see `config.h` |
| User LED | | internal | 21 (active low) |

## Build

```bash
python scripts/build.py petra-xiao-s3
```

or `idf.py set-target esp32s3` → `idf.py menuconfig` → Xiaozhi Assistant → Target Board → **Petra (PlastiBytes, XIAO ESP32S3 Sense)**,
then set Flash size → 8 MB and Partition Table → `main/boards/petra-xiao-s3/partitions.csv` (build.py does this from `config.json`).
