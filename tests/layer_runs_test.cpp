#include <cstdio>
#include "UI/LayerRuns.hpp"

using namespace OneScreen::UI;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

// A separate, pixel-mask reference: split by the exclusion, then keep the
// first even row and as many complete four-pixel units as fit in each piece.
static void CheckRuns(unsigned a, unsigned b, unsigned lo, unsigned hi)
{
    bool expected[240] = {}, actual[240] = {};
    unsigned y = a;
    while (y < b)
    {
        if (lo < hi && y >= lo && y < hi) { y++; continue; }
        const unsigned first = y;
        while (y < b && !(lo < hi && y >= lo && y < hi)) y++;
        unsigned start = first;
        if (start % 2 != 0) start++;
        for (unsigned at = start; at + 4 <= y; at += 4)
            for (unsigned k = 0; k < 4; k++) expected[at + k] = true;
    }
    const auto runs = SplitLayerRun(a, b, lo, hi);
    for (const auto r : runs.pieces)
    {
        CHECK(r.a % 2 == 0);
        CHECK(r.b >= r.a && (r.b - r.a) % 4 == 0);
        CHECK(r.b <= 240);
        for (unsigned at = r.a; at < r.b; at++)
        {
            CHECK(at >= a && at < b);
            CHECK(!(lo < hi && at >= lo && at < hi));
            CHECK(!actual[at]);
            actual[at] = true;
        }
    }
    for (unsigned at = 0; at < 240; at++) CHECK(actual[at] == expected[at]);
}

int main()
{
    // All run endpoints without skip, plus exclusions at every row and all
    // four alignment remainders. Reverse order uses the same memory math.
    for (unsigned a = 0; a <= 240; a++)
        for (unsigned b = a; b <= 240; b++)
            CheckRuns(a, b, 0, 0);
    for (unsigned a = 0; a < 8; a++)
        for (unsigned b = 232; b <= 240; b++)
            for (unsigned lo = 0; lo <= 240; lo++)
                for (unsigned width = 0; width <= 8 && lo + width <= 240; width++)
                {
                    CheckRuns(a, b, lo, lo + width);
                    CheckRuns(240 - b, 240 - a, 240 - lo - width, 240 - lo);
                }
    CheckRuns(20, 220, 0, 240); // Entire run excluded.
    CheckRuns(20, 24, 40, 60);  // Skip outside run.
    CheckRuns(20, 220, 0, 100); // Skip overlaps one end.
    CheckRuns(20, 220, 100, 240);
    std::printf("layer runs: %d failures\n", failures);
    return failures ? 1 : 0;
}
