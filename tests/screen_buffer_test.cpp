// Host test for UI/ScreenBuffer: the borrowing rules, and that every native
// screen's layer area fits the one buffer on the top screen (BGR8, rotated).

#include <cstdio>
#include <cstdlib>

#include "Screens/BagList.hpp"
#include "Screens/BattleVideo.hpp"
#include "Screens/BoxesGrid.hpp"
#include "Screens/KeyboardGrid.hpp"
#include "Screens/MapView.hpp"
#include "Screens/Options.hpp"
#include "Screens/PartyMenu.hpp"
#include "Screens/PokedexList.hpp"
#include "Screens/RotomPromptView.hpp"
#include "Screens/TrainerPassport.hpp"
#include "Screens/XMenuGrid.hpp"
#include "UI/Layer.hpp"
#include "UI/ScreenBuffer.hpp"
#include "UI/ThemeDemo.hpp"

using namespace OneScreen;
using namespace OneScreen::UI;

static int s_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            s_failures++;                                                        \
        }                                                                        \
    } while (0)

static const char *const A = "a";
static const char *const B = "b";

static void TestBorrowing(void)
{
    bool fresh = true;
    CHECK(ScreenBuffer::Borrow(A, fresh) == nullptr);   // not allocated yet
    CHECK(!fresh);
    CHECK(ScreenBuffer::Init());
    CHECK(ScreenBuffer::Init());                        // once only, same buffer

    u8 *a = ScreenBuffer::Borrow(A, fresh);
    CHECK(a != nullptr && fresh);
    CHECK(ScreenBuffer::Borrow(A, fresh) == a && !fresh);   // still A's: no redraw
    CHECK(ScreenBuffer::Holder() == A);

    // B opens over A: B takes it; A waits (refused, counted) until it's free.
    const unsigned refused = ScreenBuffer::Refused();
    CHECK(ScreenBuffer::Borrow(B, fresh) == a && fresh);
    CHECK(ScreenBuffer::Holder() == B);
    CHECK(ScreenBuffer::Borrow(A, fresh) == nullptr && !fresh);
    CHECK(ScreenBuffer::Borrow(A, fresh) == nullptr);
    CHECK(ScreenBuffer::Refused() == refused + 2);
    CHECK(ScreenBuffer::Borrow(B, fresh) == a && !fresh);   // B stays on top
    ScreenBuffer::GiveBack(A);                          // A hides while waiting: no effect on B
    CHECK(ScreenBuffer::Holder() == B);

    // A shows again while B is up: A is no longer "waiting", so it is the
    // newer one now and takes it back.
    CHECK(ScreenBuffer::Borrow(A, fresh) == a && fresh);
    CHECK(ScreenBuffer::Borrow(B, fresh) == nullptr);
    // A gives it back; the waiting B gets it again, fresh.
    ScreenBuffer::GiveBack(A);
    CHECK(ScreenBuffer::Holder() == nullptr);
    CHECK(ScreenBuffer::Borrow(B, fresh) == a && fresh);
    CHECK(ScreenBuffer::Borrow(A, fresh) == a && fresh);    // A is new again: takes it
    ScreenBuffer::GiveBack(A);
    ScreenBuffer::GiveBack(B);

    // Three up at once: each newer one wins, and the older two both wait
    // (no ping-pong between them).
    static const char *const C = "c";
    CHECK(ScreenBuffer::Borrow(A, fresh) == a);
    CHECK(ScreenBuffer::Borrow(B, fresh) == a);
    CHECK(ScreenBuffer::Borrow(C, fresh) == a);
    for (int frame = 0; frame < 3; frame++)
    {
        CHECK(ScreenBuffer::Borrow(A, fresh) == nullptr);
        CHECK(ScreenBuffer::Borrow(B, fresh) == nullptr);
        CHECK(ScreenBuffer::Borrow(C, fresh) == a && !fresh);
    }
    ScreenBuffer::GiveBack(C);
    ScreenBuffer::GiveBack(B);
    ScreenBuffer::GiveBack(A);
    CHECK(ScreenBuffer::Holder() == nullptr);


    // A again: fresh even with nobody in between.
    CHECK(ScreenBuffer::Borrow(A, fresh) == a && fresh);
    ScreenBuffer::GiveBack(A);
    CHECK(ScreenBuffer::Borrow(nullptr, fresh) == nullptr);
}

static void TestAreasFit(void)
{
    // The real top screen: rotated 240x400 BGR8, stored column by column.
    static u8 fbPixels[ScreenW * ScreenH * 3];
    const Surface top = { fbPixels + (ScreenH - 1) * 3, ScreenH * 3, -3, PixelFormat::BGR8, ScreenW, ScreenH };
    CHECK(ScreenBuffer::Init());
    bool fresh = false;
    u8 *pixels = ScreenBuffer::Borrow(A, fresh);
    CHECK(pixels != nullptr);

    const Rect areas[] = {
        XMenuArea, BagArea, BoxesArea, KeyboardArea, MapArea, OptionsArea, PartyArea, PassportArea, VideoArea,
        Rect{ 0, 0, PokedexMaxW, PokedexMaxH }, Rect{ 12, 8, RotomBoxW, RotomBoxMaxH }, RotomBadge, ThemeDemoArea,
        Rect{ 166, 36, 230, 200 },  // BattleMenu.cpp MovesArea, the largest battle page
        Rect{ 156, 36, 240, 156 },  // MoveLearnMenu.cpp ChooseArea, the largest learn page
    };
    for (const Rect &area : areas)
    {
        Layer layer;
        CHECK(Layer::BytesFor(top, area) <= ScreenBuffer::Bytes);
        CHECK(layer.Setup(top, area, 10, pixels, ScreenBuffer::Bytes));
    }
    // Boxes keeps its badge at the tail (Screens/Boxes.cpp).
    const size_t badge = (size_t)BoxesBadgeArea.w * BoxesBadgeArea.h * 4;
    CHECK(Layer::BytesFor(top, BoxesArea) <= ScreenBuffer::Bytes - badge);
    ScreenBuffer::GiveBack(A);
}

static void TestHeap(void)
{
    const size_t largest = ScreenBuffer::LargestFree();
    CHECK(largest >= 1024);
    void *p = std::malloc(largest > 4096 ? largest - 4096 : 1);
    CHECK(p != nullptr);
    std::free(p);
    CHECK(ScreenBuffer::HeapLog("x shown").compare(0, 16, "mem: x shown, he") == 0);
}

int main(void)
{
    TestBorrowing();
    TestAreasFit();
    TestHeap();
    if (s_failures != 0)
    {
        std::printf("screen_buffer_test: %d FAILED\n", s_failures);
        return 1;
    }
    std::printf("screen_buffer_test: ok (%u bytes)\n", (unsigned)ScreenBuffer::Bytes);
    return 0;
}
