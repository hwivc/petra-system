// Wi-Fi and MCP endpoint, saved in flash (NVS) by the setup portal.
#pragma once
#include <Arduino.h>

struct PanelSettings {
    String ssid;
    String pass;
    String mcpEndpoint;
    String eventName;  // shown on postcards, e.g. "CITS26 - Yaounde"
};

PanelSettings loadSettings();
void saveSettings(const PanelSettings& s);
