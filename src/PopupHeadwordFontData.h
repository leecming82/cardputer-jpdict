#pragma once

#include <cstddef>
#include <cstdint>

namespace popup_headword_font {

constexpr uint8_t kLineHeight = 30;
constexpr uint8_t kBaseline = 23;
constexpr uint8_t kSpaceAdvance = 6;
constexpr uint8_t kMissingAdvance = 24;
constexpr size_t kGlyphCount = 6429;
constexpr size_t kGlyphRecordSize = 12;
constexpr size_t kBitmapDataSize = 1458694;

extern const uint8_t kGlyphRecords[];
extern const uint8_t kBitmapData[];

}  // namespace popup_headword_font
