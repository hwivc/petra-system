#include "download.h"

#include <DNSServer.h>

#include "display.h"

namespace {

constexpr uint32_t kMaxOpenMs = 3 * 60 * 1000;     // hotspot closes after 3 min
constexpr uint32_t kAfterDownloadMs = 60 * 1000;   // or 1 min after the download

DNSServer dns;
uint8_t* screen = nullptr;  // 1 = black, MSB first, 800x480
bool active = false;
bool downloaded = false;
uint32_t openedMs = 0;
uint32_t downloadedMs = 0;

// ---------------------------------------------------------------- PNG
// 1-bit greyscale PNG, zlib "stored" (uncompressed) blocks: no compressor
// needed, ~48 KB, streamed row by row.
uint32_t crcTable[256];
void initCrc() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}
uint32_t crcUpdate(uint32_t crc, const uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) crc = crcTable[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

struct ChunkWriter {
    explicit ChunkWriter(WiFiClient& client) : out(client) {}
    WiFiClient& out;
    uint32_t crc = 0xFFFFFFFF;
    uint32_t adlerA = 1, adlerB = 0;
    void be32(uint32_t v, bool inCrc) {
        uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
        write(b, 4, inCrc);
    }
    void write(const uint8_t* b, size_t n, bool inCrc = true) {
        if (inCrc) crc = crcUpdate(crc, b, n);
        out.write(b, n);
    }
    void begin(const char* type, uint32_t len) {
        be32(len, false);
        crc = 0xFFFFFFFF;
        write(reinterpret_cast<const uint8_t*>(type), 4);
    }
    void end() { be32(crc ^ 0xFFFFFFFF, false); }
    void zdata(const uint8_t* b, size_t n) {  // data inside the zlib stream (Adler-32)
        for (size_t i = 0; i < n; i++) {
            adlerA = (adlerA + b[i]) % 65521;
            adlerB = (adlerB + adlerA) % 65521;
        }
        write(b, n);
    }
};

constexpr int kRowBytes = kScreenWidth / 8 + 1;              // filter byte + pixels
constexpr uint32_t kRawBytes = kRowBytes * kScreenHeight;    // 48,480 (< 65,535: one block)
constexpr uint32_t kIdatLen = 2 + 5 + kRawBytes + 4;          // zlib hdr + block hdr + data + adler
constexpr uint32_t kPngLen = 8 + (12 + 13) + (12 + kIdatLen) + 12;

void sendPng(WiFiClient& client) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    client.write(sig, 8);
    ChunkWriter w(client);
    // IHDR: 800x480, bit depth 1, greyscale
    w.begin("IHDR", 13);
    w.be32(kScreenWidth, true);
    w.be32(kScreenHeight, true);
    const uint8_t ihdr[5] = {1, 0, 0, 0, 0};
    w.write(ihdr, 5);
    w.end();
    // IDAT
    w.begin("IDAT", kIdatLen);
    const uint8_t zhdr[2] = {0x78, 0x01};
    w.write(zhdr, 2);
    const uint8_t block[5] = {0x01, uint8_t(kRawBytes & 0xFF), uint8_t(kRawBytes >> 8),
                              uint8_t(~kRawBytes & 0xFF), uint8_t((~kRawBytes >> 8) & 0xFF)};
    w.write(block, 5);
    uint8_t row[kRowBytes];
    for (int y = 0; y < kScreenHeight; y++) {
        row[0] = 0;  // filter: none
        for (int i = 0; i < kScreenWidth / 8; i++) row[1 + i] = ~screen[y * (kScreenWidth / 8) + i];  // PNG: 1 = white
        w.zdata(row, kRowBytes);
    }
    w.be32((w.adlerB << 16) | w.adlerA, true);
    w.end();
    // IEND
    w.begin("IEND", 0);
    w.end();
}

void reply(WiFiClient& client, int code, const char* status, const String& body, const char* type) {
    client.printf("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\n"
                  "Connection: close\r\n\r\n", code, status, type, (unsigned)body.length());
    client.print(body);
}

const char* kPage =
    "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Your PlastiBytes postcard</title><style>body{font-family:sans-serif;text-align:center;"
    "padding:12px;max-width:520px;margin:auto}img{width:100%;border:1px solid #999}"
    "a.b{display:block;margin:16px 0;padding:14px;background:#111;color:#fff;border-radius:8px;"
    "text-decoration:none;font-size:18px}small{color:#666}</style></head><body>"
    "<h2>Your PlastiBytes postcard</h2><img src='/postcard.png' alt='postcard'>"
    "<a class=b href='/postcard.png' download='plastibytes-postcard.png'>Download</a>"
    "<p><small>iPhone: press and hold the picture, then <b>Save to Photos</b>.<br>"
    "This photo is deleted from the display when this Wi-Fi closes.</small></p>"
    "<p>Follow us: <b>plastibytes.com</b></p></body></html>";

}  // namespace

String downloadApName() {
    uint64_t mac = ESP.getEfuseMac();
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04X", (unsigned)((mac >> 32) & 0xFFFF));
    return String("PlastiBytes-Photo-") + suffix;
}

uint8_t* downloadPrepare() {
    if (screen == nullptr) screen = static_cast<uint8_t*>(malloc(kScreenBytes));
    return screen;
}

void downloadOpen() {
    if (screen == nullptr) return;
    static bool crcReady = false;
    if (!crcReady) {
        initCrc();
        crcReady = true;
    }
    if (!active) {
        WiFi.mode(WIFI_AP_STA);  // stays connected to the venue Wi-Fi
        WiFi.softAP(downloadApName().c_str());
        dns.start(53, "*", WiFi.softAPIP());
    }
    active = true;
    downloaded = false;
    openedMs = millis();
    Serial.printf("[Copy] hotspot %s open\n", downloadApName().c_str());
}

void downloadClose() {
    if (!active) return;
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    free(screen);  // the photo is gone
    screen = nullptr;
    active = false;
    Serial.println("[Copy] hotspot closed, photo deleted");
}

bool downloadActive() { return active; }

void downloadLoop() {
    if (!active) return;
    dns.processNextRequest();
    uint32_t now = millis();
    bool done = downloaded && (WiFi.softAPgetStationNum() == 0 || now - downloadedMs > kAfterDownloadMs);
    if (done || now - openedMs > kMaxOpenMs) downloadClose();
}

bool downloadHandle(WiFiClient& client, const String& requestLine) {
    if (!active) return false;
    if (requestLine.startsWith("GET /postcard.png")) {
        client.printf("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: %u\r\n"
                      "Content-Disposition: inline; filename=plastibytes-postcard.png\r\n"
                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n", (unsigned)kPngLen);
        sendPng(client);
        downloaded = true;
        downloadedMs = millis();
        Serial.println("[Copy] postcard downloaded");
    } else if (requestLine.startsWith("GET / ") || requestLine.startsWith("GET /index")) {
        reply(client, 200, "OK", kPage, "text/html");
    } else {
        // Captive portal probes (Android, iPhone, Windows) and anything else:
        // send them to the page so it pops up by itself.
        client.print("HTTP/1.1 302 Found\r\nLocation: http://192.168.4.1/\r\nContent-Length: 0\r\n"
                     "Connection: close\r\n\r\n");
    }
    return true;
}
