#pragma once

namespace OneScreen
{
namespace UI
{
    struct LayerRun
    {
        unsigned a = 0, b = 0; // Half-open rows in memory order.
    };

    struct LayerRuns
    {
        LayerRun pieces[2];
    };

    // RGBA8 TextureCopy: 8-byte start, 16-byte length (2 and 4 pixels).
    // Round each surviving piece inward, never across an excluded row.
    inline LayerRun RoundLayerRun(unsigned a, unsigned b)
    {
        a = (a + 1) & ~1u;
        return { a, b > a ? a + ((b - a) & ~3u) : a };
    }

    // Inputs are memory-order rows within one column, already clipped to it.
    // An empty exclusion means no skip. Split BEFORE rounding each piece.
    inline LayerRuns SplitLayerRun(unsigned a, unsigned b, unsigned skipA, unsigned skipB)
    {
        if (skipB <= skipA || skipB <= a || skipA >= b)
            return { { RoundLayerRun(a, b), {} } };
        return { { RoundLayerRun(a, skipA > a ? skipA : a),
                   RoundLayerRun(skipB < b ? skipB : b, b) } };
    }
}
}
