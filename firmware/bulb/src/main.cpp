// PlastiBytes smart bulb: XIAO ESP32-C3 + presence sensor (D2) + relay (D3).
// The motion sensor runs the lamp on its own, also without Wi-Fi. Petra can
// switch it on/off or back to sensor mode through one MCP tool on xiaozhi.me.
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WebSocketMCP.h>
#include <WifiStore.h>

#include "light.h"
#include "settings.h"
#include "setup_portal.h"

namespace {

constexpr int kBootButtonPin = 9;                 // XIAO ESP32-C3 BOOT button
constexpr uint32_t kWifiConnectMs = 30 * 1000;    // then open the setup hotspot
constexpr uint32_t kLongPressMs = 5000;           // hold BOOT 5 s: open setup
constexpr uint32_t kButtonDebounceMs = 40;

enum class Net { Scanning, Joining, Online, Portal };
Net net = Net::Scanning;
uint32_t netStartMs = 0;

BulbSettings settings;
WebSocketMCP mcp;
bool mcpStarted = false;
String tool;

// ---------------------------------------------------------------- network
void startConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.scanNetworks(true);  // async: the lamp keeps working meanwhile
    net = Net::Scanning;
    netStartMs = millis();
}

void openPortal(const char* why) {
    Serial.printf("[Setup] %s: opening setup hotspot\n", why);
    lightFlash(2);  // the bulb has no screen: 2 blinks = "setup hotspot open"
    portalBegin(settings);
    net = Net::Portal;
}

WebSocketMCP::ToolResponse result(const String& text) {
    JsonDocument doc;
    doc["success"] = true;
    doc["result"] = text;
    String out;
    serializeJson(doc, out);
    return WebSocketMCP::ToolResponse(out);
}

void registerTools() {
    mcp.registerTool(
        tool,
        "Control the lamp called '" + settings.name +
            "' at the PlastiBytes stand (a mains lamp: on or off only, no dimming). "
            "state 'on' or 'off' switches it now and overrides its motion sensor until the room has been "
            "empty for a while; 'auto' gives control back to the motion sensor; 'status' only reports. "
            "When the user asks about all lights, call this for every light tool.",
        "{\"type\":\"object\",\"properties\":{\"state\":{\"type\":\"string\","
        "\"enum\":[\"on\",\"off\",\"auto\",\"status\"]}},\"required\":[\"state\"]}",
        [](const String& args) {
            JsonDocument doc;
            deserializeJson(doc, args);
            String state = doc["state"] | "status";
            if (state == "on") lightCommand(LightMode::ForcedOn);
            else if (state == "off") lightCommand(LightMode::ForcedOff);
            else if (state == "auto") lightCommand(LightMode::Auto);
            return result("The " + settings.name + " is now " + lightStatus());
        });
}

void onMcpConnection(bool connected) {
    Serial.printf("[MCP] %s\n", connected ? "connected" : "disconnected");
    if (connected) registerTools();
}

void networkLoop() {
    switch (net) {
        case Net::Scanning: {
            int n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING) break;
            WifiNet best;
            wifiPickBest(best);
            WiFi.scanDelete();
            Serial.printf("[WiFi] connecting to \"%s\"\n", best.ssid.c_str());
            WiFi.begin(best.ssid.c_str(), best.pass.c_str());
            net = Net::Joining;
            break;
        }
        case Net::Joining:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFi] connected %s\n", WiFi.localIP().toString().c_str());
                wifiRemember(WiFi.SSID(), WiFi.psk());  // most recent first
                net = Net::Online;
                if (settings.mcpEndpoint.isEmpty()) {
                    Serial.println("[MCP] no endpoint saved: Petra cannot control this lamp yet");
                } else if (!mcpStarted) {
                    mcpStarted = true;
                    mcp.begin(settings.mcpEndpoint.c_str(), onMcpConnection);
                }
            } else if (millis() - netStartMs > kWifiConnectMs) {
                openPortal("no saved Wi-Fi found");
            }
            break;
        case Net::Online:
            if (mcpStarted) mcp.loop();  // Wi-Fi drops: auto-reconnect, the lamp keeps working
            break;
        case Net::Portal:
            if (portalLoop()) {
                if (portalSaved()) ESP.restart();  // new name / relay type: start clean
                portalEnd();
                startConnect();
            }
            break;
    }
}

// ----------------------------------------------------------------- button
// Short press: toggle the lamp (an override, like a voice command).
// Hold 5 s: open the setup hotspot (the lamp blinks twice).
void buttonLoop() {
    static bool down = false;
    static uint32_t changedMs = 0, pressedMs = 0;
    static bool longDone = false;
    bool now = digitalRead(kBootButtonPin) == LOW;
    uint32_t t = millis();
    if (now != down && t - changedMs > kButtonDebounceMs) {
        down = now;
        changedMs = t;
        if (down) {
            pressedMs = t;
            longDone = false;
        } else if (!longDone) {
            lightToggle();
        }
    }
    if (down && !longDone && t - pressedMs > kLongPressMs) {
        longDone = true;
        if (net != Net::Portal) {
            if (mcpStarted) mcp.disconnect();  // reconnects by itself when back online
            openPortal("BOOT held");
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    settings = loadSettings();
    lightBegin(settings.relayActiveLow, settings.overrideMin * 60 * 1000);  // first: relay must start OFF
    delay(500);
    tool = toolName(settings.name);
    Serial.printf("\n== PlastiBytes bulb \"%s\" (tool %s) ==\n", settings.name.c_str(), tool.c_str());
    pinMode(kBootButtonPin, INPUT_PULLUP);

    if (wifiLoad().empty()) {
        openPortal("no Wi-Fi saved");
    } else {
        startConnect();
    }
}

void loop() {
    lightLoop();
    buttonLoop();
    networkLoop();
    delay(5);
}
