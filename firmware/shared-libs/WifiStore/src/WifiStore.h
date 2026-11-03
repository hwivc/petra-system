// Remembers the last 5 Wi-Fi networks (flash/NVS), so a device moved back to
// a place it has been before reconnects by itself, without the setup portal.
#pragma once
#include <Arduino.h>
#include <vector>

struct WifiNet {
    String ssid;
    String pass;
};

constexpr size_t kWifiRemember = 5;

// Saved networks, most recently used first.
std::vector<WifiNet> wifiLoad();
// Adds or updates a network and moves it to the front (keeps the last 5).
void wifiRemember(const String& ssid, const String& pass);
// Saved password for this network, or "" if unknown.
String wifiSavedPass(const String& ssid);
// After a WiFi.scanNetworks(): the saved network with the strongest signal.
// If none is visible (e.g. hidden network) returns the most recent one.
// Returns false only when nothing is saved.
bool wifiPickBest(WifiNet& out);
// Comma-separated list of saved network names, for the setup page.
String wifiSavedNames();
