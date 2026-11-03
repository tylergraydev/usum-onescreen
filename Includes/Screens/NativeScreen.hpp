#pragma once

// What the simple native screens (Options, Trainer Passport, Battle Video;
// W5-3) have in common: each mirrors one game scene on the top screen while
// the game's own screen runs on the hidden bottom screen. A screen names the
// bottom-screen pixels it wants sampled, turns those samples and the
// player's keys into actions (key presses on the game's screen, through the
// action layer), and draws itself. Pure C++ (no CTRPF), so tests/ can build
// it on the PC; Screens/SubScreens.cpp runs the screens in the plugin.

#include <string>
#include <vector>

#include "ActionQueue.hpp"
#include "UI/Raster.hpp"

#ifndef __3DS__
typedef int16_t s16;     // libctru's, for the host build (ActionQueue.hpp has the others)
typedef int32_t s32;
#endif

namespace OneScreen
{
    // HID key bits (same values as CTRPF's Key::). The plugin glue folds the
    // circle pad into the D-pad bits before a screen sees a key.
    namespace ScreenKey
    {
        const u32 A = 1u << 0, B = 1u << 1, Start = 1u << 3;
        const u32 Right = 1u << 4, Left = 1u << 5, Up = 1u << 6, Down = 1u << 7;
        const u32 R = 1u << 8, L = 1u << 9;
        const u32 CPadRight = 1u << 28, CPadLeft = 1u << 29, CPadUp = 1u << 30, CPadDown = 1u << 31;
        const u32 DPad = Right | Left | Up | Down;
        const u32 CPad = CPadRight | CPadLeft | CPadUp | CPadDown;
    }

    // A bottom-screen pixel to sample (320 x 240 coordinates).
    struct SamplePoint
    {
        s16 x, y;
    };

    // Most samples a screen may ask for (the glue's buffer is this big).
    const u32 MaxScreenSamples = 160;

    // A sampled colour, and the tests the probes use.
    struct Sample
    {
        u8 r, g, b;
    };
    inline bool IsRed(const Sample &s)  { return s.r > 180 && s.g < 70 && s.b < 70; }
    inline bool IsDark(const Sample &s) { return s.r < 110 && s.g < 110 && s.b < 110; }
    // The game's arrow blue (0,119,238) and its anti-aliased edge.
    inline bool IsArrowBlue(const Sample &s) { return s.r < 60 && s.g >= 80 && s.g <= 170 && s.b > 180; }

    class NativeScreen
    {
    public:
        virtual ~NativeScreen() {}

        // For commands and the log: "options".
        virtual const char *Name(void) const = 0;
        // The game scene it mirrors (SceneModule()).
        virtual const char *Scene(void) const = 0;
        // Top-screen area it draws in.
        virtual UI::Rect Area(void) const = 0;

        // Keys taken from the game while the screen waits for input; may
        // change with the screen's state. Keys not listed (B at least) go to
        // the game as without the plugin.
        virtual u32 CaptureKeys(void) const = 0;

        // Bottom-screen pixels to sample every frame while the scene is up.
        virtual const std::vector<SamplePoint> &Samples(void) const = 0;

        // The scene just showed: forget the last visit. Memory reads the
        // screen needs are handed over by the glue before this.
        virtual void Begin(std::vector<std::string> &log) = 0;
        // New samples, in the order of Samples().
        virtual void Sampled(const Sample *samples, std::vector<std::string> &log) = 0;
        // True once the samples show the game's screen as the native screen
        // expects it; it only draws and takes keys then.
        virtual bool Ready(void) const = 0;
        // One key the player pressed (or that repeats), while no action runs.
        // Returns the action to run for it, or nullptr. The definition must
        // outlive the run.
        virtual const ActionDef *Key(u32 key, std::vector<std::string> &log) = 0;
        // The action from Key() ended (the action layer logs ok / FAILED;
        // a screen that cares checks what the samples show next).
        virtual void ActionDone(std::vector<std::string> &log) { (void)log; }

        // Changes whenever Draw would draw something else.
        virtual u32 DrawKey(void) const = 0;
        virtual void Draw(UI::Canvas &canvas) const = 0;
        virtual std::string Status(void) const = 0;
    };

    // An action that presses `keys` once on the game's screen `scene`, then
    // waits `settle` frames for it to redraw. Kept for the program's life;
    // the same arguments give the same definition.
    const ActionDef &ScreenPressAction(const char *name, const char *scene, u32 keys, u32 settle);

    // Shared look (after the native X menu).
    namespace ScreenStyle
    {
        constexpr UI::Color PanelBg  = UI::Rgb(22, 30, 52);
        constexpr UI::Color Border   = UI::Rgb(120, 150, 220);
        constexpr UI::Color Divider  = UI::Rgb(120, 150, 220, 90);
        constexpr UI::Color RowBg    = UI::Rgb(38, 50, 84);
        constexpr UI::Color TextMain = UI::Rgb(250, 252, 255);
        constexpr UI::Color TextDim  = UI::Rgb(150, 165, 200);
        constexpr UI::Color Selected = UI::Rgb(255, 236, 120);
        constexpr s32 Radius = 12;
    }

    // Background, border and title; returns the area left below the title.
    UI::Rect DrawScreenFrame(UI::Canvas &canvas, const UI::Rect &area, const std::string &title);
    // One line of key hints at the bottom of `area`; returns the area above it.
    UI::Rect DrawScreenFooter(UI::Canvas &canvas, const UI::Rect &area, const std::string &hints);
}
