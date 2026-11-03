// Digital copy of a visitor's postcard: after a photo is shown, the panel
// opens a short-lived open hotspot. Scanning the QR on the screen joins it,
// a page pops up with the photo and a download button, and the hotspot
// closes after the download (or after 3 minutes). The panel stays on the
// venue Wi-Fi the whole time. Nothing is stored after the hotspot closes.
#pragma once
#include <Arduino.h>
#include <WiFi.h>

String downloadApName();
// Allocates the copy buffer and returns it (kScreenBytes), or nullptr if there
// is not enough memory (then show the photo without the download QR).
uint8_t* downloadPrepare();
// Opens the hotspot for the postcard stored in the buffer.
void downloadOpen();
void downloadClose();
bool downloadActive();
// Call from loop(): DNS for the captive page, closes the hotspot when done.
void downloadLoop();
// Handles a request that arrived on the hotspot. Returns true if handled.
bool downloadHandle(WiFiClient& client, const String& requestLine);
