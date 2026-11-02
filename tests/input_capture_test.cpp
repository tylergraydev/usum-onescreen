// Host tests for the input capture masking in Sources/InputCapture.cpp.
// Build and run: see tests/README.md.

#include "InputCapture.hpp"

#include <cstdio>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static const u32 A = 1u << 0, B = 1u << 1, Start = 1u << 3, Up = 1u << 6;
static const u32 CPadUp = 1u << 30;

static PadSample Pad(u32 held, u32 pressed = 0, u32 released = 0, s16 x = 0, s16 y = 0)
{
    PadSample s = {};
    s.held = held;
    s.pressed = pressed;
    s.released = released;
    s.stickX = x;
    s.stickY = y;
    return s;
}

// A stream of raw HID samples, one read per frame, fed through the mask the
// way the hook does. Edges in the raw stream come from `held`, as HID does.
struct PadStream
{
    PadCaptureState state;
    u32 rawPrev = 0;

    PadSample Read(u32 held, u32 request, s16 x = 0, s16 y = 0)
    {
        PadSample s = Pad(held, held & ~rawPrev, rawPrev & ~held, x, y);
        rawPrev = held;
        MaskPadSamples(state, request, &s, 1);
        return s;
    }
};

// PadReader::Read: takes edges from the sample's own bits.
struct ReadConsumer
{
    u32 presses = 0, releases = 0;
    void Feed(const PadSample &s) { presses |= s.pressed; releases |= s.released; }
};

// PadReader::ReadLatest: derives edges from `held` itself.
struct ReadLatestConsumer
{
    u32 prev = 0, presses = 0, releases = 0;
    void Feed(const PadSample &s)
    {
        presses |= s.held & ~prev;
        releases |= prev & ~s.held;
        prev = s.held;
    }
};

static void TestPassThrough()
{
    PadCaptureState state;
    PadSample s = Pad(A | B, A, Start, 50, -60);
    MaskPadSamples(state, 0, &s, 1);
    CHECK(s.held == (A | B) && s.pressed == A && s.released == Start);
    CHECK(s.stickX == 50 && s.stickY == -60);
}

static void TestCapturedKeysHidden()
{
    PadCaptureState state;
    PadSample s[2] = { Pad(A | B, A | B, Start), Pad(A | B | Up, Up, A) };
    MaskPadSamples(state, A | Start, s, 2);
    CHECK(s[0].held == B && s[0].pressed == B && s[0].released == 0);
    CHECK(s[1].held == (B | Up) && s[1].pressed == Up && s[1].released == 0);
}

// A held through begin and end: neither reader may see a press after end
// until A is released and pressed again.
static void TestHeldAcrossEnd()
{
    PadStream pad;
    ReadConsumer read;
    ReadLatestConsumer latest;
    auto feed = [&](const PadSample &s) { read.Feed(s); latest.Feed(s); };

    feed(pad.Read(0, 0));
    feed(pad.Read(0, A));       // capture begins
    feed(pad.Read(A, A));       // A pressed inside the menu
    feed(pad.Read(A, A));
    CHECK(read.presses == 0 && latest.presses == 0);

    feed(pad.Read(A, 0));       // capture ends, A still held
    feed(pad.Read(A | B, 0));   // other keys pass while A is suppressed
    CHECK((read.presses & A) == 0 && (latest.presses & A) == 0);
    CHECK((read.presses & B) && (latest.presses & B));
    CHECK(pad.state.suppressed == A);

    feed(pad.Read(B, 0));       // A released: no release edge either, the game never saw it
    CHECK((read.releases & A) == 0 && (latest.releases & A) == 0);
    CHECK(pad.state.suppressed == 0);

    feed(pad.Read(A | B, 0));   // a real new press goes through
    CHECK((read.presses & A) && (latest.presses & A));
}

// Not held at end: nothing to suppress, the next press is seen at once.
static void TestReleasedBeforeEnd()
{
    PadStream pad;
    ReadConsumer read;
    pad.Read(A, A);
    pad.Read(0, A);
    read.Feed(pad.Read(0, 0));
    CHECK(pad.state.suppressed == 0);
    read.Feed(pad.Read(A, 0));
    CHECK(read.presses == A);
}

// Begin while A is held (the menu's own open key): A disappears from held at
// once and the game never sees its later press bits or release bit.
static void TestHeldAtBegin()
{
    PadStream pad;
    ReadConsumer read;
    ReadLatestConsumer latest;
    auto feed = [&](const PadSample &s) { read.Feed(s); latest.Feed(s); };

    feed(pad.Read(A, 0));
    CHECK(read.presses == A && latest.presses == A);
    read = ReadConsumer();
    latest.presses = 0;

    feed(pad.Read(A, A));
    CHECK(latest.releases == A);   // held dropped: ReadLatest sees a release
    CHECK(read.releases == 0);     // Read gets no release bit (Wave 1 check)
    feed(pad.Read(0, A));
    CHECK(read.releases == 0);
}

// Capture re-begun while a key is still suppressed keeps it hidden.
static void TestRebeginWhileSuppressed()
{
    PadStream pad;
    pad.Read(A, A);
    pad.Read(A, 0);
    CHECK(pad.state.suppressed == A);
    PadSample s = pad.Read(A, B);
    CHECK((s.held & A) == 0);
    s = pad.Read(0, B);
    CHECK(pad.state.suppressed == 0);
}

// A release inside a multi-sample read ends suppression after that read.
static void TestReleaseInsideBatch()
{
    PadCaptureState state;
    PadSample one = Pad(A, A);
    MaskPadSamples(state, A, &one, 1);
    PadSample batch[3] = { Pad(A), Pad(0, 0, A), Pad(A, A) };
    MaskPadSamples(state, 0, batch, 3);
    for (const PadSample &s : batch)
        CHECK(s.held == 0 && s.pressed == 0 && s.released == 0);
    CHECK(state.suppressed == 0);
    PadSample next = Pad(A);
    MaskPadSamples(state, 0, &next, 1);
    CHECK(next.held == A);
}

static void TestStickMasking()
{
    PadStream pad;
    PadSample s = pad.Read(CPadUp | B, CaptureStickBits, 0, 150);
    CHECK(s.stickX == 0 && s.stickY == 0 && s.held == B);

    // Ends tilted: the stick stays centred until it returns inside the dead zone.
    s = pad.Read(CPadUp, 0, 0, 150);
    CHECK(s.stickY == 0 && (s.held & CPadUp) == 0);
    s = pad.Read(0, 0, 0, 10);
    CHECK(s.stickY == 0);
    s = pad.Read(CPadUp, 0, 0, 150);
    CHECK(s.stickY == 150 && (s.held & CPadUp));

    // Key capture alone leaves the stick alone.
    PadCaptureState keys;
    PadSample k = Pad(A, 0, 0, 30, 40);
    MaskPadSamples(keys, A, &k, 1);
    CHECK(k.stickX == 30 && k.stickY == 40);
}

static TouchSample Touch(u16 x, u16 y, bool touching)
{
    TouchSample s = {};
    s.x = x;
    s.y = y;
    s.touching = touching ? 1 : 0;
    return s;
}

static void TestTouchMasking()
{
    TouchCaptureState state;
    TouchSample s = Touch(100, 80, true);
    MaskTouchSamples(state, 0, &s, 1);
    CHECK(s.touching == 1 && s.x == 100);

    s = Touch(100, 80, true);
    MaskTouchSamples(state, A, &s, 1);   // keys only: touch passes
    CHECK(s.touching == 1);

    s = Touch(100, 80, true);
    MaskTouchSamples(state, CaptureTouchBit, &s, 1);
    CHECK(s.touching == 0 && s.x == 0 && s.y == 0);

    s = Touch(120, 90, true);           // ends while touching: still hidden
    MaskTouchSamples(state, 0, &s, 1);
    CHECK(s.touching == 0 && state.suppressed);

    s = Touch(0, 0, false);             // lifted
    MaskTouchSamples(state, 0, &s, 1);
    CHECK(!state.suppressed);

    s = Touch(50, 60, true);            // new touch goes through
    MaskTouchSamples(state, 0, &s, 1);
    CHECK(s.touching == 1 && s.x == 50 && s.y == 60);
}

static void TestEmptyReads()
{
    PadCaptureState pad;
    MaskPadSamples(pad, A, nullptr, 0);
    MaskPadSamples(pad, 0, nullptr, 0);
    CHECK(pad.suppressed == A);          // no sample yet showed A released
    TouchCaptureState touch;
    MaskTouchSamples(touch, CaptureTouchBit, nullptr, 0);
    MaskTouchSamples(touch, 0, nullptr, 0);
    CHECK(touch.suppressed);
}

int main()
{
    TestPassThrough();
    TestCapturedKeysHidden();
    TestHeldAcrossEnd();
    TestReleasedBeforeEnd();
    TestHeldAtBegin();
    TestRebeginWhileSuppressed();
    TestReleaseInsideBatch();
    TestStickMasking();
    TestTouchMasking();
    TestEmptyReads();

    if (s_failures == 0)
        std::printf("input_capture_test: all passed\n");
    return s_failures == 0 ? 0 : 1;
}
