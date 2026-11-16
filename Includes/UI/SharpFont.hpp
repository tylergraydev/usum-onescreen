#pragma once

#include <cstddef>
#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
    // Generated 64 px outline ink, independent of CFNT. No metrics are taken
    // from this font. Missing/blank glyphs return false for runtime fallback.
    static constexpr size_t SharpMaskBytes = 4096;
    bool DecodeSharpGlyph(u32 codePoint, u8 *out, size_t capacity, s32 &w, s32 &h);
}
}
