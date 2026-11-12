// Shared production presets/geometry; independent source-over pixel reference.
#include "Settings.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace OneScreen;

static unsigned Blend(unsigned dst, unsigned src, unsigned alpha)
{
    return (src * alpha + dst * (255 - alpha) + 127) / 255;
}

int main()
{
    std::vector<std::string> warnings;
    assert(Settings{}.filter == ScreenFilter::Off);
    const auto defaults = ParseSettings(DefaultSettingsText(), warnings);
    assert(warnings.empty() && defaults.filter == ScreenFilter::Off);
    for (auto filter : {ScreenFilter::Off, ScreenFilter::Warm, ScreenFilter::Cool, ScreenFilter::Vignette})
    {
        const std::string name = ScreenFilterName(filter);
        const auto s = ParseSettings("[display]\nfilter = " + name, warnings);
        assert(warnings.empty() && s.filter == filter);
        assert(DescribeSettings(s).find("filter " + name) != std::string::npos);
        SetSettings(s);
        assert(GetScreenFilter() == filter);
        ScreenFilter live = ScreenFilter::Off;
        assert(ParseScreenFilter(" \t" + name + "\r\n", live) && live == filter);
    }
    ScreenFilter unchanged = ScreenFilter::Cool;
    assert(ParseScreenFilter("WaRm", unchanged) && unchanged == ScreenFilter::Warm);
    for (const auto bad : {"", "vivid", "sharpen", "bloom", "warm extra"})
        assert(!ParseScreenFilter(bad, unchanged) && unchanged == ScreenFilter::Warm);
    warnings.clear();
    const auto invalid = ParseSettings("[display]\nfilter = cool\nfilter = vivid", warnings);
    assert(warnings.size() == 1 && invalid.filter == ScreenFilter::Cool);
    warnings.clear();
    assert(ParseSettings("[display]\nfilter = nonsense", warnings).filter == ScreenFilter::Off);
    assert(warnings.size() == 1);

    assert(ScreenFilterQuadCount(ScreenFilter::Off) == 0);
    assert(ScreenFilterQuadCount(ScreenFilter::Warm) == 1);
    assert(ScreenFilterQuadCount(ScreenFilter::Cool) == 1);
    assert(ScreenFilterQuadCount(ScreenFilter::Vignette) == 20);
    // TEV REPLACE constant colour/alpha, then source-alpha blending. Check
    // black, white, grey and primary channels against separately fixed outputs.
    const unsigned inputs[] = {0, 255, 128};
    const unsigned warm[3][3] = {{24,17,9},{255,248,240},{140,133,125}};
    const unsigned cool[3][3] = {{12,20,24},{243,251,255},{128,136,140}};
    for (unsigned i = 0; i < 3; i++)
        for (unsigned c = 0; c < 3; c++)
        {
            auto q = ScreenFilterQuad(ScreenFilter::Warm, 0);
            assert(Blend(inputs[i], (q.colour >> (8*c)) & 255, q.colour >> 24) == warm[i][c]);
            q = ScreenFilterQuad(ScreenFilter::Cool, 0);
            assert(Blend(inputs[i], (q.colour >> (8*c)) & 255, q.colour >> 24) == cool[i][c]);
        }
    // Distinct RGB primaries, not only grey ramps.
    const unsigned primary[3][3] = {{255,0,0},{0,255,0},{0,0,255}};
    const unsigned expected[3][3] = {{255,17,9},{24,248,9},{24,17,240}};
    const auto tint = ScreenFilterQuad(ScreenFilter::Warm, 0);
    for (unsigned i = 0; i < 3; i++) for (unsigned c = 0; c < 3; c++)
        assert(Blend(primary[i][c], (tint.colour >> (8*c)) & 255, tint.colour >> 24) == expected[i][c]);

    // Every edge pixel belongs to exactly one ring; no corner overlap and no
    // writes to the centre. Symmetry, bounds and alpha gradient at all pixels.
    for (int y = 0; y < 240; y++) for (int x = 0; x < 400; x++)
    {
        unsigned hits = 0, alpha = 0;
        for (unsigned i = 0; i < ScreenFilterQuadCount(ScreenFilter::Vignette); i++)
        {
            const auto q = ScreenFilterQuad(ScreenFilter::Vignette, i);
            assert(q.x >= 0 && q.y >= 0 && q.w > 0 && q.h > 0 && q.x + q.w <= 400 && q.y + q.h <= 240);
            assert((q.colour & 0xFFFFFF) == 0);
            if (x >= q.x && x < q.x+q.w && y >= q.y && y < q.y+q.h) { hits++; alpha = q.colour >> 24; }
        }
        const int dx = x < 200 ? x : 399-x, dy = y < 120 ? y : 239-y;
        const int ring = dx/10 < dy/6 ? dx/10 : dy/6;
        assert(hits == (ring < 5 ? 1u : 0u));
        assert(alpha == (ring < 5 ? (unsigned)(25-ring*5) : 0u));
        assert(Blend(255, 0, alpha) == 255-alpha);
        assert(Blend(0, 0, alpha) == 0);
    }
    for (unsigned c = 0; c < 256; c++) assert(Blend(c, 0, 0) == c);
    std::puts("filter: settings, live parser, TEV constants/source-over pixels, vignette coverage passed");
}
