#pragma once

#include <cmath>

namespace OneScreen
{
namespace UI
{
    // Destination -> unsqueezed screen column, nearest neighbour about x=200.
    // Clamp before converting to int: even extreme finite aspects are safe.
    // -1/400 are out-of-screen sentinels, never valid layer columns.
    inline int SourceColumn(int destination, float squeeze)
    {
        if (!(squeeze > 1.0f))
            return destination;
        const double source = 200.0 + (destination - 200) * (double)squeeze;
        if (source < -0.5) return -1;
        if (source >= 399.5) return 400;
        return (int)std::floor(source + 0.5);
    }
}
}
