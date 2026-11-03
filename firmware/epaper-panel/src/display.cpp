#include "display.h"

#include <TFT_eSPI.h>
#include <qrcode.h>  // esp_qrcode, bundled with the ESP32 core

#include "logos.h"

namespace {

EPaper epaper;
constexpr int W = kScreenWidth;
constexpr int H = kScreenHeight;
constexpr int kMargin = 30;
String eventName = "CITS26 - Yaounde";
bool whiteIsOne = false;  // sprite bit polarity, found at start-up

const GFXfont* const kTextFonts[] = {&FreeSansBold24pt7b, &FreeSansBold18pt7b, &FreeSans18pt7b,
                                     &FreeSans12pt7b};

// ------------------------------------------------------------------ helpers
std::vector<String> wrap(const String& text, int maxWidth) {
    std::vector<String> lines;
    String line, word;
    auto flushWord = [&]() {
        if (word.isEmpty()) return;
        String candidate = line.isEmpty() ? word : line + " " + word;
        if (epaper.textWidth(candidate) <= maxWidth || line.isEmpty()) {
            line = candidate;
        } else {
            lines.push_back(line);
            line = word;
        }
        word = "";
    };
    for (size_t i = 0; i <= text.length(); i++) {
        char c = i < text.length() ? text[i] : ' ';
        if (c == '\n') {
            flushWord();
            lines.push_back(line);
            line = "";
        } else if (c == ' ') {
            flushWord();
        } else {
            word += c;
        }
    }
    if (!line.isEmpty()) lines.push_back(line);
    return lines;
}

// Wrapped text centred in a box, using the largest font that fits.
void drawFittedText(const String& text, int x, int y, int w, int h) {
    for (const GFXfont* font : kTextFonts) {
        epaper.setFreeFont(font);
        int lineH = epaper.fontHeight() + 6;
        auto lines = wrap(text, w);
        if ((int)lines.size() * lineH <= h || font == kTextFonts[3]) {
            int maxLines = h / lineH;
            if ((int)lines.size() > maxLines) {
                lines.resize(maxLines);
                lines.back() += "...";
            }
            int top = y + (h - (int)lines.size() * lineH) / 2 + lineH / 2;
            epaper.setTextDatum(MC_DATUM);
            for (size_t i = 0; i < lines.size(); i++) epaper.drawString(lines[i], x + w / 2, top + i * lineH);
            return;
        }
    }
}

// Shortens text with "..." until it fits maxWidth in the current font.
String fit(String text, int maxWidth) {
    while (text.length() > 3 && epaper.textWidth(text) > maxWidth) text = text.substring(0, text.length() - 4) + "...";
    return text;
}

// Black text with a white outline, readable on top of a photo.
void outlinedText(const String& text, int x, int y) {
    epaper.setTextColor(TFT_WHITE);
    for (int dx = -2; dx <= 2; dx += 2)
        for (int dy = -2; dy <= 2; dy += 2)
            if (dx || dy) epaper.drawString(text, x + dx, y + dy);
    epaper.setTextColor(TFT_BLACK);
    epaper.drawString(text, x, y);
}

void thickRect(int x, int y, int w, int h, int t) {
    for (int i = 0; i < t; i++) epaper.drawRect(x + i, y + i, w - 2 * i, h - 2 * i, TFT_BLACK);
}

void star(int cx, int cy, int r) {
    float a = -PI / 2;
    for (int i = 0; i < 5; i++, a += 2 * PI / 5) {
        int x1 = cx + r * cos(a), y1 = cy + r * sin(a);
        int x2 = cx + r * 0.38 * cos(a + PI / 5), y2 = cy + r * 0.38 * sin(a + PI / 5);
        int x3 = cx + r * 0.38 * cos(a - PI / 5), y3 = cy + r * 0.38 * sin(a - PI / 5);
        epaper.fillTriangle(x1, y1, x2, y2, cx, cy, TFT_BLACK);
        epaper.fillTriangle(x1, y1, x3, y3, cx, cy, TFT_BLACK);
    }
}

void burst(int cx, int cy, int r, int points) {
    for (int i = 0; i < points; i++) {
        float a1 = i * 2 * PI / points, a2 = (i + 0.5) * 2 * PI / points, a3 = (i + 1) * 2 * PI / points;
        epaper.fillTriangle(cx + r * cos(a1), cy + r * sin(a1), cx + r * 0.65 * cos(a2), cy + r * 0.65 * sin(a2),
                            cx, cy, TFT_BLACK);
        epaper.fillTriangle(cx + r * 0.65 * cos(a2), cy + r * 0.65 * sin(a2), cx + r * cos(a3), cy + r * sin(a3),
                            cx, cy, TFT_BLACK);
    }
}

void logo(const uint8_t* bits, int w, int h, int x, int y) {
    epaper.drawBitmap(x, y, bits, w, h, TFT_BLACK);
}

// esp_qrcode draws through a callback, so the target box goes via statics.
int qrX = 0, qrY = 0, qrBox = 0;
void drawQrModules(esp_qrcode_handle_t qr) {
    int n = esp_qrcode_get_size(qr);
    int scale = qrBox / (n + 4);  // 2-module quiet zone each side
    if (scale < 1) scale = 1;
    int size = scale * n;
    int ox = qrX + (qrBox - size) / 2, oy = qrY + (qrBox - size) / 2;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (esp_qrcode_get_module(qr, x, y)) epaper.fillRect(ox + x * scale, oy + y * scale, scale, scale, TFT_BLACK);
}
void qr(const String& text, int x, int y, int box) {
    epaper.fillRect(x, y, box, box, TFT_WHITE);
    qrX = x;
    qrY = y;
    qrBox = box;
    esp_qrcode_config_t cfg = {};
    cfg.display_func = drawQrModules;
    cfg.max_qrcode_version = 10;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    esp_qrcode_generate(&cfg, text.c_str());
}

void header(const String& title) {
    epaper.fillRect(0, 0, W, 70, TFT_BLACK);
    epaper.setTextColor(TFT_WHITE, TFT_BLACK);
    epaper.setFreeFont(&FreeSansBold24pt7b);
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString(fit(title, W - 40), W / 2, 35);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
}

void footer(const String& right) {
    epaper.drawFastHLine(kMargin, H - 48, W - 2 * kMargin, TFT_BLACK);
    logo(LOGO_PB_SMALL, LOGO_PB_SMALL_W, LOGO_PB_SMALL_H, kMargin, H - 41);
    int rx = kMargin + LOGO_PB_SMALL_W + 14;
    epaper.drawFastVLine(rx, H - 40, 32, TFT_BLACK);
    logo(LOGO_ROBOT_SAVE, LOGO_ROBOT_SAVE_W, LOGO_ROBOT_SAVE_H, rx + 14, H - 24 - LOGO_ROBOT_SAVE_H / 2);
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.setTextDatum(MR_DATUM);
    epaper.drawString(right, W - kMargin, H - 24);
}

void begin() {
    epaper.setTextSize(1);
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
}

void finish() { epaper.update(); }  // full refresh, 2-5 s

// ------------------------------------------------------------- photo frames
// Every frame leaves the bottom-right corner (x>=660, y>=330) for the
// "your copy" QR box.

void eventTag(int x, int y) {
    // CITS mark + event name, on a white badge
    int w = 236, h = LOGO_CITS_H + 10;
    epaper.fillRoundRect(x, y, w, h, 10, TFT_WHITE);
    epaper.drawRoundRect(x, y, w, h, 10, TFT_BLACK);
    epaper.drawBitmap(x + 6, y + 5, LOGO_CITS, LOGO_CITS_W, LOGO_CITS_H, TFT_BLACK);
    epaper.setFreeFont(&FreeSansBold12pt7b);
    epaper.setTextDatum(ML_DATUM);
    epaper.drawString(fit(eventName, w - LOGO_CITS_W - 20), x + LOGO_CITS_W + 12, y + h / 2);
}

void drawFrame(const String& frame, const String& caption) {
    if (frame == "polaroid") {
        // white border, slightly thicker at the bottom, tape on the corners
        epaper.fillRect(0, 0, W, 24, TFT_WHITE);
        epaper.fillRect(0, 0, 40, H, TFT_WHITE);
        epaper.fillRect(W - 40, 0, 40, H, TFT_WHITE);
        epaper.fillRect(0, 360, W, H - 360, TFT_WHITE);
        epaper.drawRect(40, 24, W - 80, 336, TFT_BLACK);
        for (int t = 0; t < 2; t++) {
            int tx = t == 0 ? 60 : W - 160;
            for (int i = 0; i < 26; i += 4) epaper.drawFastHLine(tx, 8 + i, 100, TFT_BLACK);
        }
        epaper.setFreeFont(&FreeSerifBoldItalic24pt7b);
        epaper.setTextDatum(ML_DATUM);
        epaper.drawString(fit(caption, 560), 60, 410);
        epaper.setFreeFont(&FreeSerifItalic12pt7b);
        epaper.drawString(eventName, 60, 455);
    } else if (frame == "magazine") {
        epaper.fillRect(0, 0, LOGO_PB_LARGE_W + 30, LOGO_PB_LARGE_H + 24, TFT_WHITE);
        logo(LOGO_PB_LARGE, LOGO_PB_LARGE_W, LOGO_PB_LARGE_H, 15, 12);
        epaper.setFreeFont(&FreeSansBold12pt7b);
        epaper.setTextDatum(TL_DATUM);
        outlinedText("MONTHLY  -  OCT 2026", 18, LOGO_PB_LARGE_H + 30);
        epaper.setFreeFont(&FreeSansBold18pt7b);
        epaper.setTextDatum(TR_DATUM);
        outlinedText("COVER STAR", W - 20, 20);
        epaper.setFreeFont(&FreeSansBold12pt7b);
        outlinedText("125K bottles recycled!", W - 20, 70);
        outlinedText("Live from " + eventName, W - 20, 102);
        epaper.fillRect(0, 380, W, H - 380, TFT_WHITE);
        epaper.fillRect(0, 380, W, 4, TFT_BLACK);
        epaper.setFreeFont(&FreeSansBold24pt7b);
        epaper.setTextDatum(ML_DATUM);
        epaper.drawString(fit(caption, 620), 20, 432);
    } else if (frame == "hero") {
        // certificate: photo window on the left, text on the right
        epaper.fillRect(380, 0, W - 380, H, TFT_WHITE);
        epaper.fillRect(0, 0, 380, 40, TFT_WHITE);
        epaper.fillRect(0, 0, 40, H, TFT_WHITE);
        epaper.fillRect(0, 400, 380, H - 400, TFT_WHITE);
        thickRect(6, 6, W - 12, H - 12, 4);
        epaper.drawRect(16, 16, W - 32, H - 32, TFT_BLACK);
        thickRect(40, 40, 340, 360, 3);
        epaper.setTextDatum(TC_DATUM);
        epaper.setFreeFont(&FreeSerifBold18pt7b);
        epaper.drawString("CERTIFICATE", 590, 40);
        epaper.setFreeFont(&FreeSerifItalic12pt7b);
        epaper.drawString("this proudly declares", 590, 92);
        epaper.setFreeFont(&FreeSansBold18pt7b);
        epaper.drawString(fit(caption, 380), 590, 128);
        epaper.setFreeFont(&FreeSerifBold18pt7b);
        epaper.drawString("RECYCLING HERO", 590, 182);
        epaper.setFreeFont(&FreeSerifItalic12pt7b);
        epaper.drawString("at " + eventName, 590, 232);
        star(470, 290, 22);
        star(530, 280, 26);
        star(590, 290, 22);
        epaper.drawString("Signed: Petra, PlastiBytes AI", 520, 330);
    } else if (frame == "wanted") {
        // old west poster: photo window in the middle
        epaper.fillRect(0, 0, W, 110, TFT_WHITE);
        epaper.fillRect(0, 0, 150, H, TFT_WHITE);
        epaper.fillRect(W - 150, 0, 150, H, TFT_WHITE);
        epaper.fillRect(0, 370, W, H - 370, TFT_WHITE);
        thickRect(150, 110, W - 300, 260, 4);
        thickRect(4, 4, W - 8, H - 8, 3);
        epaper.setTextDatum(TC_DATUM);
        epaper.setFreeFont(&FreeSerifBold24pt7b);
        epaper.setTextSize(2);
        epaper.drawString("WANTED", W / 2, 6);
        epaper.setTextSize(1);
        epaper.setFreeFont(&FreeSerifBold18pt7b);
        epaper.drawString(fit(caption, 560), W / 2 - 60, 380);
        epaper.setFreeFont(&FreeSerifItalic12pt7b);
        epaper.drawString("for saving plastic at " + eventName, W / 2 - 60, 428);
        epaper.setTextDatum(ML_DATUM);
        epaper.drawString("REWARD:", 20, 200);
        epaper.drawString("a cleaner", 20, 230);
        epaper.drawString("planet", 20, 260);
    } else if (frame == "comic") {
        thickRect(0, 0, W, kPhotoHeight, 8);
        epaper.fillRect(0, kPhotoHeight, W, H - kPhotoHeight, TFT_WHITE);
        // speech bubble top-left
        epaper.setFreeFont(&FreeSansBold18pt7b);
        String text = fit(caption, 420);
        int bw = epaper.textWidth(text) + 60, bh = 80;
        epaper.fillEllipse(30 + bw / 2, 30 + bh / 2, bw / 2 + 4, bh / 2 + 4, TFT_BLACK);
        epaper.fillEllipse(30 + bw / 2, 30 + bh / 2, bw / 2, bh / 2, TFT_WHITE);
        epaper.fillTriangle(70, 100, 110, 104, 60, 150, TFT_BLACK);
        epaper.fillTriangle(76, 98, 104, 100, 66, 140, TFT_WHITE);
        epaper.setTextDatum(MC_DATUM);
        epaper.drawString(text, 30 + bw / 2, 30 + bh / 2);
        // POW! burst bottom-left
        burst(110, 400, 70, 12);
        epaper.fillCircle(110, 400, 40, TFT_WHITE);
        epaper.setFreeFont(&FreeSansBold18pt7b);
        epaper.drawString("POW!", 110, 400);
        epaper.setFreeFont(&FreeSansBold12pt7b);
        epaper.setTextDatum(ML_DATUM);
        epaper.drawString("Episode: " + eventName, 200, 440);
    } else if (frame == "film") {
        // film strip: black bands with sprocket holes, photo in between
        epaper.fillRect(0, 0, W, 44, TFT_BLACK);
        epaper.fillRect(0, 344, W, 44, TFT_BLACK);
        for (int x = 14; x < W; x += 52) {
            epaper.fillRoundRect(x, 12, 28, 20, 4, TFT_WHITE);
            epaper.fillRoundRect(x, 356, 28, 20, 4, TFT_WHITE);
        }
        epaper.fillRect(0, 388, W, H - 388, TFT_WHITE);
        epaper.setFreeFont(&FreeSansBold18pt7b);
        epaper.setTextDatum(ML_DATUM);
        epaper.drawString(fit(caption, 600), 20, 425);
        epaper.setFreeFont(&FreeSans12pt7b);
        epaper.drawString(eventName + "  -  frame 26", 20, 462);
    } else {
        // classic (and "event"): photo on top, caption bar, logos
        epaper.fillRect(0, kPhotoHeight, W, H - kPhotoHeight, TFT_WHITE);
        epaper.fillRect(0, kPhotoHeight, W, 3, TFT_BLACK);
        epaper.setFreeFont(&FreeSansBold18pt7b);
        epaper.setTextDatum(ML_DATUM);
        epaper.drawString(fit(caption, 460), 20, kPhotoHeight + 30);
        logo(LOGO_PB_SMALL, LOGO_PB_SMALL_W, LOGO_PB_SMALL_H, 20, kPhotoHeight + 46);
        epaper.setFreeFont(&FreeSans12pt7b);
        epaper.drawString(eventName, 190, kPhotoHeight + 62);
        if (frame == "event") eventTag(W - 250, 14);
    }
}

// The QR box that gives the visitor a digital copy.
void downloadBox(const String& qrText) {
    int x = 662, y = 332, w = 132, h = 144;
    epaper.fillRoundRect(x - 4, y - 4, w + 8, h + 8, 8, TFT_WHITE);
    epaper.drawRoundRect(x - 4, y - 4, w + 8, h + 8, 8, TFT_BLACK);
    epaper.setFreeFont(&FreeSansBold9pt7b);
    epaper.setTextDatum(TC_DATUM);
    epaper.drawString("Scan: your copy", x + w / 2, y);
    qr(qrText, x + 6, y + 20, 120);
}

}  // namespace

// ================================================================ public API
void displayBegin() {
    epaper.begin();
    epaper.fillScreen(TFT_WHITE);
    whiteIsOne = static_cast<uint8_t*>(epaper.getPointer())[0] == 0xFF;
}

void setEventName(const String& name) {
    if (!name.isEmpty()) eventName = name;
}

void copyScreen(uint8_t* out) {
    const uint8_t* buf = static_cast<const uint8_t*>(epaper.getPointer());
    for (size_t i = 0; i < kScreenBytes; i++) out[i] = whiteIsOne ? ~buf[i] : buf[i];
}

void showIdle(IdleScreen screen, const String& status) {
    switch (screen) {
        case IdleScreen::Mentorship:
            showQr("https://plastibytes.com/mentorship", "Become Africa's next hardware innovator: join the PlastiBytes Mentorship");
            return;
        case IdleScreen::Website:
            showQr("https://plastibytes.com", "We turn plastic bottles into 3D-printing filament. Discover PlastiBytes");
            return;
        case IdleScreen::Hwivc:
            showQr("https://hwivc.org", "Join the Hardware Innovation Valley Community");
            return;
        case IdleScreen::WhatsApp:
            showQr("https://chat.whatsapp.com/I8a9PIMVv76Am17iXg2b9Y", "Scan to join the PlastiBytes WhatsApp community");
            return;
        case IdleScreen::Event: {
            begin();
            epaper.drawBitmap(kMargin, 30, LOGO_CITS, LOGO_CITS_W, LOGO_CITS_H, TFT_BLACK);
            epaper.setFreeFont(&FreeSansBold24pt7b);
            epaper.setTextDatum(ML_DATUM);
            epaper.drawString("Welcome to CITS26!", kMargin + LOGO_CITS_W + 20, 70);
            epaper.setFreeFont(&FreeSans18pt7b);
            epaper.drawString("Cameroon International Tech Summit", kMargin, 160);
            epaper.drawString("15-17 Oct 2026  -  Palais des Congres, Yaounde", kMargin, 205);
            epaper.setFreeFont(&FreeSansBold18pt7b);
            epaper.drawString("Be Bold. Limitless. Visionary. Resilient.", kMargin, 270);
            epaper.setFreeFont(&FreeSans18pt7b);
            epaper.drawString("Say \"Hey Petra, take my picture\" for a", kMargin, 340);
            epaper.drawString("free CITS26 postcard!", kMargin, 380);
            footer(status);
            finish();
            return;
        }
        default:
            break;
    }
    begin();
    logo(LOGO_PB_LARGE, LOGO_PB_LARGE_W, LOGO_PB_LARGE_H, (W - LOGO_PB_LARGE_W) / 2, 24);
    epaper.setFreeFont(&FreeSansBold24pt7b);
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString("Say \"Hey Petra\"", W / 2, 160);
    epaper.setFreeFont(&FreeSans18pt7b);
    epaper.drawString("Ask me anything about recycling!", W / 2, 210);

    const char* steps[] = {"Plastic waste", "Filament", "3D print", "STEM class"};
    int boxW = 160, boxH = 60, gap = 30, y = 250;
    int x = (W - (4 * boxW + 3 * gap)) / 2;
    epaper.setFreeFont(&FreeSansBold12pt7b);
    for (int i = 0; i < 4; i++) {
        epaper.drawRoundRect(x, y, boxW, boxH, 10, TFT_BLACK);
        epaper.drawRoundRect(x + 1, y + 1, boxW - 2, boxH - 2, 9, TFT_BLACK);
        epaper.drawString(steps[i], x + boxW / 2, y + boxH / 2);
        if (i < 3) {
            int ax = x + boxW + 4, ay = y + boxH / 2;
            epaper.fillTriangle(ax, ay - 10, ax, ay + 10, ax + gap - 8, ay, TFT_BLACK);
        }
        x += boxW + gap;
    }
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.drawString("Try: \"Take my picture\"  -  \"Make me a comic\"  -  \"Can this be recycled?\"", W / 2, 355);
    footer(status);
    finish();
}

void showMessage(const String& text) {
    begin();
    header("Petra says");
    drawFittedText(text, kMargin, 90, W - 2 * kMargin, H - 160);
    footer("Say \"Hey Petra\"");
    finish();
}

void showCard(const String& title, const std::vector<String>& lines, const String& verdict) {
    begin();
    header(title);
    int textW = W - 2 * kMargin;
    if (verdict == "yes" || verdict == "no") {
        int bx = W - kMargin - 220, by = 100, bw = 220, bh = 220;
        epaper.fillRoundRect(bx, by, bw, bh, 20, verdict == "yes" ? TFT_BLACK : TFT_WHITE);
        epaper.drawRoundRect(bx, by, bw, bh, 20, TFT_BLACK);
        epaper.drawRoundRect(bx + 2, by + 2, bw - 4, bh - 4, 18, TFT_BLACK);
        epaper.setTextColor(verdict == "yes" ? TFT_WHITE : TFT_BLACK);
        epaper.setFreeFont(&FreeSansBold24pt7b);
        epaper.setTextDatum(MC_DATUM);
        epaper.drawString(verdict == "yes" ? "YES" : "NO", bx + bw / 2, by + 90);
        epaper.setFreeFont(&FreeSansBold12pt7b);
        epaper.drawString(verdict == "yes" ? "Recyclable" : "Not for", bx + bw / 2, by + 150);
        epaper.drawString(verdict == "yes" ? "into filament" : "filament", bx + bw / 2, by + 180);
        epaper.setTextColor(TFT_BLACK, TFT_WHITE);
        textW -= bw + 20;
    }
    epaper.setFreeFont(&FreeSans18pt7b);
    epaper.setTextDatum(ML_DATUM);
    int y = 115, lineH = epaper.fontHeight() + 8;
    for (const String& item : lines) {
        for (const String& l : wrap(item, textW - 30)) {
            if (y > H - 80) break;
            epaper.fillCircle(kMargin + 8, y, 6, TFT_BLACK);
            epaper.drawString(l, kMargin + 30, y);
            y += lineH;
        }
    }
    footer("Ask Petra about it!");
    finish();
}

void showSteps(const String& title, const std::vector<String>& steps) {
    begin();
    header(title);
    int n = steps.size() > 5 ? 5 : steps.size();
    int top = 90, rowH = n ? (H - 60 - top) / n : 0;
    if (rowH > 80) rowH = 80;
    for (int i = 0; i < n; i++) {
        int cy = top + i * rowH + rowH / 2;
        epaper.fillCircle(kMargin + 24, cy, 22, TFT_BLACK);
        epaper.setTextColor(TFT_WHITE);
        epaper.setFreeFont(&FreeSansBold18pt7b);
        epaper.setTextDatum(MC_DATUM);
        epaper.drawString(String(i + 1), kMargin + 24, cy);
        epaper.setTextColor(TFT_BLACK);
        if (i < n - 1) epaper.fillRect(kMargin + 22, cy + 22, 4, rowH - 44, TFT_BLACK);
        epaper.setFreeFont(rowH >= 70 ? &FreeSans18pt7b : &FreeSans12pt7b);
        epaper.setTextDatum(ML_DATUM);
        epaper.drawString(fit(steps[i], W - kMargin * 2 - 70), kMargin + 64, cy);
    }
    footer("Explained by Petra");
    finish();
}

void showQr(const String& url, const String& label) {
    begin();
    qr(url, kMargin, 40, 340);
    int tx = kMargin + 370, tw = W - tx - kMargin;
    logo(LOGO_PB_SMALL, LOGO_PB_SMALL_W, LOGO_PB_SMALL_H, tx, 50);
    epaper.setFreeFont(&FreeSansBold18pt7b);
    epaper.setTextDatum(TL_DATUM);
    epaper.drawString("Scan me!", tx, 110);
    epaper.setFreeFont(&FreeSans18pt7b);
    int y = 165;
    for (const String& l : wrap(label, tw)) {
        if (y > 330) break;
        epaper.drawString(l, tx, y);
        y += epaper.fontHeight() + 6;
    }
    epaper.setFreeFont(&FreeSansBold12pt7b);
    String shown = url;
    shown.replace("https://", "");
    epaper.drawString(fit(shown, tw), tx, 360);
    footer(eventName);
    finish();
}

void showPhoto(const uint8_t* bitmap, const String& caption, const String& frame, const String& downloadQr,
               uint8_t* copyOut) {
    begin();
    String text = caption.isEmpty() ? String("Welcome to PlastiBytes!") : caption;
    // Photo position per frame (the frame then masks around it).
    int px = 0, py = 0;
    if (frame == "hero") px = -180, py = 20;
    if (frame == "film") py = -12;
    epaper.drawBitmap(px, py, bitmap, kPhotoWidth, kPhotoHeight, TFT_BLACK, TFT_WHITE);
    drawFrame(frame, text);
    if (copyOut != nullptr) copyScreen(copyOut);  // the visitor's copy: no QR on it
    if (!downloadQr.isEmpty()) downloadBox(downloadQr);
    finish();
}

void showError(const String& title, const String& detail) {
    begin();
    header(title);
    drawFittedText(detail, kMargin, 90, W - 2 * kMargin, H - 160);
    footer("Reconnecting...");
    finish();
}

void showSetup(const String& apName, const String& qrText) {
    begin();
    header("Display setup");
    qr(qrText, kMargin, 90, 300);
    int tx = kMargin + 330;
    epaper.setTextDatum(TL_DATUM);
    epaper.setFreeFont(&FreeSansBold18pt7b);
    epaper.drawString("1. Scan to join", tx, 100);
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.drawString("or connect your phone to:", tx, 145);
    epaper.setFreeFont(&FreeSansBold12pt7b);
    epaper.drawString(apName, tx, 175);
    epaper.setFreeFont(&FreeSansBold18pt7b);
    epaper.drawString("2. Choose Wi-Fi", tx, 230);
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.drawString("The setup page opens by itself.", tx, 275);
    epaper.drawString("If not, open 192.168.4.1", tx, 305);
    epaper.drawString("Pick the network, type the", tx, 345);
    epaper.drawString("password and the MCP key.", tx, 375);
    footer("Use the same Wi-Fi as Petra");
    finish();
}
