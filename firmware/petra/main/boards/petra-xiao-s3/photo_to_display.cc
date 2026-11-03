#include "photo_to_display.h"
#include "config.h"
#include "board.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/ip4_addr.h>
#include <mdns.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#define TAG "PhotoToDisplay"

const char* const kPhotoStyles = "photo, sketch, comic, woodcut, engraving, stipple, surprise";
const char* const kPhotoFrames = "classic, polaroid, magazine, hero, wanted, comic, film, event, surprise";

namespace {

constexpr int kOutW = 800;
constexpr int kOutH = 400;
constexpr size_t kOutBytes = kOutW / 8 * kOutH;  // 40,000

const std::vector<std::string> kStyleList = {"photo", "sketch", "comic", "woodcut", "engraving", "stipple"};
const std::vector<std::string> kFrameList = {"classic", "polaroid", "magazine", "hero", "wanted", "comic", "film",
                                             "event"};

std::string Pick(const std::string& wanted, const std::vector<std::string>& list) {
    std::string w;
    for (char c : wanted) w += (char)tolower((unsigned char)c);
    for (const auto& s : list) {
        if (w == s) return s;
    }
    if (w == "surprise" || w == "random") return list[esp_random() % list.size()];
    return list[0];
}

// PSRAM buffer that frees itself.
template <typename T>
struct PsramBuffer {
    T* data = nullptr;
    explicit PsramBuffer(size_t count) {
        data = static_cast<T*>(heap_caps_calloc(count, sizeof(T), MALLOC_CAP_SPIRAM));
    }
    ~PsramBuffer() { heap_caps_free(data); }
    PsramBuffer(const PsramBuffer&) = delete;
    PsramBuffer& operator=(const PsramBuffer&) = delete;
};

// ------------------------------------------------------------ preparation
// Centre-crop to 2:1 and scale (bilinear) to 800x400.
void CropAndScale(const uint8_t* src, int w, int h, uint8_t* dst) {
    int cropW = w, cropH = w / 2;
    if (cropH > h) {
        cropH = h;
        cropW = h * 2;
    }
    int x0 = (w - cropW) / 2, y0 = (h - cropH) / 2;
    for (int y = 0; y < kOutH; y++) {
        int sy256 = y * (cropH - 1) * 256 / (kOutH - 1);
        int sy = sy256 >> 8, fy = sy256 & 255;
        int sy1 = sy + 1 < cropH ? sy + 1 : sy;
        const uint8_t* r0 = src + (y0 + sy) * w + x0;
        const uint8_t* r1 = src + (y0 + sy1) * w + x0;
        for (int x = 0; x < kOutW; x++) {
            int out_x = PHOTO_MIRROR ? kOutW - 1 - x : x;
            int sx256 = x * (cropW - 1) * 256 / (kOutW - 1);
            int sx = sx256 >> 8, fx = sx256 & 255;
            int sx1 = sx + 1 < cropW ? sx + 1 : sx;
            int top = r0[sx] * (256 - fx) + r0[sx1] * fx;
            int bot = r1[sx] * (256 - fx) + r1[sx1] * fx;
            dst[y * kOutW + out_x] = (uint8_t)((top * (256 - fy) + bot * fy) >> 16);
        }
    }
}

// Stretch so the darkest 2% become black and the brightest 2% white.
void StretchContrast(uint8_t* img, size_t n) {
    uint32_t hist[256] = {};
    for (size_t i = 0; i < n; i++) hist[img[i]]++;
    size_t cut = n / 50, acc = 0;
    int lo = 0, hi = 255;
    for (int v = 0; v < 256; v++) {
        acc += hist[v];
        if (acc > cut) { lo = v; break; }
    }
    acc = 0;
    for (int v = 255; v >= 0; v--) {
        acc += hist[v];
        if (acc > cut) { hi = v; break; }
    }
    if (hi - lo < 16) return;  // flat image, leave it
    for (size_t i = 0; i < n; i++) {
        int v = (img[i] - lo) * 255 / (hi - lo);
        img[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
    }
}

void BoxBlur3(const uint8_t* src, uint8_t* dst) {
    memcpy(dst, src, kOutW * kOutH);
    for (int y = 1; y < kOutH - 1; y++) {
        for (int x = 1; x < kOutW - 1; x++) {
            int s = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) s += src[(y + dy) * kOutW + x + dx];
            dst[y * kOutW + x] = s / 9;
        }
    }
}

// Sobel edge strength (0-255) of a softened image.
void Edges(const uint8_t* img, uint8_t* mag) {
    PsramBuffer<uint8_t> soft(kOutW * kOutH);
    BoxBlur3(img, soft.data);
    const uint8_t* s = soft.data;
    for (int y = 1; y < kOutH - 1; y++) {
        for (int x = 1; x < kOutW - 1; x++) {
            auto p = [&](int dx, int dy) { return (int)s[(y + dy) * kOutW + x + dx]; };
            int gx = -p(-1, -1) - 2 * p(-1, 0) - p(-1, 1) + p(1, -1) + 2 * p(1, 0) + p(1, 1);
            int gy = -p(-1, -1) - 2 * p(0, -1) - p(1, -1) + p(-1, 1) + 2 * p(0, 1) + p(1, 1);
            int m = (abs(gx) + abs(gy)) / 4;
            mag[y * kOutW + x] = m > 255 ? 255 : m;
        }
    }
}

// Value above which only `percent` of the pixels lie (at least `floor`).
int TopPercentThreshold(const uint8_t* v, int percent, int floor) {
    uint32_t hist[256] = {};
    for (size_t i = 0; i < (size_t)kOutW * kOutH; i++) hist[v[i]]++;
    size_t keep = (size_t)kOutW * kOutH * percent / 100, acc = 0;
    for (int t = 255; t > 0; t--) {
        acc += hist[t];
        if (acc >= keep) return t < floor ? floor : t;
    }
    return floor;
}

inline void SetBlack(uint8_t* bits, int x, int y) {
    if (x >= 0 && x < kOutW && y >= 0 && y < kOutH) bits[y * (kOutW / 8) + x / 8] |= 0x80 >> (x & 7);
}

// ------------------------------------------------------------------ styles
// photo: Floyd-Steinberg dither (newspaper photo look).
void StylePhoto(const uint8_t* img, uint8_t* bits) {
    PsramBuffer<int16_t> err(kOutW * 2 + 4);
    int16_t* cur = err.data + 1;
    int16_t* nxt = err.data + kOutW + 3;
    for (int y = 0; y < kOutH; y++) {
        memset(nxt - 1, 0, (kOutW + 2) * sizeof(int16_t));
        for (int x = 0; x < kOutW; x++) {
            int v = img[y * kOutW + x] + cur[x] / 16;
            int out = v < 128 ? 0 : 255;
            if (out == 0) SetBlack(bits, x, y);
            int e = v - out;
            cur[x + 1] += e * 7;
            nxt[x - 1] += e * 3;
            nxt[x] += e * 5;
            nxt[x + 1] += e;
        }
        std::swap(cur, nxt);
    }
}

// sketch: outlines only, like a pencil drawing.
void StyleSketch(const uint8_t* img, uint8_t* bits) {
    PsramBuffer<uint8_t> mag(kOutW * kOutH);
    Edges(img, mag.data);
    int t = TopPercentThreshold(mag.data, 12, 8);
    for (int y = 0; y < kOutH; y++)
        for (int x = 0; x < kOutW; x++)
            if (mag.data[y * kOutW + x] >= t) SetBlack(bits, x, y);
}

// comic: pop-art halftone dots for shading + bold ink outlines.
void StyleComic(const uint8_t* img, uint8_t* bits) {
    constexpr int kCell = 8;
    for (int cy = 0; cy < kOutH; cy += kCell) {
        for (int cx = 0; cx < kOutW; cx += kCell) {
            int sum = 0;
            for (int y = cy; y < cy + kCell; y++)
                for (int x = cx; x < cx + kCell; x++) sum += img[y * kOutW + x];
            float dark = 1.0f - sum / (255.0f * kCell * kCell);
            float r = kCell * 0.72f * sqrtf(dark);
            float mx = cx + kCell / 2.0f - 0.5f, my = cy + kCell / 2.0f - 0.5f;
            for (int y = cy; y < cy + kCell; y++)
                for (int x = cx; x < cx + kCell; x++)
                    if ((x - mx) * (x - mx) + (y - my) * (y - my) <= r * r) SetBlack(bits, x, y);
        }
    }
    PsramBuffer<uint8_t> mag(kOutW * kOutH);
    Edges(img, mag.data);
    int t = TopPercentThreshold(mag.data, 9, 10);
    for (int y = 0; y < kOutH; y++)
        for (int x = 0; x < kOutW; x++)
            if (mag.data[y * kOutW + x] >= t) {
                SetBlack(bits, x, y);
                SetBlack(bits, x + 1, y);  // thicker ink line
                SetBlack(bits, x, y + 1);
            }
}

// woodcut: bold black shapes, carved stripes in the mid-tones, cut outlines.
void StyleWoodcut(const uint8_t* img, uint8_t* bits) {
    PsramBuffer<uint8_t> soft(kOutW * kOutH);
    BoxBlur3(img, soft.data);
    // Adapt to the photo: darkest 30% solid black, next 40% carved stripes.
    int darkT = 0;
    {
        uint32_t hist[256] = {};
        for (size_t i = 0; i < (size_t)kOutW * kOutH; i++) hist[soft.data[i]]++;
        size_t n = (size_t)kOutW * kOutH, acc = 0;
        int p30 = 0, p70 = 255;
        for (int v = 0; v < 256; v++) {
            acc += hist[v];
            if (p30 == 0 && acc >= n * 30 / 100) p30 = v;
            if (acc >= n * 70 / 100) { p70 = v; break; }
        }
        darkT = p30;
        for (int y = 0; y < kOutH; y++) {
            for (int x = 0; x < kOutW; x++) {
                int v = soft.data[y * kOutW + x];
                int wave = (int)(2.0f * sinf(x / 23.0f + y / 41.0f));
                bool carved = ((y + wave) % 5) < 2;
                if (v <= darkT || (v <= p70 && carved)) SetBlack(bits, x, y);
            }
        }
    }
    PsramBuffer<uint8_t> mag(kOutW * kOutH);
    Edges(img, mag.data);
    int t = TopPercentThreshold(mag.data, 6, 12);
    for (int y = 0; y < kOutH; y++)
        for (int x = 0; x < kOutW; x++)
            if (mag.data[y * kOutW + x] >= t) SetBlack(bits, x, y);
}

// engraving: shading from fine wavy parallel lines, like a banknote portrait.
void StyleEngraving(const uint8_t* img, uint8_t* bits) {
    constexpr float kPeriod = 6.0f;
    for (int y = 0; y < kOutH; y++) {
        for (int x = 0; x < kOutW; x++) {
            float dark = 1.0f - img[y * kOutW + x] / 255.0f;
            float phase = fmodf(y + 2.5f * sinf(x / 19.0f), kPeriod);
            if (phase < 0) phase += kPeriod;
            float half = dark * kPeriod * 0.5f;
            if (fabsf(phase - kPeriod / 2) < half) SetBlack(bits, x, y);
        }
    }
}

// stipple: shading made only of scattered ink dots.
void StyleStipple(const uint8_t* img, uint8_t* bits) {
    uint32_t seed = 0x9E3779B9u;
    for (int y = 0; y < kOutH; y++) {
        for (int x = 0; x < kOutW; x++) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            float dark = 1.0f - img[y * kOutW + x] / 255.0f;
            float p = powf(dark, 1.6f) * 0.85f;
            if ((seed & 0xFFFF) < p * 65535.0f) SetBlack(bits, x, y);
        }
    }
}

// ---------------------------------------------------------------- sending
std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// Finds epaper.local with mDNS (cached; re-resolved after a failure).
std::string ResolvePanel(bool refresh) {
    static std::string cached;
    static bool mdns_ready = false;
    if (!refresh && !cached.empty()) return cached;
    if (!mdns_ready) {
        mdns_ready = mdns_init() == ESP_OK;
        if (!mdns_ready) return "";
    }
    esp_ip4_addr_t addr = {};
    if (mdns_query_a(EPAPER_HOSTNAME, 1500, &addr) != ESP_OK) {
        ESP_LOGW(TAG, "mDNS: %s.local not found", EPAPER_HOSTNAME);
        cached.clear();
        return "";
    }
    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&addr));
    cached = ip;
    ESP_LOGI(TAG, "mDNS: %s.local is %s", EPAPER_HOSTNAME, ip);
    return cached;
}

// Returns HTTP status, or -1 if the panel could not be reached.
int Post(const std::string& ip, const uint8_t* bits, const std::string& caption, const std::string& frame) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    if (!http) return -1;
    http->SetTimeout(8000);  // the panel may be finishing a 2-5 s refresh
    http->SetHeader("Content-Type", "application/octet-stream");
    http->SetHeader("X-Caption", UrlEncode(caption.substr(0, 80)));
    http->SetHeader("X-Frame", frame);
    http->SetContent(std::string(reinterpret_cast<const char*>(bits), kOutBytes));  // sets Content-Length
    if (auto opened = http->Open("POST", "http://" + ip + "/photo"); !opened) {
        ESP_LOGW(TAG, "POST failed: %s", opened.error().ToString().c_str());
        return -1;
    }
    auto status = http->GetStatusCode();
    http->Close();
    return status ? *status : -1;
}

}  // namespace

std::string ResolveStyle(const std::string& style) { return Pick(style, kStyleList); }
std::string ResolveFrame(const std::string& frame) { return Pick(frame, kFrameList); }

std::string SendPhotoToDisplay(const uint8_t* luma, int width, int height, const std::string& caption,
                               const std::string& style, const std::string& frame) {
    int64_t start = esp_timer_get_time();
    PsramBuffer<uint8_t> gray(kOutW * kOutH);
    PsramBuffer<uint8_t> bits(kOutBytes);
    if (gray.data == nullptr || bits.data == nullptr) {
        return "Not enough memory to prepare the photo";
    }
    CropAndScale(luma, width, height, gray.data);
    StretchContrast(gray.data, kOutW * kOutH);
    if (style == "sketch") StyleSketch(gray.data, bits.data);
    else if (style == "comic") StyleComic(gray.data, bits.data);
    else if (style == "woodcut") StyleWoodcut(gray.data, bits.data);
    else if (style == "engraving") StyleEngraving(gray.data, bits.data);
    else if (style == "stipple") StyleStipple(gray.data, bits.data);
    else StylePhoto(gray.data, bits.data);
    ESP_LOGI(TAG, "Photo prepared (%s, frame %s) in %lld ms, free PSRAM %u", style.c_str(), frame.c_str(),
             (esp_timer_get_time() - start) / 1000, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    const std::string sent = "Done: a " + style + " postcard with the " + frame +
                             " frame will appear on the display in about 5 seconds. The visitor can scan "
                             "the small QR code on it to download a copy.";
    for (int attempt = 0; attempt < 2; attempt++) {
        std::string ip = ResolvePanel(attempt > 0);
        if (ip.empty()) continue;
        int status = Post(ip, bits.data, caption, frame);
        ESP_LOGI(TAG, "POST /photo -> %d", status);
        if (status == 200) return sent;
        if (status == 503) {
            vTaskDelay(pdMS_TO_TICKS(3000));  // panel busy refreshing, try once more
            if (Post(ip, bits.data, caption, frame) == 200) return sent;
            return "The display is busy, try again in a few seconds.";
        }
    }
    return "The photo was taken, but the ePaper display is offline (switched off or not on this Wi-Fi), "
           "so it could not be shown. Tell the visitor kindly and offer something else, for example to "
           "describe what you see with the camera.";
}
