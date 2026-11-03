#pragma once

// The one layer buffer every native screen draws into (docs/MEMORY.md).
//
// The plugin heap is 5 MiB and fragments: a 250 KB block freed after the
// game has been played a while can't be allocated again even with more than
// that free in pieces. So the buffer is allocated once, first thing at boot,
// and never freed. It holds a whole screen in the top screen's format
// (400x240 BGR8), which is larger than any native screen's area; a screen
// whose framebuffer needs more bytes than that (RGBA8) fails Layer::Setup and
// stays hidden.
//
// Screens take turns. Borrow and GiveBack from the OSD callbacks only (the
// game thread: they run one after another, so two screens never race for
// it). Usually one gives it back before the next asks (on a hand-over the
// next screen shows a frame later). When two are up at once, the newer one
// wins: a Borrow by a screen that doesn't hold it takes it from the holder
// (the keyboard opens over the battle's after-catch box). The screen it was
// taken from gets nullptr until the buffer is free again, then gets it back
// (fresh) and shows again.

//
// Usage in a screen's OSD callback:
//
//     if (!s_shown) { UI::ScreenBuffer::GiveBack(Owner); return false; }
//     bool fresh = false;
//     u8 *pixels = UI::ScreenBuffer::Borrow(Owner, fresh);
//     if (pixels == nullptr) return false;               // someone else's turn
//     if (fresh || !s_layer.Matches(fb))                 // fresh: another screen drew in it
//     {
//         if (!s_layer.Setup(fb, area, radius, pixels, UI::ScreenBuffer::Bytes))
//             return false;
//         redraw = true;
//     }

#include <cstddef>
#include <string>

#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
namespace ScreenBuffer
{
    static constexpr size_t Bytes = (size_t)ScreenW * ScreenH * 3;

    // main(), before anything else allocates. False if there was no memory
    // (Borrow then always returns nullptr).
    bool Init(void);

    // Game thread. owner is a string literal naming the screen ("xmenu");
    // it is compared by address. fresh is set when the owner did not hold
    // the buffer the last time it asked (set up the layer and redraw).
    u8  *Borrow(const char *owner, bool &fresh);
    // Game thread. Does nothing unless owner holds it.
    void GiveBack(const char *owner);

    // Any thread, for logs: who holds it now (nullptr: nobody), and how many
    // borrows were refused since boot (a screen waiting for a newer one).

    const char *Holder(void);
    unsigned    Refused(void);

    // "heap 1148 KB used, 3700 KB free, largest 3650 KB" (mallinfo plus a
    // malloc probe for the largest block). Plugin thread, for show/hide logs.
    std::string HeapStatus(void);
    // "mem: <what>, " + HeapStatus(), plus ", LOW" when the largest block
    // is under LowMark: the line each screen logs when it shows and hides
    // (what = "xmenu shown"). scenarios/memory-tour.txt fails on LOW.
    static constexpr size_t LowMark = 256 * 1024;
    std::string HeapLog(const char *what);


    // The largest block malloc can hand out now, in bytes (to 1 KB).
    size_t      LargestFree(void);
}
}
}
