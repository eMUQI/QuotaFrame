#pragma once
#include <cstdint>
struct Glyph { uint32_t offset; int16_t width, height, left, top, advance; uint8_t character; };
struct Font { const uint8_t* bitmap; const Glyph* glyphs; int count, size, family; };
extern const Font fonts[];
extern const int font_count;
