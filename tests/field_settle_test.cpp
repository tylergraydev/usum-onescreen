// Host tests for the field state settle (Includes/FieldSettle.hpp) that
// AutoPanel uses after entering the field. Build and run: make -C tests.

#include "FieldSettle.hpp"

#include <cstdio>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static const int Menu = 4;

// Reads `state` `count` times; returns how many of them were still settling.
static int ReadFor(FieldSettle &settle, int state, int count)
{
    int waiting = 0;
    for (int i = 0; i < count; i++)
        waiting += settle.Read(state);
    return waiting;
}

static void TestTownMapReturn(void)
{
    // Logged on Ultra Sun (xmenu-touch-close): tapping the map on Rotom's
    // screen reads 4 in the field, and coming back from the Town Map it reads
    // 4 for 30 frames from the first FieldRo read, then 0. The old settle kept
    // the 4 from before the Town Map, so the first 4 didn't restart it and
    // the 30th read let the menu count as open.
    FieldSettle settle;
    settle.Enter();
    CHECK(ReadFor(settle, 0, 200) == FieldSettle::Frames);  // walking
    CHECK(!settle.Read(Menu));                              // settled: no new count
    settle.Enter();                                         // Town Map, then back
    CHECK(ReadFor(settle, Menu, 30) == 30);
    CHECK(ReadFor(settle, Menu, 15) == 15);                 // room for a slower run
    CHECK(settle.Read(0));                                  // drops to 0: counts again
    CHECK(ReadFor(settle, 0, FieldSettle::Frames - 1) == FieldSettle::Frames - 1);
    CHECK(!settle.Read(0));
}

static void TestMenuAfterParty(void)
{
    // X menu -> Pokémon -> back returns to the open menu: 4 that holds counts.
    FieldSettle settle;
    settle.Enter();
    CHECK(ReadFor(settle, Menu, FieldSettle::Frames) == FieldSettle::Frames);
    CHECK(!settle.Read(Menu));
    CHECK(!settle.Read(0));  // closing the menu later isn't delayed
}

static void TestFirstReadStarts(void)
{
    // The value from the field the player left never counts as held.
    FieldSettle settle;
    settle.Enter();
    CHECK(settle.left == FieldSettle::Frames && settle.last == -1);
    CHECK(settle.Read(0));
    CHECK(settle.left == FieldSettle::Frames);
}

int main()
{
    TestTownMapReturn();
    TestMenuAfterParty();
    TestFirstReadStarts();
    if (s_failures == 0)
        std::printf("field_settle_test: all passed\n");
    return s_failures == 0 ? 0 : 1;
}
