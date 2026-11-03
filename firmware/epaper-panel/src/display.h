// Screens for the 800x480 black-and-white ePaper. Every call does one full
// refresh (2-5 s) and blocks, so call them only from loop(), never from a
// network callback.
#pragma once
#include <Arduino.h>
#include <vector>

constexpr int kPhotoWidth = 800;
constexpr int kPhotoHeight = 400;
constexpr size_t kPhotoBytes = kPhotoWidth / 8 * kPhotoHeight;  // 40,000
constexpr int kScreenWidth = 800;
constexpr int kScreenHeight = 480;
constexpr size_t kScreenBytes = kScreenWidth / 8 * kScreenHeight;  // 48,000

// Idle rotation screens (welcome + QR codes + event).
enum class IdleScreen { Welcome, Mentorship, Website, Event, Hwivc, WhatsApp, Count };

void displayBegin();
void setEventName(const String& name);  // e.g. "CITS26 - Yaounde"

void showIdle(IdleScreen screen, const String& status);
void showMessage(const String& text);
// verdict: "yes" (big YES badge), "no" (big NO badge) or "" (plain card)
void showCard(const String& title, const std::vector<String>& lines, const String& verdict);
// Numbered steps, for Petra's explanations ("how filament is made").
void showSteps(const String& title, const std::vector<String>& steps);
void showQr(const String& url, const String& label);
// bitmap: kPhotoBytes, 1 = black, MSB first (see shared/protocol.md).
// frame: classic, polaroid, magazine, hero, wanted, comic, film, event.
// downloadQr: Wi-Fi QR text for the download hotspot ("" = no download box).
// copyOut: if not null, receives the finished postcard WITHOUT the download QR
// (kScreenBytes), for the visitor's digital copy.
void showPhoto(const uint8_t* bitmap, const String& caption, const String& frame,
               const String& downloadQr, uint8_t* copyOut);
void showError(const String& title, const String& detail);
void showSetup(const String& apName, const String& qrText);

// Copies the screen as last drawn (1 = black, MSB first, kScreenBytes).
void copyScreen(uint8_t* out);
