#include "UI/Device.hpp"

#include <vector>

#include "Framebuffer.hpp"
#include "Widescreen.hpp"
#include "UI/Demo.hpp"
#include "UI/Font.hpp"
#include "UI/Layer.hpp"
#include "UI/ScreenBuffer.hpp"
#include "UI/Text.hpp"

// libctru's shared system font; CTRPF maps it at startup (font.h declares it
// only inside an inline function).
extern "C" CFNT_s *g_sharedFont;

namespace OneScreen
{
    using namespace CTRPluginFramework;

    UI::Surface SurfaceFromScreen(const Screen &screen)
    {
        const FbLayout fb = GetLayout(screen);
        UI::Surface surface;
        surface.origin = fb.origin;
        surface.stepX = fb.stepX;
        surface.stepY = fb.stepY;
        surface.format = (UI::PixelFormat)fb.format;
        surface.width = screen.IsTop ? (s32)TopWidth : (s32)BottomWidth;
        surface.height = ScreenHeight;
        surface.squeeze = screen.IsTop ? WidescreenSqueeze() : 1.0f;
        surface.pillarbox = GetSettings().pillarbox;
        return surface;
    }

    static UI::FontSource s_font;
    static bool s_fontReady = false;

    static int FontIndexOf(u32 codePoint)
    {
        return fontGlyphIndexFromCodePoint(g_sharedFont, codePoint);
    }

    static void FontWidths(int index, s32 &left, s32 &glyphW, s32 &advance)
    {
        const charWidthInfo_s *w = fontGetCharWidthInfo(g_sharedFont, index);
        left = w->left;
        glyphW = w->glyphWidth;
        advance = w->charWidth;
    }

    bool UiInitFont(void)
    {
        if (s_fontReady)
            return true;
        // CTRPF maps the font when it first needs it (already at boot in
        // testing); measuring a string makes sure.
        if (g_sharedFont == nullptr)
            OSD::GetTextWidth(true, "A");
        if (g_sharedFont == nullptr || g_sharedFont->finf.tglp == nullptr)
            return false;

        const FINF_s &finf = g_sharedFont->finf;
        const TGLP_s &tglp = *finf.tglp;
        s_font.sheets = tglp.sheetData;
        s_font.sheetSize = tglp.sheetSize;
        s_font.sheetCount = tglp.nSheets;
        s_font.sheetFormat = tglp.sheetFmt;
        s_font.sheetW = tglp.sheetWidth;
        s_font.sheetH = tglp.sheetHeight;
        s_font.cellsPerRow = tglp.nRows;
        s_font.cellRows = tglp.nLines;
        s_font.cellW = tglp.cellWidth;
        s_font.cellH = tglp.cellHeight;
        s_font.baseline = tglp.baselinePos;
        s_font.lineFeed = finf.lineFeed;
        s_font.indexOf = FontIndexOf;
        s_font.widths = FontWidths;
        if (s_font.sheetFormat != UI::SheetFormatA4)
            return false;
        UI::SetSystemFont(&s_font);
        s_fontReady = true;
        return true;
    }

    std::string UiFontInfo(void)
    {
        if (!UiInitFont())
            return Utils::Format("uifont: no system font (g_sharedFont %08lX)", (u32)g_sharedFont);
        u32 hits, misses;
        UI::TextCacheStats(hits, misses);
        return Utils::Format("uifont: %08lX cell %ux%u baseline %u line %u, %u sheets %ux%u format %u at %08lX, "
                             "cache hits %lu misses %lu",
                             (u32)g_sharedFont, s_font.cellW, s_font.cellH, s_font.baseline, s_font.lineFeed,
                             s_font.sheetCount, s_font.sheetW, s_font.sheetH, s_font.sheetFormat,
                             (u32)s_font.sheets, hits, misses);
    }

    static volatile UiDemoMode s_demo = UiDemoMode::Off;
    static u32 s_demoFrames = 0;

    // The demo panel, drawn off-screen when the highlight moves.
    static UI::Layer s_demoLayer;
    // Its pixels are UI::ScreenBuffer's while the demo shows.
    static const char *const DemoOwner = "uidemo";
    static s32 s_demoDrawn = -1;

    // Emulated ticks per demo frame, and how many frames re-rendered the
    // layer; the layer re-renders and copies are also timed on their own.
    static u32 s_uiFrames = 0, s_uiRedraws = 0, s_uiPresents = 0;
    static u64 s_uiSum = 0, s_uiMax = 0, s_uiRedrawSum = 0, s_uiPresentSum = 0;

    void UiDemoSetMode(UiDemoMode mode)
    {
        s_demo = mode;
        s_demoFrames = 0;
        s_demoDrawn = -1;
    }

    UiDemoMode UiDemoGetMode(void)
    {
        return s_demo;
    }

    bool UiDemoOnScreen(const Screen &screen)
    {
        const UiDemoMode mode = s_demo;
        if (!screen.IsTop)
            return false;
        if (mode == UiDemoMode::Off)
        {
            UI::ScreenBuffer::GiveBack(DemoOwner);
            return false;
        }

        const u64 start = svcGetSystemTick();
        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        // Move the highlight about once a second so redraws can be seen.
        const s32 selected = (s32)((s_demoFrames++ / 60) % UI::DemoRows);

        if (mode == UiDemoMode::Direct && !(fb.squeeze > 1.0f))
        {
            UI::Canvas canvas(fb);
            UI::DrawDemoShade(canvas);
            UI::DrawDemoMenu(canvas, selected);
            s_uiRedraws++;
        }
        else
        {
            // Keep the full-screen shade; widescreen Direct uses the layer
            // for its menu so it receives the same squeeze as native screens.
            if (mode == UiDemoMode::Shade || mode == UiDemoMode::Direct)
            {
                UI::Canvas canvas(fb);
                UI::DrawDemoShade(canvas);
            }
            bool fresh = false;
            u8 *pixels = UI::ScreenBuffer::Borrow(DemoOwner, fresh);
            if (pixels == nullptr)
                return false;
            if (fresh || !s_demoLayer.Matches(fb))
            {
                s_demoLayer.Setup(fb, UI::DemoPanel, UI::DemoRadius, pixels, UI::ScreenBuffer::Bytes);
                s_demoDrawn = -1;
            }

            if (selected != s_demoDrawn)
            {
                const u64 redraw = svcGetSystemTick();
                UI::Canvas canvas = s_demoLayer.Begin();
                UI::DrawDemoMenu(canvas, selected);
                s_demoDrawn = selected;
                s_uiRedraws++;
                s_uiRedrawSum += svcGetSystemTick() - redraw;
            }
            const u64 present = svcGetSystemTick();
            s_demoLayer.Present(fb);
            s_uiPresents++;
            s_uiPresentSum += svcGetSystemTick() - present;
        }

        const u64 ticks = svcGetSystemTick() - start;
        s_uiFrames++;
        s_uiSum += ticks;
        if (ticks > s_uiMax)
            s_uiMax = ticks;
        return true;
    }

    std::string UiPerfReport(void)
    {
        static const char *names[] = { "off", "on", "shade", "direct" };
        const auto us = [](u64 ticks) { return (unsigned long)(ticks * 1000000 / SYSCLOCK_ARM11); };
        const auto mean = [](u64 sum, u32 n) { return n ? sum / n : 0; };
        const std::string line = Utils::Format(
            "ui: demo %s, frames %lu, redraws %lu, mean %lu us, max %lu us, layer redraw mean %lu us, copy mean %lu us",
            names[(int)s_demo], s_uiFrames, s_uiRedraws, us(mean(s_uiSum, s_uiFrames)), us(s_uiMax),
            us(mean(s_uiRedrawSum, s_uiRedraws)), us(mean(s_uiPresentSum, s_uiPresents)));
        s_uiFrames = s_uiRedraws = s_uiPresents = 0;
        s_uiSum = s_uiMax = s_uiRedrawSum = s_uiPresentSum = 0;
        return line;
    }
}
