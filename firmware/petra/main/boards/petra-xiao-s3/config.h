#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// Petra: Seeed XIAO ESP32S3 Sense + MAX98357A + SSD1306 128x64 OLED
// Pins verified against tools/pcb-test/src/main.cpp and the arduino-esp32
// XIAO_ESP32S3 variant / CameraWebServer camera_pins.h.

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// On-board PDM microphone
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_42
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_41

// MAX98357A I2S amplifier
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_1   // D0
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_2   // D1
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_3   // D2

// MAX98357A (SD pin floating) outputs (L+R)/2, so send audio on both slots.
// Default volume when nothing is saved yet (Xiaozhi's default is 70).
#define AUDIO_DEFAULT_OUTPUT_VOLUME 100
// Extra digital gain on top of volume 100 (speech TTS is much quieter than the
// full-scale test tones in tools/pcb-test). Peaks are soft-limited, not clipped.
#define AUDIO_OUTPUT_GAIN           2.5f

// Photo postcard -> ePaper panel (shared/protocol.md)
#define EPAPER_HOSTNAME             "epaper"   // mDNS name: epaper.local
#define PHOTO_COUNTDOWN_S           3
#define CAMERA_CAPTURE_TIMEOUT_MS   6000       // a stuck capture must never freeze Petra
#define PHOTO_MIRROR                false      // true = selfie-style mirror image
#define PHOTO_KEEP_MS               (3 * 60 * 1000)  // last photo kept for restyling, then deleted

// Max wait for Petra's spoken goodbye before ending a conversation by voice.
#define CONVERSATION_END_TIMEOUT_MS 10000

// Buttons and LED
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define EXT_BUTTON_GPIO         GPIO_NUM_4   // D3, optional push-to-talk to GND
#define BUILTIN_LED_GPIO        GPIO_NUM_21  // orange user LED, active low

// SSD1306 OLED on I2C port 0
#define DISPLAY_SDA_PIN         GPIO_NUM_5   // D4
#define DISPLAY_SCL_PIN         GPIO_NUM_6   // D5
#define DISPLAY_WIDTH           128
#define DISPLAY_HEIGHT          64
// Orientation for Petra's mounting. Re-applied after every OLED re-init
// (the SSD1306 init sequence resets mirroring to off).
#define DISPLAY_MIRROR_X        true
#define DISPLAY_MIRROR_Y        true
// The PCB test waited ~5.4 s after power-up before using the OLED. The OLED
// ACKs its address earlier, but configuration commands fail until then.
#define DISPLAY_POWER_UP_MS       5500
// Keep looking (both pin orders, like the test) until this time since boot.
#define DISPLAY_PROBE_DEADLINE_MS 12000
#define DISPLAY_INIT_ATTEMPTS     3
#define DISPLAY_I2C_SPEED_HZ      100000   // internal pull-ups only: stay slow
// ESP-IDF's default is to wait forever; a stuck OLED then blocks boot.
// Must be longer than a full-frame transfer: 1 KB at 100 kHz takes ~92 ms
// (50 ms made every full redraw time out and re-init in a loop).
#define DISPLAY_I2C_TIMEOUT_MS    250

// On-board OV2640 (DVP), SCCB on I2C port 1
#define CAMERA_PIN_PWDN         GPIO_NUM_NC
#define CAMERA_PIN_RESET        GPIO_NUM_NC
#define CAMERA_PIN_XCLK         GPIO_NUM_10
#define CAMERA_PIN_SIOD         GPIO_NUM_40
#define CAMERA_PIN_SIOC         GPIO_NUM_39
#define CAMERA_PIN_D7           GPIO_NUM_48
#define CAMERA_PIN_D6           GPIO_NUM_11
#define CAMERA_PIN_D5           GPIO_NUM_12
#define CAMERA_PIN_D4           GPIO_NUM_14
#define CAMERA_PIN_D3           GPIO_NUM_16
#define CAMERA_PIN_D2           GPIO_NUM_18
#define CAMERA_PIN_D1           GPIO_NUM_17
#define CAMERA_PIN_D0           GPIO_NUM_15
#define CAMERA_PIN_VSYNC        GPIO_NUM_38
#define CAMERA_PIN_HREF         GPIO_NUM_47
#define CAMERA_PIN_PCLK         GPIO_NUM_13
#define XCLK_FREQ_HZ            20000000

#endif  // _BOARD_CONFIG_H_
