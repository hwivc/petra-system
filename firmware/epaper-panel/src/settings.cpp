#include "settings.h"

#include <Preferences.h>
#include <WifiStore.h>

// Optional build-time defaults (include/secrets.h), used until the portal saves values.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef MCP_ENDPOINT
#define MCP_ENDPOINT ""
#endif

PanelSettings loadSettings() {
    Preferences prefs;
    prefs.begin("panel", true);
    PanelSettings s;
    s.ssid = prefs.getString("ssid", WIFI_SSID);
    s.pass = prefs.getString("pass", WIFI_PASS);
    s.mcpEndpoint = prefs.getString("mcp", MCP_ENDPOINT);
    s.eventName = prefs.getString("event", "CITS26 - Yaounde");
    prefs.end();
    // Ignore the untouched placeholders from secrets.example.h
    if (s.ssid == "your-2.4GHz-wifi") s.ssid = "";
    if (s.mcpEndpoint.indexOf("PASTE_YOUR_TOKEN") >= 0) s.mcpEndpoint = "";
    // Wi-Fi now lives in WifiStore (last 5 networks); move the old single network over once.
    std::vector<WifiNet> nets = wifiLoad();
    if (nets.empty() && !s.ssid.isEmpty()) {
        wifiRemember(s.ssid, s.pass);
    } else if (!nets.empty()) {
        s.ssid = nets[0].ssid;
        s.pass = nets[0].pass;
    }
    return s;
}

void saveSettings(const PanelSettings& s) {
    Preferences prefs;
    prefs.begin("panel", false);
    prefs.putString("mcp", s.mcpEndpoint);
    prefs.putString("event", s.eventName);
    prefs.end();
    wifiRemember(s.ssid, s.pass);
}
