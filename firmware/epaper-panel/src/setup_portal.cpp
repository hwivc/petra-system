#include "setup_portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WifiStore.h>

#include "display.h"

namespace {

DNSServer dns;
WebServer web(80);
PanelSettings settings;
String apName;
bool restartRequested = false;
uint32_t restartAtMs = 0;
uint32_t startedMs = 0;
constexpr uint32_t kRetrySavedWifiMs = 5 * 60 * 1000;

String htmlEscape(const String& in) {
    String out;
    for (char c : in) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

const char* kStyle =
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;max-width:480px;margin:auto;padding:16px}"
    "h1{font-size:22px}label{display:block;padding:10px;border:1px solid #ccc;border-radius:8px;"
    "margin:6px 0}input[type=text],input[type=password]{width:100%;padding:10px;font-size:16px;"
    "box-sizing:border-box;margin:6px 0 14px}button{width:100%;padding:14px;font-size:18px;"
    "background:#111;color:#fff;border:0;border-radius:8px}small{color:#666}</style>";

void handleRoot() {
    int n = WiFi.scanNetworks();
    String page = String("<!doctype html><html><head><title>PlastiBytes display setup</title>") + kStyle +
                  "</head><body><h1>PlastiBytes display setup</h1>"
                  "<form method=post action=/save><p><b>1. Choose a Wi-Fi network</b> "
                  "<small>(2.4 GHz, same as Petra)</small></p>";
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;
        bool same = ssid == settings.ssid;
        page += "<label><input type=radio name=ssid value=\"" + htmlEscape(ssid) + "\"" +
                (same ? " checked" : "") + "> " + htmlEscape(ssid) + " <small>(" + WiFi.RSSI(i) +
                " dBm" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? ", open" : "") + ")</small></label>";
    }
    WiFi.scanDelete();
    page += "<label><input type=radio name=ssid value=\"__other\"> Other (hidden network):"
            "<input type=text name=other placeholder='Network name'></label>"
            "<p><b>2. Wi-Fi password</b> <small>(leave empty for a remembered network)</small></p>"
            "<input type=password name=pass placeholder='Password'><p><small>Remembered (last 5): " +
            htmlEscape(wifiSavedNames()) + "</small></p>"
            "<p><b>3. xiaozhi.me MCP endpoint</b><br><small>Console &rarr; Agents &rarr; Petra &rarr; "
            "MCP endpoint. Leave empty to keep the current one" +
            String(settings.mcpEndpoint.isEmpty() ? " (none saved yet)" : " (one is saved)") +
            ".</small></p><input type=text name=mcp placeholder='wss://api.xiaozhi.me/mcp/?token=...'>"
            "<p><b>4. Event name</b> <small>(printed on the postcards)</small></p>"
            "<input type=text name=event maxlength=30 value=\"" + htmlEscape(settings.eventName) + "\">"
            "<button type=submit>Save and connect</button></form>"
            "<p><small><a href=/>Rescan networks</a></small></p></body></html>";
    web.send(200, "text/html", page);
}

void handleSave() {
    String ssid = web.arg("ssid");
    if (ssid == "__other") ssid = web.arg("other");
    ssid.trim();
    if (ssid.isEmpty()) {
        web.send(400, "text/html", String(kStyle) + "<p>Please choose a network. <a href=/>Back</a></p>");
        return;
    }
    settings.ssid = ssid;
    settings.pass = web.arg("pass");
    if (settings.pass.isEmpty()) settings.pass = wifiSavedPass(ssid);  // a remembered network
    String mcp = web.arg("mcp");
    mcp.trim();
    if (!mcp.isEmpty()) settings.mcpEndpoint = mcp;
    String event = web.arg("event");
    event.trim();
    if (!event.isEmpty()) settings.eventName = event;
    saveSettings(settings);
    web.send(200, "text/html",
             String(kStyle) + "<h1>Saved!</h1><p>The display restarts and connects to <b>" +
                 htmlEscape(ssid) + "</b>. You can close this page and rejoin your normal Wi-Fi.</p>");
    restartRequested = true;
    restartAtMs = millis() + 2000;  // let the page reach the phone first
}

// Phones probe these URLs; redirecting them opens the setup page automatically.
void handleCaptive() {
    web.sendHeader("Location", "http://192.168.4.1/", true);
    web.send(302, "text/plain", "");
}

}  // namespace

void portalBegin(const PanelSettings& current) {
    settings = current;
    uint64_t mac = ESP.getEfuseMac();
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04X", (unsigned)((mac >> 32) & 0xFFFF));
    apName = String("PlastiBytes-Display-") + suffix;

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);  // STA stays idle, only used to scan networks
    WiFi.softAP(apName.c_str());  // open network, so the QR code is all you need
    startedMs = millis();
    dns.start(53, "*", WiFi.softAPIP());
    web.on("/", handleRoot);
    web.on("/save", HTTP_POST, handleSave);
    web.on("/generate_204", handleCaptive);         // Android
    web.on("/hotspot-detect.html", handleCaptive);  // iPhone / Mac
    web.on("/connecttest.txt", handleCaptive);      // Windows
    web.onNotFound(handleCaptive);
    web.begin();
    Serial.printf("[Setup] hotspot %s, page http://192.168.4.1/\n", apName.c_str());

    showSetup(apName, "WIFI:T:nopass;S:" + apName + ";;");
}

bool portalLoop() {
    dns.processNextRequest();
    web.handleClient();
    if (restartRequested && millis() > restartAtMs) {
        return true;
    }
    // Nobody is setting it up: restart and try the saved Wi-Fi again
    // (e.g. the venue router started later than the panel).
    if (!settings.ssid.isEmpty() && WiFi.softAPgetStationNum() == 0 &&
        millis() - startedMs > kRetrySavedWifiMs) {
        Serial.println("[Setup] no one connected, retrying saved Wi-Fi");
        return true;
    }
    return false;
}
