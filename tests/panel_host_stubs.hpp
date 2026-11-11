#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "UI/ColumnMapping.hpp"

// Addresses must fit host pointers; pixel channels and signed offsets retain
// their device widths. Host transfer mocks count every GX submission.
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uintptr_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
static u64 svcGetSystemTick() { return 0; }
static constexpr u64 SYSCLOCK_ARM11 = 268123480;
static int gxCommands = 0;
static bool reverseY = false, cursorTouching = false;
static float testSqueeze = 1.0f;
static u32 cursorX = 160, cursorY = 120;
using Result = int;
using MemPerm = int;
static constexpr int MEMOP_ALLOC_LINEAR = 0, MEMPERM_READ = 1, MEMPERM_WRITE = 2;
#define R_FAILED(result) ((result) < 0)
static Result svcControlMemory(u32 *address, u32, u32, u32 size, int, MemPerm)
{
    // A single persistent block, just like the production scratch allocation.
    static std::vector<u8> scratch;
    scratch.resize(size);
    *address = (u32)scratch.data();
    return 0;
}

namespace CTRPluginFramework
{
    enum GSPFormat { GSP_RGBA8_OES, GSP_BGR8_OES, GSP_RGB565_OES, GSP_RGB5_A1_OES, GSP_RGBA4_OES };
    struct Color
    {
        u8 r, g, b;
        static const Color Black, White, Red;
    };
    inline const Color Color::Black = {0, 0, 0};
    inline const Color Color::White = {255, 255, 255};
    inline const Color Color::Red = {255, 0, 0};
    struct Screen
    {
        bool IsTop;
        int Format;
        u32 Stride, LeftFramebuffer;
        u8 *GetFramebuffer(u32 x, u32 y) const
        {
            const u32 bpp = (Format & 7) == GSP_BGR8_OES ? 3 : (Format & 7) == GSP_RGBA8_OES ? 4 : 2;
            return (u8 *)LeftFramebuffer + x * Stride + (reverseY ? 239 - y : y) * bpp;
        }
        void DrawRect(s32 x, s32 y, s32 w, s32 h, Color color) const;
    };
    namespace Utils
    {
        template<class... Args> std::string Format(const char *, Args...) { return {}; }
    }
    namespace OSD { inline void Run(bool (*)(const Screen &)) {} }
}
using CTRPluginFramework::GSP_RGBA8_OES;
using CTRPluginFramework::GSP_BGR8_OES;
using CTRPluginFramework::GSP_RGB565_OES;
using CTRPluginFramework::GSP_RGB5_A1_OES;
using CTRPluginFramework::GSP_RGBA4_OES;

namespace OneScreen
{
    enum class PanelMode : u8 { Off, Panel, Full };
    static Settings settings;
    const Settings &GetSettings() { return settings; }
    static float WidescreenSqueeze() { return testSqueeze; }
    static bool CursorGet(u32 &x, u32 &y) { x = cursorX; y = cursorY; return cursorTouching; }
    static bool UiDemoOnScreen(const CTRPluginFramework::Screen &) { return false; }
    static bool BattleScreenOnScreen(const CTRPluginFramework::Screen &) { return false; }
    static bool ShotOnScreen(const CTRPluginFramework::Screen &) { return false; }
    namespace UI
    {
        static void StartLayerGpu(u32, u32) {}
        static bool GpuQuadOnScreen(const CTRPluginFramework::Screen &) { return false; }
    }
}
