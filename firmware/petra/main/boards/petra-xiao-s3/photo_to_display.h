#ifndef _PHOTO_TO_DISPLAY_H_
#define _PHOTO_TO_DISPLAY_H_

#include <cstdint>
#include <string>

// Turns a camera frame into an 800x400 1-bit postcard in an artistic style and
// sends it to the ePaper panel over local Wi-Fi (POST http://epaper.local/photo,
// see shared/protocol.md). The panel adds the frame and caption. Nothing goes
// to the cloud.

// Styles: photo, sketch, comic, woodcut, engraving, stipple ("surprise" = random).
// Frames (drawn by the panel): classic, polaroid, magazine, hero, wanted, comic,
// film, event ("surprise" = random).
extern const char* const kPhotoStyles;  // for tool descriptions
extern const char* const kPhotoFrames;

std::string ResolveStyle(const std::string& style);  // validates, picks for "surprise"
std::string ResolveFrame(const std::string& frame);

// luma: 8-bit grayscale frame (width x height), e.g. from PetraCamera::CaptureLuma().
// Returns a short sentence for the AI to tell the visitor what happened.
std::string SendPhotoToDisplay(const uint8_t* luma, int width, int height, const std::string& caption,
                               const std::string& style, const std::string& frame);

#endif  // _PHOTO_TO_DISPLAY_H_
