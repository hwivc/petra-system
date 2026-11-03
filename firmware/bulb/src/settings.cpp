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

BulbSettings loadSettings() {
    Preferences prefs;
    prefs.begin("bulb", true);
    BulbSettings s;
    s.mcpEndpoint = prefs.getString("mcp", MCP_ENDPOINT);
    s.name = prefs.getString("name", "stand light");
    s.relayActiveLow = prefs.getBool("lowrelay", false);
    s.overrideMin = prefs.getUInt("holdmin", 30);
    prefs.end();
    if (s.mcpEndpoint.indexOf("PASTE_YOUR_TOKEN") >= 0) s.mcpEndpoint = "";
    String ssid = WIFI_SSID;
    if (wifiLoad().empty() && !ssid.isEmpty() && ssid != "your-2.4GHz-wifi") {
        wifiRemember(ssid, WIFI_PASS);
    }
    return s;
}

void saveSettings(const BulbSettings& s) {
    Preferences prefs;
    prefs.begin("bulb", false);
    prefs.putString("mcp", s.mcpEndpoint);
    prefs.putString("name", s.name);
    prefs.putBool("lowrelay", s.relayActiveLow);
    prefs.putUInt("holdmin", s.overrideMin);
    prefs.end();
}

String toolName(const String& name) {
    String out = "light_";
    bool lastUnderscore = true;
    for (char c : name) {
        if (isalnum((unsigned char)c)) {
            out += (char)tolower(c);
            lastUnderscore = false;
        } else if (!lastUnderscore) {
            out += '_';
            lastUnderscore = true;
        }
    }
    while (out.endsWith("_")) out.remove(out.length() - 1);
    if (out == "light") out = "light_bulb";
    return out.substring(0, 40);
}
