#include "light.h"

namespace {

constexpr int kSensorPin = D2;
constexpr int kRelayPin = D3;
constexpr uint32_t kSampleMs = 100;  // same as the original sketch

bool activeLow = false;
uint32_t overrideMs = 30 * 60 * 1000;

LightMode mode = LightMode::Auto;
bool relayOn = false;
bool presence = false;
uint32_t lastSampleMs = 0;
uint32_t lastPresenceMs = 0;  // last detection, or last voice/button command

void writeRelay(bool on) {
    digitalWrite(kRelayPin, (on != activeLow) ? HIGH : LOW);
}

const char* modeName(LightMode m) {
    switch (m) {
        case LightMode::ForcedOn:  return "forced ON";
        case LightMode::ForcedOff: return "forced OFF";
        default:                   return "AUTO";
    }
}

}  // namespace

void lightBegin(bool relayActiveLow, uint32_t overrideMsIn) {
    activeLow = relayActiveLow;
    overrideMs = overrideMsIn;
    writeRelay(false);  // set the OFF level before the pin drives, so the lamp never blinks at boot
    pinMode(kRelayPin, OUTPUT);
    writeRelay(false);
    pinMode(kSensorPin, INPUT);  // like the original sketch: the sensor drives the pin
    Serial.printf("[Light] relay active %s, voice command lasts %lu min after the last detection\n",
                  activeLow ? "LOW" : "HIGH", overrideMs / 60000);
}

void lightLoop() {
    uint32_t now = millis();
    if (now - lastSampleMs < kSampleMs) return;
    lastSampleMs = now;

    // AUTO is the original sketch: sensor HIGH = lamp on, LOW = lamp off.
    // (The sensor's own time knob sets how long it stays HIGH.)
    bool raw = digitalRead(kSensorPin) == HIGH;
    if (raw != presence) {
        presence = raw;
        Serial.printf("[Sensor] %s\n", presence ? "presence" : "clear");
    }
    if (presence) lastPresenceMs = now;

    // A voice/button command holds until nobody has been detected for the override time.
    if (mode != LightMode::Auto && now - lastPresenceMs > overrideMs) {
        Serial.println("[Light] nobody detected for a while: back to AUTO");
        mode = LightMode::Auto;
    }

    bool want = mode == LightMode::ForcedOn || (mode == LightMode::Auto && presence);
    if (want != relayOn) {
        relayOn = want;
        writeRelay(relayOn);
        Serial.printf("[Light] %s (%s)\n", relayOn ? "ON" : "OFF", modeName(mode));
    }
}

void lightCommand(LightMode m) {
    mode = m;
    lastPresenceMs = millis();  // someone just gave the command: count it as presence
    Serial.printf("[Light] command: %s\n", modeName(m));
    lastSampleMs = 0;
    lightLoop();
}

void lightToggle() {
    lightCommand(relayOn ? LightMode::ForcedOff : LightMode::ForcedOn);
}

void lightFlash(int times) {
    for (int i = 0; i < times; i++) {
        writeRelay(!relayOn);
        delay(500);
        writeRelay(relayOn);
        delay(500);
    }
}

bool lightIsOn() { return relayOn; }
LightMode lightMode() { return mode; }

String lightStatus() {
    String s = relayOn ? "on" : "off";
    if (mode == LightMode::Auto) {
        s += " (motion sensor mode)";
    } else {
        s += " (manual override; the motion sensor takes over again after nobody has been detected for " +
             String(overrideMs / 60000) + " minutes, or when asked to go back to auto)";
    }
    return s;
}
