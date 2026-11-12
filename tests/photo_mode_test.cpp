#include "PhotoMode.hpp"
#include "InputCapture.hpp"
#include "PcAnywhereRules.hpp"
#include "Settings.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace OneScreen;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main()
{
    PhotoState state;
    CHECK(!state.active);
    CHECK(!PhotoExit(state));
    CHECK(!PhotoHintVisible(state, 0));
    PhotoSafety safe;
    CHECK(PhotoEnter(state, safe, 500));
    CHECK(state.active && state.enteredMs == 500);
    CHECK(!PhotoEnter(state, safe, 1000));
    CHECK(state.enteredMs == 500);
    CHECK(PhotoHintVisible(state, 500));
    CHECK(PhotoHintVisible(state, 2499));
    CHECK(!PhotoHintVisible(state, 2500));
    CHECK(!PhotoHintVisible(state, 100000));
    CHECK(PhotoExit(state));
    CHECK(!PhotoHintVisible(state, 501));
    CHECK(PhotoEnter(state, safe, 100000));
    CHECK(PhotoHintVisible(state, 100001));
    CHECK(PhotoExit(state));

    // Every refusal leaves the state untouched; clearing it permits entry.
    bool PhotoSafety::*fields[] = { &PhotoSafety::saving, &PhotoSafety::action,
        &PhotoSafety::pcPending, &PhotoSafety::fieldBusy, &PhotoSafety::alreadyPaused,
        &PhotoSafety::gpuTest };
    for (auto field : fields)
    {
        PhotoSafety unsafe;
        unsafe.*field = true;
        CHECK(*PhotoRefusal(unsafe));
        CHECK(!PhotoEnter(state, unsafe, 42));
        CHECK(!state.active && state.enteredMs == 100000);
        unsafe.*field = false;
        CHECK(PhotoEnter(state, unsafe, 42));
        CHECK(PhotoExit(state));
        state.enteredMs = 100000;
    }
    PhotoSafety loading;
    loading.supportedScene = false;
    CHECK(!PhotoEnter(state, loading, 1));

    const u32 chord = KeyBits::R | KeyBits::Select;
    PcComboState combo;
    CHECK(!PcComboRead(combo, 0, chord));
    CHECK(!PcComboRead(combo, chord, KeyBits::R));
    CHECK(PcComboRead(combo, chord, chord));
    CHECK(combo.suppressed == chord);
    CHECK(!PcComboRead(combo, chord, chord));
    CHECK(!PcComboRead(combo, chord, 0));
    CHECK(PcComboRead(combo, chord, chord));

    // The actual shared capture functions used at runtime: entry reads prime
    // capture, no reads happen during pause, exit restores the prior request.
    PadCaptureState pad;
    PadSample sample{};
    sample.held = KeyBits::A | chord | KeyBits::CPadRight;
    sample.pressed = sample.held;
    sample.stickX = 156;
    MaskPadSamples(pad, ~0u, &sample, 1);
    CHECK(sample.held == 0 && sample.pressed == 0 && sample.stickX == 0);
    // A pressed/held during the frozen interval cannot fire on resume.
    sample = PadSample{};
    sample.held = KeyBits::A | chord | KeyBits::CPadRight;
    sample.pressed = sample.held;
    sample.stickX = 156;
    MaskPadSamples(pad, 0, &sample, 1);
    CHECK(sample.held == 0 && sample.pressed == 0 && sample.stickX == 0);
    sample = PadSample{};
    sample.released = KeyBits::A | chord | KeyBits::CPadRight;
    MaskPadSamples(pad, 0, &sample, 1);
    CHECK(sample.released == 0 && pad.suppressed == 0);
    sample.held = sample.pressed = KeyBits::A;
    MaskPadSamples(pad, 0, &sample, 1);
    CHECK(sample.held == KeyBits::A && sample.pressed == KeyBits::A);

    // Restore a previous menu capture rather than dropping its ownership.
    pad = PadCaptureState{};
    sample = PadSample{};
    MaskPadSamples(pad, ~0u, &sample, 1);
    sample.held = sample.pressed = KeyBits::A | KeyBits::B;
    MaskPadSamples(pad, KeyBits::B, &sample, 1);
    CHECK(sample.held == 0 && sample.pressed == 0);
    CHECK(pad.applied == KeyBits::B);

    TouchCaptureState touch;
    TouchSample ts{};
    ts.touching = 1;
    MaskTouchSamples(touch, ~0u, &ts, 1);
    CHECK(!ts.touching);
    ts.touching = 1;
    MaskTouchSamples(touch, 0, &ts, 1);
    CHECK(!ts.touching && touch.suppressed);
    MaskTouchSamples(touch, 0, &ts, 1);
    CHECK(!touch.suppressed);
    ts.touching = 1;
    MaskTouchSamples(touch, 0, &ts, 1);
    CHECK(ts.touching);

    std::vector<std::string> warnings;
    CHECK(Settings{}.photoKey == 0);
    auto settings = ParseSettings("[controls]\nphoto_key = R+Select\n", warnings);
    CHECK(warnings.empty() && settings.photoKey == chord);
    CHECK(DescribeSettings(settings).find("photo_key R+Select") != std::string::npos);
    settings = ParseSettings(DefaultSettingsText(), warnings);
    CHECK(warnings.empty() && settings.photoKey == 0);
    settings = ParseSettings("[controls]\nphoto_key = nonsense\n", warnings);
    CHECK(!warnings.empty() && settings.photoKey == 0);
    warnings.clear();
    settings = ParseSettings("[controls]\nphoto_key = R+Select\nphoto_key = none\n", warnings);
    CHECK(warnings.empty() && settings.photoKey == 0);
    std::printf("photo_mode_test: %d checks passed\n", checks);
}
