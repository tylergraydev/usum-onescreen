#pragma once

// The native Pokédex (W5-2): what the game's Zukan app shows, decoded from
// its memory and the save's dex flags, the rows around the cursor, and
// drawing. Pure C++ (no CTRPF), so tests/ can build it on the PC;
// Screens/Pokedex.cpp reads the game and runs it in the plugin. See
// docs/RE7-pokedex.md.

#include <string>
#include <vector>

#ifdef __3DS__
#include <3ds/types.h>
#else
#include <cstdint>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

#include "UI/Raster.hpp"

namespace OneScreen
{
    // The game's Pokédexes, in the order of its top-level rows (0-4) and of
    // the dex numbers in its records.
    enum class DexKind : u8 { Alola, Melemele, Akala, Ulaula, Poni, Count };
    static const int DexKinds = (int)DexKind::Count;
    // "Alola", "Melemele", ... ("?" if out of range).
    const char *DexName(DexKind dex);
    // "alola", "melemele", ... for commands and the log.
    const char *DexId(DexKind dex);

    // Which part of the Zukan app is up, from the class of its current frame
    // (scene manager +8): offset of the frame's vtable in the Zukan module.
    enum class DexScreen : u8
    {
        None,       // not in the Zukan app, or nothing readable
        Top,        // Alola / island rows / Evaluation
        List,       // the Alola Pokédex list
        Grid,       // an island Pokédex (icon grid)
        Entry,      // one Pokémon's page
        Other,      // anything else (registering a catch, ...)
    };
    DexScreen DexScreenFromVtable(u32 offsetInModule);
    const char *DexScreenName(DexScreen screen);
    // Top-level frame state byte (+0x19D): its menu waits for input. Other
    // values: Pokédex Evaluation (Rotom talking, choosing a dex) and the
    // moments a list opens or closes.
    static const u8 DexTopMenuState = 7;
    // Top-level cursor rows: 0-4 the dexes, 5 Pokédex Evaluation.
    static const int DexTopRows = 6;
    static const int DexTopEvaluation = 5;

    // --- Seen and caught (Savedata::ZukanData) ---

    // The form table the game reads (0x5D63DC in v1.2): species with more
    // than one form and how many they have, in the order their extra forms'
    // flag bits are allotted.
    struct DexFormEntry
    {
        u16 species;
        u16 forms;
    };

    // Flag bit of (species, form) in each seen region, as the game's
    // 0x4C7FD0: form 0 is bit species-1; extra forms follow bit 806, in
    // table order. -1 if there is none.
    int DexFormBit(u16 species, u16 form, const DexFormEntry *table, int count);
    // Forms of species (1 if it isn't in the table), as 0x4C7664.
    u16 DexFormCount(u16 species, const DexFormEntry *table, int count);

    // ZukanData offsets (from the object; +0 is its vtable, +4 the magic).
    static const u32 DexZukanCaught = 0x8C;                     // bit species-1
    static const u32 DexZukanSeen[4] = { 0xF4, 0x180, 0x20C, 0x298 };  // male, female, shiny male, shiny female
    static const u32 DexZukanBytes = 0x298 + 0x8C;              // what DexStateOf reads

    enum class DexState : u8 { Unseen, Seen, Caught };
    // As the game's 0x4C7A7C (caught) and 0x4C7B7C (seen: any form in any of
    // the four regions). `zukan` holds DexZukanBytes from the object's start.
    DexState DexStateOf(const u8 *zukan, u16 species, const DexFormEntry *table, int count);

    // --- The app's records ---

    // One of the Zukan app's 403 records (the Alola Pokédex, 0x1F0 bytes
    // each): the species and its number in each dex (0 = not in it).
    struct DexRecord
    {
        u16 species = 0;
        u16 number[DexKinds] = {};
    };

    struct DexRow
    {
        u16 number = 0;             // in this dex, 1-based
        u16 species = 0;
        DexState state = DexState::Unseen;
        std::string name;           // filled in for seen rows only
    };

    // The rows of `dex` in dex-number order, as the game lists them. `limit`
    // > 0 keeps only the first `limit` (the Alola list stops at the highest
    // number seen).
    std::vector<DexRow> DexRows(const std::vector<DexRecord> &records, DexKind dex, u32 limit);

    struct DexCounts
    {
        u16 seen = 0;       // caught ones included, as the game counts
        u16 caught = 0;
        u16 total = 0;
    };
    DexCounts DexCount(const std::vector<DexRow> &rows);

    // First row to show of `count`, `visible` at a time, keeping `cursor`
    // in the middle where it can. 0 if everything fits.
    int DexWindowTop(int cursor, int count, int visible);

    // Up to `visible` rows around the cursor, for drawing.
    struct DexWindow
    {
        std::vector<DexRow> rows;
        int first = 0;          // index of rows[0] in the full list
        int cursor = 0;         // index of the cursor in the full list
        int count = 0;          // rows in the full list
    };
    DexWindow MakeDexWindow(const std::vector<DexRow> &rows, int cursor, int visible);

    // --- What the native screen shows ---

    struct DexView
    {
        DexScreen screen = DexScreen::None;
        DexKind dex = DexKind::Alola;
        // Top level.
        int topCursor = 0;
        bool open[DexKinds] = {};       // row unlocked (Alola always)
        bool evaluation = false;        // the Evaluation row is there
        DexCounts counts[DexKinds];
        // List, grid, entry.
        DexWindow window;
    };

    // Rows each screen shows at a time.
    static const int DexListRows = 8;
    static const int DexGridRows = 3;

    // Where each screen draws on the top screen: the parts of the game's own
    // top screen that show something (the list's and entry page's 3D model,
    // the island map and the name plate) stay uncovered.
    UI::Rect PokedexArea(DexScreen screen);
    static const int PokedexRadius = 10;
    // Largest area, for the layer buffer.
    static const int PokedexMaxW = 304, PokedexMaxH = 224;
    // Fixed top-level count area, wide enough for 999 in the current font.
    int DexTopCountWidth(void);
    int DexTopCountTextWidth(void);
    void DrawPokedex(UI::Canvas &canvas, const DexView &view);

    // "#025 Pikachu caught" / "#002 ????? unseen", for the log.
    std::string DescribeDexRow(const DexRow &row);
}
