// Wi-Fi setup portal, like Petra's: the panel opens its own hotspot, shows a
// QR code to join it, and a captive web page lets you pick a Wi-Fi network,
// type the password and paste the xiaozhi.me MCP endpoint.
#pragma once
#include <Arduino.h>
#include "settings.h"

// Starts the hotspot + web page and draws the setup screen.
void portalBegin(const PanelSettings& current);
// Call from loop(). Returns true when the panel should restart
// (settings saved, or nobody connected for 5 min: retry the saved Wi-Fi).
bool portalLoop();
