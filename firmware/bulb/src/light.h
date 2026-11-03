// The lamp: presence sensor + relay, with voice/button override.
//
// AUTO: exactly the original sketch, sensor HIGH = lamp on, LOW = off.
// A voice command or a button press overrides the sensor until nobody has been
// detected for the override time (default 30 min), or until "auto" is asked.
// The command itself counts as a detection.
#pragma once
#include <Arduino.h>

enum class LightMode { Auto, ForcedOn, ForcedOff };

void lightBegin(bool relayActiveLow, uint32_t overrideMs);
void lightLoop();                 // call often (non-blocking)
void lightCommand(LightMode m);   // voice or button
void lightToggle();               // button: flip the current state as an override
void lightFlash(int times);       // signal, e.g. "setup hotspot is open"
bool lightIsOn();
LightMode lightMode();
String lightStatus();             // e.g. "on (voice override, sensor resumes when the room is empty)"
