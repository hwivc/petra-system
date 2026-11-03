// =====================================================================
//  Petra: OLED eyes + sound (no RGB yet)
//  Board: Seeed XIAO ESP32S3 Sense, OLED SSD1306 128x64 I2C
//  platformio.ini:  lib_deps = olikraus/U8g2
//                   build_flags = -DARDUINO_USB_CDC_ON_BOOT=1
//                   monitor_speed = 115200
// =====================================================================
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 3
#include <ESP_I2S.h>          // ESP32 core 3.x built-in I2S
#else
#include <driver/i2s.h>       // ESP32 core 2.x built-in I2S
#endif

// ---------------- Sound pins (MAX98357) ----------------
#define PIN_I2S_BCLK 1        // D0
#define PIN_I2S_LRC  2        // D1
#define PIN_I2S_DIN  3        // D2
#define ENABLE_SOUND 1
#define VOLUME       1.0f     // 0.0 - 1.0 (raise slowly; too high may disturb the OLED power)

#define PIN_SDA      5        // D4
#define PIN_SCL      6        // D5
#define MOOD_MS      4000     // time per animation

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
int oledAddr = 0;
bool oledOk = false;
unsigned long recoveries = 0;

// ---------------------------------------------------------------------
//  OLED start: copied exactly from v2 (the version that turns the OLED on)
// ---------------------------------------------------------------------
int scanBus(int sda, int scl) {
  Wire.end();
  Wire.begin(sda, scl, 100000);
  Wire.setTimeOut(20);
  int found = 0;
  for (int a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C]   found device at 0x%02X\n", a);
      if (a == 0x3C || a == 0x3D) found = a;
    }
  }
  return found;
}

bool startOled() {
  Serial.printf("[I2C] scanning SDA=GPIO%d (D4), SCL=GPIO%d (D5)...\n", PIN_SDA, PIN_SCL);
  int sda = PIN_SDA, scl = PIN_SCL;
  int addr = scanBus(sda, scl);
  if (!addr) {
    Serial.println("[I2C] nothing found. Trying SDA/SCL swapped...");
    addr = scanBus(PIN_SCL, PIN_SDA);
    if (addr) { sda = PIN_SCL; scl = PIN_SDA; Serial.println("[I2C] OLED found with SDA/SCL SWAPPED"); }
  }
  if (!addr) { Serial.println("[OLED] NOT FOUND"); return false; }

  Serial.printf("[OLED] found at 0x%02X, starting display...\n", addr);
  Wire.end();
  Wire.begin(sda, scl, 400000);
  Wire.setTimeOut(20);
  oledAddr = addr;
  oled.setI2CAddress(addr * 2);
  oled.begin();
  oled.setPowerSave(0);
  oled.setContrast(255);
  Serial.println("[OLED] OK");
  return true;
}

bool oledAnswers() {
  Wire.beginTransmission(oledAddr);
  return Wire.endTransmission() == 0;
}

// ---------------------------------------------------------------------
//  Sound: smooth sine "bell" tones -> I2S -> MAX98357
// ---------------------------------------------------------------------
const int SAMPLE_RATE = 22050;

#if ENABLE_SOUND && ESP_ARDUINO_VERSION_MAJOR >= 3
I2SClass i2sOut;
#endif

// Start I2S with ONLY our three pins (D0, D1, D2). Nothing else is touched.
bool i2sStart() {
#if ENABLE_SOUND && ESP_ARDUINO_VERSION_MAJOR >= 3
  i2sOut.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN, -1, -1);
  return i2sOut.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#elif ENABLE_SOUND
  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  c.sample_rate = SAMPLE_RATE;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  c.dma_buf_count = 6;
  c.dma_buf_len = 256;
  c.tx_desc_auto_clear = true;
  if (i2s_driver_install(I2S_NUM_0, &c, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t p = {};
  p.mck_io_num = I2S_PIN_NO_CHANGE;
  p.bck_io_num = PIN_I2S_BCLK;
  p.ws_io_num = PIN_I2S_LRC;
  p.data_out_num = PIN_I2S_DIN;
  p.data_in_num = I2S_PIN_NO_CHANGE;
  return i2s_set_pin(I2S_NUM_0, &p) == ESP_OK;
#else
  return false;
#endif
}

size_t i2sWrite(const void* buf, size_t len) {
#if ENABLE_SOUND && ESP_ARDUINO_VERSION_MAJOR >= 3
  return i2sOut.write((const uint8_t*)buf, len);
#elif ENABLE_SOUND
  size_t w = 0;
  i2s_write(I2S_NUM_0, buf, len, &w, portMAX_DELAY);
  return w;
#else
  return 0;
#endif
}

enum SfxStyle : uint8_t { BELL, SOFT };   // BELL = ding that fades, SOFT = smooth held tone
struct SfxNote {
  float f0, f1;     // start / end frequency (Hz). Different = smooth slide. 0 = rest
  uint16_t ms;      // length (0 = end of sound)
  uint8_t style;
  float vol;        // 0..1 for this note
};
#define SFX_END {0, 0, 0, 0, 0}

// Musical notes (Hz)
#define N_C5 523
#define N_D5 587
#define N_E5 659
#define N_G5 784
#define N_A5 880
#define N_B5 988
#define N_C6 1047
#define N_D6 1175
#define N_E6 1319
#define N_G6 1568
#define N_C7 2093

const SfxNote SND_BOOT[]      = {{N_C5,N_C5,140,BELL,1},{N_E5,N_E5,140,BELL,1},{N_G5,N_G5,140,BELL,1},{N_C6,N_C6,600,BELL,1},SFX_END};
const SfxNote SND_NEUTRAL[]   = {{N_G5,N_G5,180,BELL,0.8},{N_C6,N_C6,350,BELL,0.8},SFX_END};
const SfxNote SND_HAPPY[]     = {{N_C6,N_C6,110,BELL,1},{N_E6,N_E6,110,BELL,1},{N_G6,N_G6,350,BELL,1},SFX_END};
const SfxNote SND_LOVE[]      = {{N_E5,N_E5,220,SOFT,0.8},{N_G5,N_G5,220,SOFT,0.8},{N_C6,N_C6,220,SOFT,0.8},{N_E6,N_E6,600,BELL,1},SFX_END};
const SfxNote SND_SAD[]       = {{N_E5,N_C5,500,SOFT,0.7},{N_C5,N_A5/2,800,SOFT,0.6},SFX_END};
const SfxNote SND_ANGRY[]     = {{440,370,220,SOFT,1},{0,0,60,SOFT,0},{440,330,400,SOFT,1},SFX_END};
const SfxNote SND_SURPRISED[] = {{400,1400,220,SOFT,0.9},{N_G6,N_G6,400,BELL,1},SFX_END};
const SfxNote SND_SLEEPY[]    = {{N_C5,N_E5,400,SOFT,0.5},{N_E5,330,1000,SOFT,0.4},SFX_END};
const SfxNote SND_THINKING[]  = {{N_A5,N_A5,160,BELL,0.8},{0,0,120,BELL,0},{N_B5,N_B5,160,BELL,0.8},{0,0,120,BELL,0},{N_D6,N_D6,300,BELL,0.8},SFX_END};
const SfxNote SND_CURIOUS[]   = {{N_C5,N_G5,350,SOFT,0.8},{N_G5,N_G5,300,BELL,0.9},SFX_END};
const SfxNote SND_WINK[]      = {{N_G6,N_G6,120,BELL,0.9},{N_C7,N_C7,350,BELL,0.9},SFX_END};

QueueHandle_t soundQueue = nullptr;

void playSound(const SfxNote* snd, const char* name) {
  if (!soundQueue) return;
  xQueueOverwrite(soundQueue, &snd);
  Serial.printf("[SND] play %s\n", name);
}

#if ENABLE_SOUND
void audioTask(void*) {
  bool ok = i2sStart();
  Serial.printf("[SND] I2S on core %d, BCLK=GPIO%d LRC=GPIO%d DIN=GPIO%d: %s\n", xPortGetCoreID(),
                PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN, ok ? "OK" : "FAILED");
  if (!ok) vTaskDelete(nullptr);
  const int FRAMES = 256;
  static int16_t buf[FRAMES * 2];
  const SfxNote* snd = nullptr;
  int idx = 0;
  uint32_t n = 0, total = 0;
  float phase = 0;
  const float attack = SAMPLE_RATE * 0.006f;   // 6 ms soft start
  const float release = SAMPLE_RATE * 0.030f;  // 30 ms soft end

  for (;;) {
    const SfxNote* req;
    if (xQueueReceive(soundQueue, &req, 0) == pdTRUE) {
      snd = req; idx = 0; n = 0; total = 0;
    }
    for (int i = 0; i < FRAMES; i++) {
      float s = 0;
      if (snd) {
        const SfxNote& nt = snd[idx];
        if (nt.ms == 0) {
          snd = nullptr;
        } else {
          if (total == 0) total = (uint32_t)nt.ms * SAMPLE_RATE / 1000;
          float p = (float)n / total;
          float f = nt.f0 + (nt.f1 - nt.f0) * p;
          if (f > 0) {
            phase += f / SAMPLE_RATE;
            if (phase >= 1) phase -= 1;
            float a = 2 * PI * phase;
            // warm tone: sine + soft harmonics
            float w = sinf(a) + 0.25f * sinf(2 * a) + 0.08f * sinf(3 * a);
            w *= 0.75f;
            // envelope
            float env = 1;
            if (n < attack) env = n / attack;
            if (nt.style == BELL) env *= expf(-4.0f * p);           // ding that fades out
            float left = total - n;
            if (left < release) env *= left / release;             // no click at the end
            s = w * env * nt.vol * VOLUME;
          }
          if (++n >= total) { idx++; n = 0; total = 0; }
        }
      }
      int16_t v = (int16_t)(constrain(s, -1.0f, 1.0f) * 32000);
      buf[2 * i] = v;
      buf[2 * i + 1] = v;
    }
    size_t written = i2sWrite(buf, sizeof(buf));
    if (written == 0) vTaskDelay(2);   // never hog the CPU (avoids watchdog resets)
    else vTaskDelay(1);
  }
}

void audioInit() {
  soundQueue = xQueueCreate(1, sizeof(const SfxNote*));
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 2, nullptr, 0);
  delay(100);
}
#else
void audioInit() { Serial.println("[SND] sound disabled (ENABLE_SOUND 0)"); }
#endif

// ---------------------------------------------------------------------
//  Eyes
// ---------------------------------------------------------------------
enum Mood { NEUTRAL, HAPPY, LOVE, SAD, ANGRY, SURPRISED, SLEEPY, THINKING, CURIOUS, WINK, MOOD_COUNT };
const char* moodName[] = {"NEUTRAL", "HAPPY", "LOVE", "SAD", "ANGRY", "SURPRISED", "SLEEPY", "THINKING", "CURIOUS", "WINK"};
const SfxNote* moodSound[] = {SND_NEUTRAL, SND_HAPPY, SND_LOVE, SND_SAD, SND_ANGRY, SND_SURPRISED, SND_SLEEPY, SND_THINKING, SND_CURIOUS, SND_WINK};

int mood = NEUTRAL;
unsigned long moodStart = 0;
float blink = 0, lookX = 0, lookY = 0, tLookX = 0, tLookY = 0;
bool closing = false;
unsigned long nextBlink = 0, nextLook = 0;

void eye(int cx, int cy, int w, int h) {
  if (h < 2) h = 2;
  cx = constrain(cx, w / 2 + 1, 126 - w / 2);
  cy = constrain(cy, h / 2 + 1, 62 - h / 2);
  int x = cx - w / 2, y = cy - h / 2;
  int r = min(6, min(w, h) / 2 - 1);
  if (r >= 2) oled.drawRBox(x, y, w, h, r);
  else        oled.drawBox(x, y, w, h);
}

// erase a triangle from the top of an eye (angry / sad brows)
void cutTop(int cx, int cy, int w, int h, bool slopeRight) {
  int x = cx - w / 2 - 1, y = cy - h / 2 - 1, d = h / 2;
  oled.setDrawColor(0);
  if (slopeRight) oled.drawTriangle(x, y, x + w + 2, y, x + w + 2, y + d);
  else            oled.drawTriangle(x, y, x + w + 2, y, x, y + d);
  oled.setDrawColor(1);
}

// erase the bottom of an eye with a curve -> ^ ^ happy eyes
void cutBottom(int cx, int cy, int w, int h) {
  oled.setDrawColor(0);
  oled.drawFilledEllipse(cx, cy + h / 2 + 2, w * 3 / 4, h / 2);
  oled.setDrawColor(1);
}

void heart(int cx, int cy, int s) {
  int r = s / 2;
  oled.drawDisc(cx - r, cy - s / 4, r);
  oled.drawDisc(cx + r, cy - s / 4, r);
  oled.drawTriangle(cx - s, cy - s / 4 + 2, cx + s, cy - s / 4 + 2, cx, cy + s);
}

void drawFrame() {
  unsigned long now = millis();
  float t = (now - moodStart) / 1000.0f;

  // blinking
  if (now > nextBlink) { closing = true; nextBlink = now + random(1500, 4500); }
  if (closing) { blink += (1 - blink) * 0.6f; if (blink > 0.95f) closing = false; }
  else         { blink += (0 - blink) * 0.4f; }
  // glancing
  if (now > nextLook) { tLookX = random(-18, 19); tLookY = random(-5, 6); nextLook = now + random(600, 2000); }
  lookX += (tLookX - lookX) * 0.35f;
  lookY += (tLookY - lookY) * 0.35f;

  float open = 1 - blink;
  int L = 39, R = 89, Y = 32, W = 34, H = 34;

  oled.clearBuffer();
  switch (mood) {
    case NEUTRAL:
      eye(L + lookX, Y + lookY, W, H * open);
      eye(R + lookX, Y + lookY, W, H * open);
      break;

    case HAPPY: {
      int b = 4 * sinf(t * 2 * PI * 2);              // bounce
      eye(L, Y - 2 + b, W, H); eye(R, Y - 2 + b, W, H);
      cutBottom(L, Y - 2 + b, W, H); cutBottom(R, Y - 2 + b, W, H);
      break;
    }
    case LOVE: {
      int s = 16 + 3 * sinf(t * 2 * PI * 1.5f);      // heartbeat
      heart(L, 30, s); heart(R, 30, s);
      break;
    }
    case SAD: {
      int sx = lookX * 0.3f, h = 26 * open;
      eye(L + sx, Y + 8, W, h); eye(R + sx, Y + 8, W, h);
      cutTop(L + sx, Y + 8, W, h, false); cutTop(R + sx, Y + 8, W, h, true);
      float p = fmodf(t, 1.5f) / 1.5f;               // falling tear
      int ty = Y + 22 + p * 18;
      if (ty < 62) oled.drawDisc(L - 10, ty, 2);
      break;
    }
    case ANGRY: {
      int s = 5 * sinf(t * 2 * PI * 6);              // shake
      int h = 28 * open;
      eye(L + s, Y + 2, W, h); eye(R + s, Y + 2, W, h);
      cutTop(L + s, Y + 2, W, h, true); cutTop(R + s, Y + 2, W, h, false);
      break;
    }
    case SURPRISED: {
      float k = 1 + 0.35f * expf(-t * 2.5f) * cosf(t * 2 * PI * 3);   // pop + wobble
      eye(L, Y, W * k, 38 * k * open); eye(R, Y, W * k, 38 * k * open);
      break;
    }
    case SLEEPY: {
      int h = max(3.0f, 24.0f - t * 6);              // slowly closing
      eye(L, Y + 8, 38, h); eye(R, Y + 8, 38, h);
      oled.setFont(u8g2_font_6x10_tr);
      for (int i = 0; i < 3; i++) {                  // Zzz floating up
        float p = fmodf(t * 0.6f + i * 0.33f, 1.0f);
        int zy = 28 - p * 24;
        if (zy > 8) oled.drawStr(104 + i * 6, zy, "z");
      }
      break;
    }
    case THINKING: {
      int dx = 14 * sinf(t * 2 * PI * 0.5f);         // eyes roll, looking up
      eye(L + dx, Y - 8, W, 28 * open); eye(R + dx, Y - 8, W, 28 * open);
      int dots = ((int)(t * 3)) % 4;
      for (int i = 0; i < dots; i++) oled.drawDisc(54 + i * 10, 56, 2);
      break;
    }
    case CURIOUS: {
      float s = sinf(t * 2 * PI * 0.6f);             // one eye grows, other shrinks
      eye(L, Y - 4 * s, W * (1 + 0.2f * s), H * (1 + 0.2f * s) * open);
      eye(R, Y + 4 * s, W * (1 - 0.2f * s), H * (1 - 0.2f * s) * open);
      break;
    }
    case WINK: {
      bool winking = fmodf(t, 1.2f) < 0.4f;
      eye(L, Y, W, H); cutBottom(L, Y, W, H);
      if (winking) oled.drawRBox(R - W / 2, Y - 2, W, 4, 1);
      else { eye(R, Y, W, H); cutBottom(R, Y, W, H); }
      break;
    }
  }

  if (t < 1.0f) {                                    // mood name for 1 s
    oled.setFont(u8g2_font_5x7_tr);
    oled.drawStr(1, 7, moodName[mood]);
  }
  oled.sendBuffer();
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(300);
  Serial.println("\n===== Petra OLED eyes test =====");

  // v2 spent ~5 s on the LED test before starting the OLED.
  // The OLED seems to need this time after power-up, so we wait the same.
  Serial.println("[BOOT] waiting 5 s for the OLED to power up...");
  delay(5400);

  oledOk = startOled();
  if (oledOk) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_helvB18_tr);
    oled.drawStr((128 - oled.getStrWidth("Petra")) / 2, 38, "Petra");
    oled.sendBuffer();
  }
  audioInit();                      // sound starts only after the OLED is running
  playSound(SND_BOOT, "BOOT");
  delay(1500);
  randomSeed(esp_random());
  moodStart = millis();
  Serial.printf("[MOOD] %s\n", moodName[mood]);
  playSound(moodSound[mood], moodName[mood]);
}

void loop() {
  if (!oledOk) {                    // keep trying until found
    delay(2000);
    oledOk = startOled();
    return;
  }

  if (millis() - moodStart > MOOD_MS) {
    mood = (mood + 1) % MOOD_COUNT;
    moodStart = millis();
    Serial.printf("[MOOD] %s\n", moodName[mood]);
    playSound(moodSound[mood], moodName[mood]);
  }

  unsigned long d0 = millis();
  drawFrame();
  unsigned long drawMs = millis() - d0;

  // health check: if the OLED stopped answering, recover and restart it
  static unsigned long frames = 0, lastLog = 0, lastCheck = 0;
  frames++;
  if (millis() - lastCheck > 500) {
    lastCheck = millis();
    if (!oledAnswers()) {
      recoveries++;
      Serial.printf("[OLED] !!! stopped answering (draw took %lu ms). Restarting it, #%lu\n", drawMs, recoveries);
      delay(200);
      oledOk = startOled();
    }
  }
  if (millis() - lastLog > 2000) {
    Serial.printf("[LOOP] %lu fps | draw %lu ms | recoveries %lu | mood %s\n",
                  frames / 2, drawMs, recoveries, moodName[mood]);
    frames = 0;
    lastLog = millis();
  }
}