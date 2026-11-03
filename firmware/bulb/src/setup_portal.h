// Wi-Fi setup portal, like the ePaper panel's: the bulb opens the open hotspot
// PlastiBytes-Bulb-XXXX and a captive page to pick Wi-Fi, paste the MCP
// endpoint and name the lamp. The lamp keeps working on its sensor meanwhile.
#pragma once
#include <Arduino.h>
#include "settings.h"

void portalBegin(const BulbSettings& current);
// Call from loop(). Returns true when setup is over (settings saved, or nobody
// connected for 5 min): the caller then calls portalEnd() and retries Wi-Fi.
bool portalLoop();
void portalEnd();
// True if new settings were saved (name / relay / delay may have changed).
bool portalSaved();
