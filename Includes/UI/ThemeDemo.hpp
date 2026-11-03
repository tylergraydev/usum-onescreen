#pragma once

#include <string>

#include "UI/Raster.hpp"

// A sample screen drawn with the USUM theme (UI/Theme.hpp) and the game's
// assets, for the `uitheme` test command and the host test's preview: one
// page built like the game's battle move list, one like its list menus.
// Pure drawing; Screens/ThemeScreen.cpp fills the data from the game.
namespace OneScreen
{
namespace UI
{
    // The area the demo draws in: the size of the bottom screen, centered on
    // the top screen, so a screenshot lines up with the game's own menu.
    static constexpr Rect ThemeDemoArea = { 40, 0, 320, 240 };
    static constexpr s32  ThemeDemoPages = 2;

    struct ThemeDemoMove
    {
        std::string name;
        std::string typeName;
        u8 type = 0, category = 0, pp = 0, maxPp = 0;
    };

    struct ThemeDemoMon
    {
        std::string name;
        u8  level = 0;
        u32 hp = 0, maxHp = 0;
    };

    struct ThemeDemoData
    {
        ThemeDemoMove moves[4];
        s32 moveCount = 0;
        ThemeDemoMon party[6];
        s32 partyCount = 0;
    };

    // Made-up sample data (the host test, or no party to read).
    ThemeDemoData ThemeDemoSample(void);

    // page 0: move list, page 1: list and menu. `selected` is the row the
    // cursor is on (wraps).
    void DrawThemeDemo(Canvas &canvas, const ThemeDemoData &data, s32 page, s32 selected);
}
}
