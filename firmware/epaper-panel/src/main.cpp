// PlastiBytes ePaper panel: works on its own, talks to xiaozhi.me (MCP tools)
// and receives photos from Petra over local Wi-Fi (see shared/protocol.md).
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <WebSocketMCP.h>
#include <WifiStore.h>

#include "display.h"
#include "download.h"
#include "settings.h"
#include "setup_portal.h"

namespace {

constexpr uint32_t kIdleAfterMs = 2 * 60 * 1000;      // back to the welcome rotation after 2 min
constexpr uint32_t kIdleRotateMs = 90 * 1000;         // next idle screen every 90 s
constexpr uint32_t kWifiLostErrorMs = 60 * 1000;      // error screen after 60 s offline
constexpr uint32_t kHttpTimeoutMs = 5000;
constexpr uint32_t kWifiConnectMs = 30 * 1000;        // then open the setup hotspot
constexpr uint32_t kSetupButtonWindowMs = 3000;       // press BOOT now to force setup
constexpr int kBootButtonPin = 9;                     // XIAO ESP32-C3 BOOT button

WebSocketMCP mcp;
WiFiServer http(80);
bool setupMode = false;

// One pending screen at a time (latest request wins). Rendering blocks for a
// few seconds, so callbacks only fill this in and loop() draws it.
enum class JobType { None, Idle, Message, Card, Steps, Qr, Photo, Error };
struct Job {
    JobType type = JobType::None;
    String text, title, verdict;
    std::vector<String> lines;
};
Job pending;
uint8_t photoBuffer[kPhotoBytes];
String photoCaption, photoFrame;

bool showingIdle = false;
int idleIndex = 0;
uint32_t lastContentMs = 0;
uint32_t wifiLostSinceMs = 0;
bool errorShown = false;
bool mcpConnected = false;

String statusText() {
    if (WiFi.status() != WL_CONNECTED) return "Wi-Fi: connecting...";
    return String(mcpConnected ? "Online" : "Petra link: connecting") + "  " + WiFi.localIP().toString();
}

std::vector<String> stringList(JsonVariant v, size_t max) {
    std::vector<String> out;
    for (JsonVariant item : v.as<JsonArray>()) {
        if (out.size() < max) out.push_back(item.as<String>());
    }
    return out;
}

// ---------------------------------------------------------------- MCP tools
WebSocketMCP::ToolResponse ok(const String& what) {
    return WebSocketMCP::ToolResponse("{\"success\":true,\"result\":\"" + what + "\"}");
}

void registerTools() {
    mcp.registerTool(
        "show_message",
        "Show a short text message on the large black-and-white ePaper display at the PlastiBytes "
        "stand (800x480). Use it when the visitor asks to write or display something. The screen "
        "takes about 3 seconds to refresh.",
        "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\",\"description\":"
        "\"Message to show, max about 150 characters\"}},\"required\":[\"text\"]}",
        [](const String& args) {
            JsonDocument doc;
            deserializeJson(doc, args);
            pending = Job{};
            pending.type = JobType::Message;
            pending.text = doc["text"] | "";
            return ok("Message will appear on the display in a few seconds");
        });

    mcp.registerTool(
        "show_card",
        "Show an information card on the ePaper display: a title, up to 4 short bullet lines and "
        "an optional big YES/NO badge. Use it (1) after looking at an object with the camera, to "
        "show whether it can be recycled into 3D-printing filament, e.g. title 'PET bottle "
        "(type 1)', lines ['Clear drinks bottle', 'Shred, wash, extrude', 'Becomes filament'], "
        "verdict 'yes'; and (2) while explaining a topic, to show its 3 key points (verdict "
        "'none').",
        "{\"type\":\"object\",\"properties\":{"
        "\"title\":{\"type\":\"string\",\"description\":\"Short title, max 25 characters\"},"
        "\"lines\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Up to 4 short lines\"},"
        "\"verdict\":{\"type\":\"string\",\"enum\":[\"yes\",\"no\",\"none\"],\"description\":"
        "\"yes = recyclable into filament, no = not recyclable into filament, none = no badge\"}},"
        "\"required\":[\"title\",\"lines\"]}",
        [](const String& args) {
            JsonDocument doc;
            deserializeJson(doc, args);
            pending = Job{};
            pending.type = JobType::Card;
            pending.title = doc["title"] | "";
            pending.verdict = doc["verdict"] | "none";
            pending.lines = stringList(doc["lines"], 4);
            return ok("Card will appear on the display in a few seconds");
        });

    mcp.registerTool(
        "show_steps",
        "Show numbered steps on the ePaper display (up to 5, each a few words). Use it when "
        "explaining a process, e.g. 'How we make filament': ['Collect bottles', 'Wash and "
        "shred', 'Melt and extrude', 'Wind on spools', '3D print in class']. Call it before "
        "you start explaining, then talk through the steps.",
        "{\"type\":\"object\",\"properties\":{"
        "\"title\":{\"type\":\"string\",\"description\":\"Short title, max 25 characters\"},"
        "\"steps\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"2 to 5 short steps\"}},"
        "\"required\":[\"title\",\"steps\"]}",
        [](const String& args) {
            JsonDocument doc;
            deserializeJson(doc, args);
            pending = Job{};
            pending.type = JobType::Steps;
            pending.title = doc["title"] | "";
            pending.lines = stringList(doc["steps"], 5);
            return ok("Steps will appear on the display in a few seconds");
        });

    mcp.registerTool(
        "show_qr",
        "Show a big QR code with a short label on the ePaper display, so visitors can scan it "
        "with their phone. Use it when a visitor wants to learn more or sign up, e.g. "
        "https://plastibytes.com/mentorship (mentorship programme), https://plastibytes.com "
        "(website), https://plastibytes.com/blog (stories), https://hwivc.org (Hardware "
        "Innovation Valley Community), https://citscm.com (CITS26 summit), "
        "https://chat.whatsapp.com/I8a9PIMVv76Am17iXg2b9Y (PlastiBytes WhatsApp community).",
        "{\"type\":\"object\",\"properties\":{"
        "\"url\":{\"type\":\"string\",\"description\":\"Full https:// link\"},"
        "\"label\":{\"type\":\"string\",\"description\":\"One short sentence shown next to the QR code\"}},"
        "\"required\":[\"url\",\"label\"]}",
        [](const String& args) {
            JsonDocument doc;
            deserializeJson(doc, args);
            pending = Job{};
            pending.type = JobType::Qr;
            pending.text = doc["url"] | "https://plastibytes.com";
            pending.title = doc["label"] | "";
            return ok("QR code will appear on the display in a few seconds");
        });

    mcp.registerTool(
        "clear_display",
        "Clear the ePaper display and go back to the PlastiBytes welcome screens.",
        "{\"type\":\"object\",\"properties\":{}}",
        [](const String&) {
            pending = Job{};
            pending.type = JobType::Idle;
            return ok("Display cleared");
        });
}

// ------------------------------------------------- local HTTP (Petra photos)
String urlDecode(const String& in) {
    String out;
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < in.length()) {
            out += (char)strtol(in.substring(i + 1, i + 3).c_str(), nullptr, 16);
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

void reply(WiFiClient& client, int code, const char* status, const String& body,
           const char* type = "text/plain") {
    client.printf("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                  code, status, type, (unsigned)body.length());
    client.print(body);
}

bool readLine(WiFiClient& client, String& line, uint32_t deadline) {
    line = "";
    while (millis() < deadline) {
        while (client.available()) {
            char c = client.read();
            if (c == '\n') return true;
            if (c != '\r') line += c;
        }
        if (!client.connected()) return false;
        delay(1);
    }
    return false;
}

void handleHttp() {
    WiFiClient client = http.accept();
    if (!client) return;
    uint32_t deadline = millis() + kHttpTimeoutMs;

    String requestLine, line, caption, frame;
    size_t contentLength = 0;
    if (!readLine(client, requestLine, deadline)) return;
    while (readLine(client, line, deadline) && !line.isEmpty()) {
        int colon = line.indexOf(':');
        if (colon < 0) continue;
        String name = line.substring(0, colon);
        String value = line.substring(colon + 1);
        value.trim();
        name.toLowerCase();
        if (name == "content-length") contentLength = value.toInt();
        if (name == "x-caption") caption = urlDecode(value).substring(0, 80);
        if (name == "x-frame") frame = value;
    }

    // Phones on the download hotspot get the postcard page.
    if (downloadActive() && client.localIP() == WiFi.softAPIP() && downloadHandle(client, requestLine)) {
        client.stop();
        return;
    }

    if (requestLine.startsWith("GET /status")) {
        bool busy = pending.type != JobType::None;
        reply(client, 200, "OK", String("{\"ok\":true,\"busy\":") + (busy ? "true" : "false") + "}",
              "application/json");
    } else if (requestLine.startsWith("POST /photo")) {
        if (pending.type == JobType::Photo) {
            reply(client, 503, "Busy", "busy");
        } else if (contentLength != kPhotoBytes) {
            reply(client, 400, "Bad Request", "bad size");
        } else {
            size_t got = 0;
            while (got < kPhotoBytes && millis() < deadline) {
                int n = client.read(photoBuffer + got, kPhotoBytes - got);
                if (n > 0) {
                    got += n;
                } else {
                    if (!client.connected()) break;
                    delay(1);
                }
            }
            if (got != kPhotoBytes) {
                reply(client, 400, "Bad Request", "bad size");
            } else {
                photoCaption = caption;
                photoFrame = frame.isEmpty() ? String("classic") : frame;
                pending = Job{};
                pending.type = JobType::Photo;
                reply(client, 200, "OK", "ok");
                Serial.printf("[HTTP] photo received, frame %s, caption \"%s\"\n", photoFrame.c_str(),
                              caption.c_str());
            }
        }
    } else {
        reply(client, 404, "Not Found", "not found");
    }
    client.stop();
}

// ----------------------------------------------------------------- render
void render() {
    if (pending.type == JobType::None) return;
    Job job = pending;  // copy: callbacks may set a new job meanwhile
    pending = Job{};
    Serial.printf("[EPD] render job %d\n", (int)job.type);

    if (downloadActive() && job.type != JobType::Photo) {
        downloadClose();  // the postcard is no longer on screen
    }
    switch (job.type) {
        case JobType::Idle:
            showIdle(static_cast<IdleScreen>(idleIndex), statusText());
            break;
        case JobType::Message: showMessage(job.text); break;
        case JobType::Card:    showCard(job.title, job.lines, job.verdict); break;
        case JobType::Steps:   showSteps(job.title, job.lines); break;
        case JobType::Qr:      showQr(job.text, job.title); break;
        case JobType::Error:   showError(job.title, job.text); break;
        case JobType::Photo: {
            uint8_t* copy = downloadPrepare();
            showPhoto(photoBuffer, photoCaption, photoFrame,
                      copy ? "WIFI:T:nopass;S:" + downloadApName() + ";;" : String(), copy);
            if (copy) downloadOpen();
            break;
        }
        case JobType::None: break;
    }
    showingIdle = job.type == JobType::Idle;
    lastContentMs = millis();
}

void onMcpConnection(bool connected) {
    mcpConnected = connected;
    Serial.printf("[MCP] %s\n", connected ? "connected" : "disconnected");
    if (connected) registerTools();
}

// True if BOOT is pressed during the first seconds after power-on.
// (Holding it *while* plugging in would start the chip's download mode instead.)
bool setupButtonPressed() {
    pinMode(kBootButtonPin, INPUT_PULLUP);
    uint32_t start = millis();
    while (millis() - start < kSetupButtonWindowMs) {
        if (digitalRead(kBootButtonPin) == LOW) return true;
        delay(20);
    }
    return false;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n== PlastiBytes ePaper panel ==");
    displayBegin();
    PanelSettings settings = loadSettings();
    setEventName(settings.eventName);

    Serial.println("[Setup] press BOOT now to change Wi-Fi / MCP key");
    if (setupButtonPressed() || settings.ssid.isEmpty()) {
        setupMode = true;
        portalBegin(settings);
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    // Join the strongest of the last 5 networks, so moving between places needs no setup.
    WiFi.scanNetworks();
    WifiNet best;
    wifiPickBest(best);
    WiFi.scanDelete();
    Serial.printf("[WiFi] connecting to \"%s\"\n", best.ssid.c_str());
    WiFi.begin(best.ssid.c_str(), best.pass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectMs) delay(250);
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] no remembered network found, opening setup");
        setupMode = true;
        portalBegin(settings);
        return;
    }
    Serial.printf("[WiFi] connected %s\n", WiFi.localIP().toString().c_str());
    wifiRemember(WiFi.SSID(), WiFi.psk());  // most recent first

    if (MDNS.begin("epaper")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[mDNS] epaper.local");
    }
    http.begin();
    if (settings.mcpEndpoint.isEmpty()) {
        Serial.println("[MCP] no endpoint saved: photos from Petra work, AI tools do not");
    } else {
        mcp.begin(settings.mcpEndpoint.c_str(), onMcpConnection);
    }

    pending.type = JobType::Idle;
}

void loop() {
    if (setupMode) {
        if (portalLoop()) ESP.restart();
        delay(2);
        return;
    }
    mcp.loop();
    handleHttp();
    downloadLoop();

    // Wi-Fi watchdog: error screen after 60 s offline, idle screen when back.
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiLostSinceMs == 0) wifiLostSinceMs = millis();
        if (!errorShown && millis() - wifiLostSinceMs > kWifiLostErrorMs) {
            pending = Job{};
            pending.type = JobType::Error;
            pending.title = "Wi-Fi lost";
            pending.text = "The display is offline. It will reconnect by itself. "
                           "To change Wi-Fi, restart it and press BOOT within 3 seconds.";
            errorShown = true;
        }
    } else {
        wifiLostSinceMs = 0;
        if (errorShown) {
            errorShown = false;
            pending.type = JobType::Idle;
        }
    }

    // Idle: after content, go back to the welcome rotation; rotate every 90 s.
    // Never while a visitor may still be downloading their postcard.
    if (pending.type == JobType::None && !errorShown && !downloadActive()) {
        uint32_t wait = showingIdle ? kIdleRotateMs : kIdleAfterMs;
        if (millis() - lastContentMs > wait) {
            idleIndex = showingIdle ? (idleIndex + 1) % (int)IdleScreen::Count : 0;
            pending.type = JobType::Idle;
        }
    }

    render();
    delay(5);
}
