#pragma once

// The game's own font, icons and a few data tables, read from the running
// game's RomFS at runtime (docs/GAME-ASSETS.md). Nothing of the game is in
// the repo: this file only knows the formats and where to look.
//
// The decoders are pure code on byte buffers (no CTRPF), so tests/ builds
// them on the PC with synthetic data. LoadGameAssets() does the reading
// through a RomfsRead callback: the shared service (UI/Assets.hpp) gives it
// the game's RomFS (ARCHIVE_ROMFS), in its own Task. Screens read the result
// through UI/Assets.hpp. Everything has a fallback:
// if a piece doesn't load, the theme draws with the system font and its own
// shapes.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "RomfsText.hpp"
#include "UI/Font.hpp"
#include "UI/Raster.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
namespace UI
{
    // --- Containers ---

    // LZ11 (type 0x11) decompression, as used for most GARC entries.
    bool Lz11Decompress(const u8 *src, size_t size, std::vector<u8> &out);

    // A file inside a SARC archive (or the SARC inside an ALYT layout
    // archive). Offsets are from the start of `data`.
    struct ArchiveFile
    {
        u32 offset = 0;
        u32 size = 0;
    };
    bool SarcFind(const u8 *data, size_t size, const std::string &name, ArchiveFile &out);

    // Two-letter packs ("WD", "BL", ...: magic, u16 count, u32 offsets
    // [count + 1]). Entry `index`.
    bool MiniPackEntry(const u8 *data, size_t size, u32 index, ArchiveFile &out);

    // --- Images ---

    struct Image
    {
        s32 w = 0, h = 0;
        std::vector<u8> rgba;   // w * h * 4, row-major, straight alpha

        bool Empty(void) const { return w <= 0 || h <= 0; }
    };

    // BFLIM ("FLIM" footer, the 3DS layout texture): formats L8, A8, LA4,
    // LA8, RGB565, RGB8, RGBA5551, RGBA4, RGBA8, ETC1, ETC1A4, L4, A4.
    // Textures are 8x8 Morton-tiled at power-of-two size; with flag 4 in the
    // swizzle byte the texture is stored transposed (image pixel (x, y) is
    // texel (texW - 1 - y, x) of a texW = pow2(h) by pow2(w) texture).
    bool DecodeFlim(const u8 *file, size_t size, Image &out, std::string &error);

    // Draws the image (or the `src` part of it) with its top-left at (x, y),
    // alpha-blended, scaled by `alpha`.
    void DrawImage(Canvas &canvas, const Image &image, s32 x, s32 y, const Rect *src = nullptr, u8 alpha = 255);

    // --- The game font (BFFNT v4, "FFNT") ---

    // The header fields the font code needs.
    struct FfntInfo
    {
        u8  height = 0, width = 0, ascent = 0;
        u16 lineFeed = 0;
        u16 altIndex = 0;                   // replacement glyph
        std::int8_t defLeft = 0;
        u8  defGlyphW = 0, defCharW = 0;
        u32 glyphAt = 0, widthAt = 0, mapAt = 0;  // TGLP / CWDH / CMAP data (block + 8)
        u8  cellW = 0, cellH = 0, sheetCount = 0;
        u32 sheetSize = 0;
        u16 baseline = 0, sheetFormat = 0, cellsPerRow = 0, cellRows = 0, sheetW = 0, sheetH = 0;
        u32 sheetData = 0;                  // file offset of sheet 0
    };
    // `head` holds at least the first 0x60 bytes of the file.
    bool ParseFfntHeader(const u8 *head, size_t size, FfntInfo &out, std::string &error);

    // The game font's Latin part: the first rows of glyph sheet 0 (every
    // glyph a European language uses sits there), its width table and
    // character map. ~110 KB for USUM's font instead of 2 MB.
    class GameFont
    {
    public:
        // Most glyph rows kept (64 glyphs a row in USUM's font).
        static constexpr u32 MaxRows = 12;

        // Reads through `read` (offsets in the font file). False with a
        // reason if the font can't be used; the object is then empty.
        using FileRead = std::function<bool(u32 offset, void *dst, u32 size)>;
        bool Load(const FileRead &read, u32 fileSize, std::string &error);

        bool Ready(void) const { return m_ready; }
        const FfntInfo &Info(void) const { return m_info; }
        u32 GlyphCount(void) const { return m_glyphs; }
        size_t Bytes(void) const;

        // Glyph index of a code point (the replacement glyph if it's not in
        // the loaded part), and its widths from the font's table.
        int  IndexOf(u32 codePoint) const;
        void Widths(int index, s32 &left, s32 &glyphW, s32 &advance) const;
        // Decodes glyph `index` (cellW x cellH coverage). False if it isn't
        // loaded.
        bool DecodeCell(int index, u8 *out) const;

    private:
        bool         m_ready = false;
        FfntInfo     m_info;
        u32          m_glyphs = 0;          // indexes below this are loaded
        std::vector<std::pair<u16, u16>> m_map;  // (code point, index), sorted
        std::vector<u8> m_widths;           // 3 bytes per glyph
        std::vector<u8> m_band;             // the first tile rows of sheet 0
        FontSource   m_source = {};         // m_band seen as a one-sheet font
    };

    // Text in the game font at a fraction of its size (1 = the game's own
    // menu text: 19 px line), spaced by the font's width table. Glyphs are
    // cached per code point like UI::GlyphCache.
    class GameTextCache
    {
    public:
        static constexpr s32 Slots = 256;

        explicit GameTextCache(float scale) : m_scale(scale) {}
        void SetFont(const GameFont *font);
        const GameFont *Font(void) const { return m_font; }

        s32 LineHeight(void) const;
        s32 Ascent(void) const;
        s32 Advance(u32 codePoint);
        const Glyph *Get(u32 codePoint);

        u32 hits = 0, misses = 0;

    private:
        struct Slot
        {
            u32   codePoint;
            bool  used;
            Glyph glyph;
        };
        s32 Scaled(s32 v) const;

        const GameFont *m_font = nullptr;
        float           m_scale;
        Slot            m_slots[Slots] = {};
        std::vector<u8> m_masks;
        s32             m_maskW = 0, m_maskH = 0;
    };

    // --- What gets loaded ---

    enum class GameIcon : u8
    {
        ButtonA, ButtonB, ButtonX, ButtonY,
        Cursor,             // the red menu cursor (Common_Cursor_02)
        Male, Female,
        Categories,         // move categories: status, physical, special (stacked, 20 px apart)
        Count,
    };

    // Where in GameIcon::Categories each category is.
    Rect CategoryIconRect(u8 category);

    // Move data from the game's move table (a/0/1/1).
    struct MoveInfo
    {
        bool valid = false;
        u8 type = 0, category = 0, power = 0, accuracy = 0, pp = 0;
    };
    // Decodes one entry of the move table pack.
    MoveInfo DecodeMoveEntry(const u8 *entry, size_t size);

    struct GameAssets
    {
        GameFont font;
        Image icons[(int)GameIcon::Count];
        std::vector<Image> balls;              // by ball item ID 1-16 (0 empty)
        std::vector<std::string> typeNames;    // the game's names, by type ID
        std::vector<MoveInfo> moves;           // by move ID
        std::vector<std::string> moveNames;    // by move ID
        std::string status;                    // one line per part, for the log

        const Image *Icon(GameIcon icon) const;
        const Image *Ball(u16 item) const;
        MoveInfo Move(u16 move) const;
        std::string MoveName(u16 move) const;
        std::string TypeName(u8 type) const;
    };

    // Reads everything from a RomFS image (ARCHIVE_ROMFS's view of it) into
    // `out`, for language `lang` (0-9, the game's language byte; text
    // only). Each part that fails is logged and left empty. Runs on the
    // plugin thread; takes ~0.5 MB at most while it decompresses.
    void LoadGameAssets(const RomfsRead &read, u32 lang, GameAssets &out, std::vector<std::string> &log);

    // The loaded set the theme draws with, or nullptr (fallback look).
    // Set by the device glue once loading is done; read by the drawing code
    // on the OSD thread (the set is never changed after it is published).
    const GameAssets *GetGameAssets(void);
    void PublishGameAssets(const GameAssets *assets);
}
}
