#pragma once

// Decoding of the game's party and bag data (docs/RE3-party-bag.md): Gen 7
// Pokémon data (PK7 core + party stats), bag item words, and message-file text
// lines. Pure functions on byte buffers, with no CTRPF dependency, so tests/
// can build them on the PC. Party.cpp reads the buffers from game memory.

#ifdef __3DS__
#include <3ds/types.h>
#else
#include <cstdint>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

namespace OneScreen
{
    static const u32 Pk7CoreSize = 0xE8;    // encryption constant, checksum, 4 blocks of 0x38
    static const u32 Pk7CalcSize = 0x1C;    // party stats: status, level, HP, stats

    // How the core data was stored when it was decoded.
    enum class Pk7Form : u8
    {
        Invalid,    // no form gave a matching checksum (empty slot, or read mid-update)
        Encrypted,  // XOR-encrypted and block-shuffled (the game's resting state)
        Decrypted,  // XOR removed, blocks still shuffled: how the game's
                    // accessor leaves it while working on it (+0xC == 0)
    };

    struct Pk7
    {
        Pk7Form form = Pk7Form::Invalid;
        u32 encryptionConstant = 0;
        u16 species = 0;            // national dex number
        u16 heldItem = 0;
        u8  ability = 0;            // ability ID (0x14)
        u8  nature = 0;             // nature (0x1C)
        u8  gender = 0;             // 0 male, 1 female, 2 genderless (0x1D bits 1-2)
        u8  altForm = 0;            // alternate form (0x1D bits 3-7)
        // Arrays use display order: HP, Atk, Def, SpA, SpD, Spe.
        u8 ivs[6] = {}, evs[6] = {};
        bool hyperTrained[6] = {};
        bool shiny = false, hiddenAbility = false;
        u16 nickname[13] = {};      // UTF-16, 0-terminated (12 chars max)
        u16 moves[4] = {};
        u8  pp[4] = {};
        bool isEgg = false;
        u8  ball = 0;               // Poké Ball it was caught in (0xDC; 1-16 = item ID)
        bool calcValid = false;     // the party stats decoded to something plausible
        u32 status = 0;             // status condition word, raw
        u8  level = 0;
        u16 hp = 0, maxHp = 0;
    };

    // Decodes one Pokémon from its core (0xE8 bytes) and party stats (0x1C
    // bytes, may be null). Tries both stored forms and keeps the one whose
    // checksum matches, so a slot read while the game re-encrypts it comes
    // back Invalid instead of garbage. The game XORs core and stats together,
    // so the stats are decrypted only for the Encrypted form. (The checksum is
    // a plain sum, so it cannot tell block orders apart; the game never keeps
    // the blocks unshuffled in RAM.)
    Pk7 DecodePk7(const u8 *core, const u8 *calc);

    // Inverse of the decoding, for tests: shuffles and encrypts a plain core
    // (with a correct checksum written) and encrypts the stats.
    void EncodePk7(u8 *core, u8 *calc);

    // XORs `size` bytes (u16 units) with the PK6/PK7 LCRNG stream from `seed`.
    void Pk7Crypt(u8 *data, u32 size, u32 seed);

    // Sum of the u16 words of the 4 blocks (what the checksum at +6 holds).
    u16 Pk7Checksum(const u8 *core);

    // One bag slot: a packed u32 in the game's item arrays.
    struct BagSlot
    {
        u16 item = 0;       // bits 0-9
        u16 count = 0;      // bits 10-19
        u16 freeSpace = 0;  // bits 20-29: position in the Free Space pocket, 0 = not there
        bool isNew = false; // bit 30: the "NEW" badge in the Bag
    };

    BagSlot DecodeBagSlot(u32 word);

    // Decrypts one message-file line (gfl2::str::MsgData, docs/RE2-text.md) of
    // `length` UTF-16 units (at most MsgLineMax) into `out`, without control
    // tags (0x0010 ...); a plural tag keeps the singular ending. Writes at
    // most cap - 1 units and a 0. Returns the number of units written
    // (without the 0).
    static const u32 MsgLineMax = 128;
    u32 DecryptMsgLine(const u16 *encrypted, u32 length, u32 line, u16 *out, u32 cap);

    // Appends UTF-16 text (0-terminated or `cap` units) to `out` as UTF-8.
    // Returns the number of bytes written (without the 0); `out` always ends
    // in a 0 if outSize > 0.
    u32 Utf16ToUtf8(const u16 *text, u32 cap, char *out, u32 outSize);
}
