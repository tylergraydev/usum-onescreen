#pragma once

#include "Usum.hpp"

// The game's own Pokémon box icons (the 40x30 "menu sprites"), read from
// the running game's RomFS on demand (docs/GAME-ASSETS.md, "Pokémon icons").
// Nothing of the game is in the repo: this file knows the formats and where
// to look.
//
// - Which icon: the game's own lookup table (one 16-byte entry per species,
//   in code.bin's rodata), found through the lookup function's literal pool
//   and read from game memory (FindPokeIconTable, PokeIconIndex).
// - The pictures: GARC a/0/6/2, one LZ11 BFLIM per icon (64x32 RGBA5551,
//   stored transposed); the picture is the 40x30 at (12, 1) of it.
//
// Pure code on read callbacks (no CTRPF), so tests/ builds it on the PC with
// synthetic data. A screen keeps a PokeIconArchive and a PokeIconCache while
// it shows and frees both when it hides; Fill() decodes a few icons per call
// so no frame waits long.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "RomfsText.hpp"
#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
    static constexpr u32 PokeIconNone = 0xFFFFFFFF;
    static constexpr s32 PokeIconW = 40, PokeIconH = 30;

    // Reads `size` bytes of game memory at `address`; false if it can't.
    using MemRead = std::function<bool(u32 address, void *dst, u32 size)>;

    // The game's icon lookup (USUM v1.2, both games, docs/OFFSETS.md).
    static constexpr u32 PokeIconLookupFunc = Usum::PokeIconLookupFunc;

    struct PokeIconTable
    {
        u32 table = 0;      // 808 entries of 16 bytes (species 0-807)
        u32 egg = 0;        // the egg icon (also what the game shows for species 808, 809)
        bool Valid(void) const { return table != 0; }
    };

    // Checks the lookup function's first instructions and reads the table
    // address and the egg icon from its literal pool.
    bool FindPokeIconTable(const MemRead &read, PokeIconTable &out, std::string &error);

    // The icon the game's Box shows for a Pokémon, as its lookup does it:
    // an egg gets the egg icon; species 808 / 809 the egg icon, any other
    // past 807 species 0's; a form below the entry's form count its form
    // icon; else the female icon for gender 1, the entry's icon otherwise.
    // PokeIconNone if the table can't be read.
    u32 PokeIconIndex(const MemRead &read, const PokeIconTable &table, u16 species, u8 form, u8 gender, bool egg);

    // One decoded icon, RGBA5551 as the game stores it (bit 0 = opaque).
    struct PokeIcon
    {
        u32 index = PokeIconNone;
        u16 px[PokeIconW * PokeIconH] = {};
    };

    // Decodes one GARC entry (LZ11 or plain BFLIM) into the 40x30 icon.
    bool DecodePokeIcon(const u8 *entry, size_t size, PokeIcon &out, std::string &error);

    // Draws the icon at (x, y), 1:1, only where it is opaque, inside the
    // canvas's clip; `alpha` below 255 fades it (a dimmed slot).
    void DrawPokeIcon(Canvas &canvas, const PokeIcon &icon, s32 x, s32 y, u8 alpha = 255);

    // The icon GARC (a/0/6/2): where each entry is.
    class PokeIconArchive
    {
    public:
        bool Open(const RomfsRead &read, std::string &error);
        void Close(void);
        bool IsOpen(void) const { return !m_starts.empty(); }
        u32 Count(void) const { return (u32)m_starts.size(); }
        bool Load(u32 index, PokeIcon &out, std::string &error) const;
        size_t Bytes(void) const { return m_starts.capacity() * 4 + m_sizes.capacity() * 2; }

    private:
        RomfsRead m_read;
        uint64_t  m_data = 0;           // the GARC's data block in the image
        std::vector<u32> m_starts;      // per entry, from m_data
        std::vector<u16> m_sizes;
    };

    // Decoded icons, least recently wanted dropped first. Each icon is its
    // own small allocation (2.4 KB), so a full cache never needs one big
    // block.
    //
    // Threads: one thread loads (Next, Store, Fail, Fill); readers on
    // another thread take the screen's lock around Find and drawing, and the
    // loader takes it only around Store and Clear (they change the list).
    class PokeIconCache
    {
    public:
        static constexpr u32 DefaultCapacity = 40;

        explicit PokeIconCache(u32 capacity = DefaultCapacity) : m_capacity(capacity) {}

        // The icon if it is decoded, else nullptr.
        const PokeIcon *Find(u32 index) const;
        // Marks `wanted` (PokeIconNone entries skipped) as just used and
        // returns the first of them that is neither decoded nor failed, or
        // PokeIconNone.
        u32 Next(const u32 *wanted, u32 count);
        // Keeps a copy of `icon`: in a new slot while under capacity, else
        // over the least recently used one that isn't wanted now. False if
        // there is no room (or no memory).
        bool Store(const PokeIcon &icon);
        // `index` didn't load: not tried again until Clear.
        void Fail(u32 index);
        // Next / Load / Store up to `budget` times, without any lock (tests,
        // single-threaded use). Returns how many it tried; the first error
        // goes to `error`.
        u32 Fill(const PokeIconArchive &archive, const u32 *wanted, u32 count, u32 budget, std::string &error);
        // Frees every icon.
        void Clear(void);
        u32 Size(void) const { return (u32)m_icons.size(); }
        size_t Bytes(void) const { return m_icons.size() * sizeof(PokeIcon); }

    private:
        bool Failed(u32 index) const;

        u32 m_capacity;
        u32 m_clock = 0;
        std::vector<std::unique_ptr<PokeIcon>> m_icons;
        std::vector<u32> m_used;        // m_clock when each was last wanted
        std::vector<u32> m_failed;
    };
}
}
