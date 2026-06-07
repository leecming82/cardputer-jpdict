#include "PopupHeadwordFont.h"

#include <Arduino.h>

namespace popup_headword_font {
namespace {

const uint8_t* glyphRecord(size_t index) {
  return kGlyphRecords + index * kGlyphRecordSize;
}

uint8_t glyphByte(const uint8_t* glyph, size_t offset) {
  return pgm_read_byte(glyph + offset);
}

uint16_t glyphU16(const uint8_t* glyph, size_t offset) {
  return static_cast<uint16_t>(glyphByte(glyph, offset)) |
         (static_cast<uint16_t>(glyphByte(glyph, offset + 1)) << 8);
}

uint32_t glyphU24(const uint8_t* glyph, size_t offset) {
  return static_cast<uint32_t>(glyphByte(glyph, offset)) |
         (static_cast<uint32_t>(glyphByte(glyph, offset + 1)) << 8) |
         (static_cast<uint32_t>(glyphByte(glyph, offset + 2)) << 16);
}

const uint8_t* findGlyph(uint16_t codepoint) {
  size_t lo = 0;
  size_t hi = kGlyphCount;
  while (lo < hi) {
    const size_t mid = lo + ((hi - lo) >> 1);
    const uint16_t midCode = glyphU16(glyphRecord(mid), 0);
    if (midCode < codepoint) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < kGlyphCount && glyphU16(glyphRecord(lo), 0) == codepoint) {
    return glyphRecord(lo);
  }
  return nullptr;
}

uint32_t blendRgb888(uint32_t foreground, uint32_t background, uint8_t alpha) {
  const uint8_t inv = 255 - alpha;
  const uint8_t fr = (foreground >> 16) & 0xFF;
  const uint8_t fg = (foreground >> 8) & 0xFF;
  const uint8_t fb = foreground & 0xFF;
  const uint8_t br = (background >> 16) & 0xFF;
  const uint8_t bg = (background >> 8) & 0xFF;
  const uint8_t bb = background & 0xFF;
  const uint8_t r = (fr * alpha + br * inv + 127) / 255;
  const uint8_t g = (fg * alpha + bg * inv + 127) / 255;
  const uint8_t b = (fb * alpha + bb * inv + 127) / 255;
  return (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) |
         static_cast<uint32_t>(b);
}

class PopupHeadwordFont final : public lgfx::IFont {
 public:
  font_type_t getType() const override {
    return ft_unknown;
  }

  void getDefaultMetric(lgfx::FontMetrics* metrics) const override {
    metrics->width = kMissingAdvance;
    metrics->x_advance = kMissingAdvance;
    metrics->x_offset = 0;
    metrics->height = kLineHeight;
    metrics->y_advance = kLineHeight;
    metrics->y_offset = 0;
    metrics->baseline = kBaseline;
  }

  bool updateFontMetric(lgfx::FontMetrics* metrics, uint16_t codepoint) const override {
    metrics->height = kLineHeight;
    metrics->y_advance = kLineHeight;
    metrics->y_offset = 0;
    metrics->baseline = kBaseline;
    metrics->x_offset = 0;

    if (codepoint == ' ') {
      metrics->width = kSpaceAdvance;
      metrics->x_advance = kSpaceAdvance;
      return true;
    }

    const uint8_t* glyph = findGlyph(codepoint);
    if (!glyph) {
      metrics->width = kMissingAdvance;
      metrics->x_advance = kMissingAdvance;
      return false;
    }

    metrics->width = glyphByte(glyph, 7);
    metrics->x_advance = glyphByte(glyph, 11);
    return true;
  }

  size_t drawChar(lgfx::LGFXBase* gfx, int32_t x, int32_t y, uint16_t codepoint,
                  const lgfx::TextStyle* style, lgfx::FontMetrics* metrics,
                  int32_t& filled_x) const override {
    const int32_t sx = 65536 * style->size_x;
    const int32_t sy = 65536 * style->size_y;
    const int32_t advance = (metrics->x_advance * sx) >> 16;
    const int32_t lineHeight = (metrics->height * sy) >> 16;

    if (style->fore_rgb888 != style->back_rgb888) {
      gfx->writeFillRect(x, y, advance, lineHeight, style->back_rgb888);
      filled_x = x + advance;
    }

    if (codepoint == ' ') {
      return advance;
    }

    const uint8_t* glyph = findGlyph(codepoint);
    if (!glyph) {
      return drawCharDummy(gfx, x, y, advance, lineHeight, style, filled_x);
    }

    const uint32_t offset = glyphU24(glyph, 2);
    const uint16_t length = glyphU16(glyph, 5);
    const uint8_t width = glyphByte(glyph, 7);
    const uint8_t height = glyphByte(glyph, 8);
    const uint8_t xOffset = glyphByte(glyph, 9);
    const uint8_t yOffset = glyphByte(glyph, 10);
    const uint32_t fore = style->fore_rgb888;
    const uint32_t back = style->back_rgb888;
    uint16_t px = 0;
    uint16_t py = 0;

    for (uint16_t i = 0; i < length && py < height; ++i) {
      const uint8_t packed = pgm_read_byte(&kBitmapData[offset + i]);
      uint8_t run = (packed >> 3) + 1;
      const uint8_t a3 = packed & 0x07;
      const uint8_t alpha = (a3 * 255 + 3) / 7;
      while (run-- && py < height) {
        if (alpha != 0) {
          gfx->setColor(alpha == 255 ? fore : blendRgb888(fore, back, alpha));
          gfx->writePixel(x + xOffset + px, y + yOffset + py);
        }
        ++px;
        if (px >= width) {
          px = 0;
          ++py;
        }
      }
    }

    return advance;
  }
};

const PopupHeadwordFont kFont;

}  // namespace

const lgfx::IFont* font() {
  return &kFont;
}

}  // namespace popup_headword_font
