// Bulb settings, saved in flash (NVS) by the setup portal.
// Wi-Fi networks are kept separately by WifiStore (last 5).
#pragma once
#include <Arduino.h>

struct BulbSettings {
    String mcpEndpoint;
    String name;             // spoken name, e.g. "stand light"
    bool relayActiveLow;     // most relay modules switch on with LOW
    uint32_t overrideMin;    // a voice command lasts until nobody is detected for this long
};

BulbSettings loadSettings();
void saveSettings(const BulbSettings& s);
// "stand light" -> "light_stand_light": the MCP tool name Petra sees.
String toolName(const String& name);
