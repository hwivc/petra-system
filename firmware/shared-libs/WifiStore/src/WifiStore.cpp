#include "WifiStore.h"

#include <Preferences.h>
#include <WiFi.h>

namespace {
constexpr const char* kNamespace = "wifi";

void save(const std::vector<WifiNet>& nets) {
    Preferences prefs;
    prefs.begin(kNamespace, false);
    prefs.clear();
    for (size_t i = 0; i < nets.size() && i < kWifiRemember; i++) {
        prefs.putString(("s" + String(i)).c_str(), nets[i].ssid);
        prefs.putString(("p" + String(i)).c_str(), nets[i].pass);
    }
    prefs.end();
}
}  // namespace

std::vector<WifiNet> wifiLoad() {
    std::vector<WifiNet> nets;
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) return nets;  // nothing saved yet
    for (size_t i = 0; i < kWifiRemember; i++) {
        String ssid = prefs.getString(("s" + String(i)).c_str(), "");
        if (ssid.isEmpty()) break;
        nets.push_back({ssid, prefs.getString(("p" + String(i)).c_str(), "")});
    }
    prefs.end();
    return nets;
}

void wifiRemember(const String& ssid, const String& pass) {
    if (ssid.isEmpty()) return;
    std::vector<WifiNet> nets = wifiLoad();
    if (!nets.empty() && nets[0].ssid == ssid && nets[0].pass == pass) return;  // no flash write
    for (size_t i = 0; i < nets.size(); i++) {
        if (nets[i].ssid == ssid) {
            nets.erase(nets.begin() + i);
            break;
        }
    }
    nets.insert(nets.begin(), {ssid, pass});
    if (nets.size() > kWifiRemember) nets.resize(kWifiRemember);
    save(nets);
}

String wifiSavedPass(const String& ssid) {
    for (const WifiNet& n : wifiLoad()) {
        if (n.ssid == ssid) return n.pass;
    }
    return "";
}

bool wifiPickBest(WifiNet& out) {
    std::vector<WifiNet> nets = wifiLoad();
    if (nets.empty()) return false;
    out = nets[0];
    int bestRssi = -1000;
    int found = WiFi.scanComplete();
    for (int i = 0; i < found; i++) {
        for (const WifiNet& n : nets) {
            if (WiFi.SSID(i) == n.ssid && WiFi.RSSI(i) > bestRssi) {
                bestRssi = WiFi.RSSI(i);
                out = n;
            }
        }
    }
    return true;
}

String wifiSavedNames() {
    String names;
    for (const WifiNet& n : wifiLoad()) {
        if (!names.isEmpty()) names += ", ";
        names += n.ssid;
    }
    return names;
}
