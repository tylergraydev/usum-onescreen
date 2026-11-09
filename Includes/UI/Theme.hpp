#pragma once

// The shared USUM look for native top-screen menus (docs/VISUAL-STYLE.md):
// the game's colours, sizes and the shapes its menus are made of, in one
// place, so every native screen draws the same panel, row, cursor, button
// prompt and gauges. Colours come from screenshots of the game's own menus
// and from its layout animations; the doc lists where each one was taken.
//
// Text and icons use the game's own font and icons when they were loaded
// from the RomFS (UI/GameAssets.hpp) and fall back to the system font and
// drawn shapes when not, so the theme works (and the host tests run) without
// them.

#include <string>

#include "UI/Raster.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
namespace UI
{
namespace Theme
{
    // --- Colours ---

    // Dark message window (the game's Window_Basic: X-menu prompts,
    // summary details, "What would you like to do with ...?").
    constexpr Color WindowLine   = Rgb(0x00, 0x00, 0x00);  // outer line, 2 px
    constexpr Color WindowRim    = Rgb(0xA3, 0x8B, 0x6A);  // tan line inside it, 1 px
    constexpr Color WindowBevel  = Rgb(0x4A, 0x43, 0x2B);  // inner edge, fades to the fill
    constexpr Color WindowFill   = Rgb(0x2B, 0x2A, 0x19);
    constexpr Color WindowText   = Rgb(0xFF, 0xFF, 0xFF);
    constexpr Color AccentText   = Rgb(0x00, 0x8C, 0xFF);  // values in the summary ("Impish", "Route 1")

    // List row (the Bag's item rows): lime outline, white to mint fill,
    // dark green shadow line under it, black text.
    constexpr Color ListOutline  = Rgb(0xCE, 0xF0, 0x24);
    constexpr Color ListTop      = Rgb(0xFF, 0xFF, 0xFF);
    constexpr Color ListMid      = Rgb(0xD6, 0xFF, 0xFF);
    constexpr Color ListBottom   = Rgb(0xAE, 0xF0, 0xAE);
    constexpr Color ListShadow   = Rgb(0x29, 0x7E, 0x29);
    constexpr Color ListText     = Rgb(0x00, 0x00, 0x00);

    // Menu option (the Party screen's Summary / Restore / Back buttons):
    // black outline, white to sky-blue fill, dark blue base, yellow glint.
    constexpr Color OptionLine   = Rgb(0x00, 0x00, 0x00);
    constexpr Color OptionTop    = Rgb(0xF2, 0xFF, 0xFF);
    constexpr Color OptionMid    = Rgb(0xCD, 0xFD, 0xFF);
    constexpr Color OptionBottom = Rgb(0x78, 0xE8, 0xFF);
    constexpr Color OptionBase   = Rgb(0x00, 0x4C, 0xA4);
    constexpr Color OptionGlint  = Rgb(0xFF, 0xF5, 0x4E);
    constexpr Color OptionText   = Rgb(0x00, 0x00, 0x00);

    // Selection: the game's red arrow cursor (lists, options, moves), and
    // the orange frame of the selected Party plate.
    constexpr Color CursorRed    = Rgb(0xEE, 0x00, 0x00);
    constexpr Color CursorEdge   = Rgb(0x24, 0x21, 0x21);
    constexpr Color Highlight    = Rgb(0xFF, 0x5E, 0x04);

    // Pokédex list rows: selected red, others orange, empty brown, all
    // with a dark red line.
    constexpr Color DexSelected  = Rgb(0xFF, 0x22, 0x11);
    constexpr Color DexRow       = Rgb(0xFF, 0x66, 0x00);
    constexpr Color DexEmpty     = Rgb(0x4C, 0x1D, 0x00);
    constexpr Color DexLine      = Rgb(0x44, 0x11, 0x11);

    // HP gauge (the battle layout's Battle_GAG_002_color_* animations: left
    // and right colour of the gauge), black frame.
    constexpr Color HpGreenL  = Rgb(0xB8, 0xFF, 0x37), HpGreenR  = Rgb(0x00, 0xFF, 0x1F);
    constexpr Color HpYellowL = Rgb(0xFF, 0x9E, 0x37), HpYellowR = Rgb(0xFF, 0xFF, 0x1F);
    constexpr Color HpRedL    = Rgb(0xFF, 0x00, 0x50), HpRedR    = Rgb(0xFF, 0x00, 0x0C);
    constexpr Color GaugeFrame = Rgb(0x00, 0x00, 0x00);
    constexpr Color GaugeEmpty = Rgb(0x3A, 0x3A, 0x3A);   // not seen in the game (every HP was full); our pick

    // Button prompt chip: a dark pill behind the game's button icon.
    constexpr Color PromptFill = Rgb(0x00, 0x00, 0x00, 0xA0);
    constexpr Color PromptText = Rgb(0xFF, 0xFF, 0xFF);

    // Battle command colours, close to the game's red / gold / green / blue.
    constexpr Color BattleFight = Rgb(0xE8, 0x48, 0x43);
    constexpr Color BattleBag = Rgb(0xF4, 0xB5, 0x35);
    constexpr Color BattlePokemon = Rgb(0x59, 0xB8, 0x62);
    constexpr Color BattleRun = Rgb(0x42, 0xA6, 0xE0);

    // --- Sizes (top screen pixels; the game's menus are drawn 1:1) ---

    constexpr s32 RowHeight    = 26;    // Bag rows are 26 px plus a 2 px shadow line
    constexpr s32 RowPitch     = 28;
    constexpr s32 RowRadius    = 12;
    constexpr s32 OptionHeight = 26;
    constexpr s32 WindowRadius = 6;
    constexpr s32 WindowPad    = 8;     // text inset inside a window
    constexpr s32 RowTextInset = 22;    // room for the cursor tip at the left of a row
    constexpr s32 CursorW      = 16, CursorH = 16;
    constexpr s32 PromptHeight = 20;
    constexpr s32 GaugeHeight  = 6;     // fill height inside a 1 px frame (Party HP bar)
    constexpr s32 TypePlateW   = 50, TypePlateH = 14;

    // --- Text ---

    enum class TextSize : u8
    {
        Body,   // the game's menu text: its font at full size, 19 px line
        Small,  // 4/5 size, 15 px line (Lv., PP, small labels)
    };

    struct TextStyle
    {
        TextSize size    = TextSize::Body;
        Color    color   = ListText;
        Color    outline = Rgb(0, 0, 0, 0);  // a > 0: a 1 px outline in this colour
        Align    align   = Align::Left;
    };

    // True if text is drawn with the game's font (it was loaded).
    bool UsingGameFont(void);
    s32  LineHeight(TextSize size);
    s32  MeasureText(const std::string &text, TextSize size);
    // Draws text in box: aligned per style, centered vertically, cut to the
    // box width with "...". Returns the width drawn.
    s32  DrawText(Canvas &canvas, const std::string &text, const Rect &box, const TextStyle &style);

    // --- Shapes ---

    // The dark message window.
    void DrawWindow(Canvas &canvas, const Rect &r);

    enum class RowKind : u8
    {
        List,   // green Bag-style row
        Option, // blue menu option
    };

    // A row with its label; a selected row gets the red cursor at its left
    // end, as in the game (the row itself doesn't change). The label starts
    // RowTextInset in.
    void DrawRow(Canvas &canvas, const Rect &r, RowKind kind, const std::string &label, bool selected);
    // Just the row's background (for rows with more than a label).
    void DrawRowBackground(Canvas &canvas, const Rect &r, RowKind kind);
    // The orange 2 px frame the game puts around the selected Party plate,
    // just inside r.
    void DrawHighlightFrame(Canvas &canvas, const Rect &r, s32 radius);

    // The red menu cursor, its tip at (tipX, centerY), pointing right.
    void DrawCursor(Canvas &canvas, s32 tipX, s32 centerY);

    enum class Button : u8 { A, B, X, Y, L, R, Start, Select, ZL, ZR };
    // Positional mapping; widths always use the original 3DS badge size.
    const char *ButtonName(Button button, GlyphFamily family);
    // Converts standalone button names in a known text-only prompt.
    std::string FormatPromptText(const std::string &text);
    // Mixed messages can contain user names; only translate their fixed panel hint.
    std::string FormatPanelHint(const std::string &text);
    // A prompt chip: the button's icon, then the label. Returns its width;
    // PromptWidth measures without drawing.
    s32 DrawButtonPrompt(Canvas &canvas, s32 x, s32 y, Button button, const std::string &label);
    s32 PromptWidth(Button button, const std::string &label);

    // --- Gauges ---

    enum class GaugeLevel : u8 { High, Half, Low, Empty };
    // The game's HP thresholds: above 1/2 green, above 1/5 yellow, else red.
    GaugeLevel HpLevel(u32 value, u32 max);
    // PP: above 1/2, above 1/4, above 0, 0.
    GaugeLevel PpLevel(u32 value, u32 max);
    // Filled width of a gauge `width` wide: never 0 while value > 0, never
    // full while value < max.
    s32 GaugeFill(u32 value, u32 max, s32 width);
    // HP bar in r (frame included): black frame, game colours by level.
    void DrawHpBar(Canvas &canvas, const Rect &r, u32 hp, u32 maxHp);
    // PP bar in the same style: green, then yellow, orange, red as it runs out.
    void DrawPpBar(Canvas &canvas, const Rect &r, u32 pp, u32 maxPp);
    // Colour for "PP x / y" text on a dark background, by PpLevel.
    Color PpTextColor(u32 pp, u32 maxPp);

    // --- Types and moves ---

    // A type's colours, from the battle move buttons' per-type colour
    // animations (Battle_PLT_LOW_002_color_00..17): the main plate's dark
    // and light material colours and the outline's.
    struct TypeColors
    {
        Color dark, light, lineDark, lineLight;
    };
    // By the game's type ID (0 Normal ... 17 Fairy); others get Normal's.
    TypeColors TypeColor(u8 type);
    constexpr u8 TypeCount = 18;

    // A type plate (the move list's "FIRE" chip): type-coloured pill with a
    // white rim and the name in white capitals with a dark outline.
    void DrawTypePlate(Canvas &canvas, const Rect &r, u8 type, const std::string &name);
    // A battle move button background in the type's colours; the selected
    // one is a little lighter (the game adds the cursor, DrawCursor).
    void DrawMoveButton(Canvas &canvas, const Rect &r, u8 type, bool selected);
    // The move category icon (0 status, 1 physical, 2 special), 42x20,
    // top-left at (x, y).
    void DrawCategoryIcon(Canvas &canvas, s32 x, s32 y, u8 category);
    // Gender sign, 12x12, top-left at (x, y).
    void DrawGender(Canvas &canvas, s32 x, s32 y, bool female);
    // Red five-point shiny marker, 10x10; independent of font glyphs.
    void DrawShiny(Canvas &canvas, s32 x, s32 y);
    // A Poké Ball icon (item IDs 1-16), 32x32 at (x, y). False if the game's
    // icon isn't loaded (nothing is drawn).
    bool DrawBall(Canvas &canvas, s32 x, s32 y, u16 ballItem);

    // Upper-cases ASCII and Latin-1 letters (type names are shown in capitals).
    std::string Upper(const std::string &text);
}
}
}
