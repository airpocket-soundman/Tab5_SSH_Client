#pragma once

#include <Arduino.h>
#include <M5GFX.h>

namespace TerminusBitmap {

struct Font {
    uint8_t srcW;
    uint8_t srcH;
    uint8_t targetW;
    uint8_t targetH;
    uint8_t rowBytes;
    uint16_t glyphCount;
    const uint16_t* codepoints;
    const uint32_t* offsets;
    const uint8_t* bitmap;
};

const Font& fontForHeight(uint8_t targetHeight);
bool hasGlyph(const Font& font, uint32_t cp);
bool drawGlyph(M5Canvas& canvas, const Font& font, uint32_t cp, int x, int y, uint16_t fg);

} // namespace TerminusBitmap
