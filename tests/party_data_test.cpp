// Host tests for the party/bag decoding in Sources/PartyData.cpp. Synthetic
// data only (no game data in the repo).

#include "PartyData.hpp"

#include <cstdio>
#include <cstring>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static void Put16(u8 *p, u32 offset, u16 v)
{
    p[offset] = (u8)v;
    p[offset + 1] = (u8)(v >> 8);
}

static void Put32(u8 *p, u32 offset, u32 v)
{
    Put16(p, offset, (u16)v);
    Put16(p, offset + 2, (u16)(v >> 16));
}

// A plain core + stats with recognisable values, filler in every block so a
// wrong shuffle or key shows up.
static void MakeMon(u8 *core, u8 *calc, u32 ec)
{
    std::memset(core, 0, Pk7CoreSize);
    std::memset(calc, 0, Pk7CalcSize);
    Put32(core, 0x00, ec);
    for (u32 i = 0x08; i < Pk7CoreSize; i++)
        core[i] = (u8)(i * 7);
    Put16(core, 0x08, 726);         // species
    Put16(core, 0x0A, 217);         // held item
    core[0x14] = 92;                // ability (Skill Link)
    core[0x1C] = 10;                // nature (Timid)
    const char *nick = "Torracat";
    for (u32 i = 0; i < 13; i++)
        Put16(core, 0x40 + i * 2, i < std::strlen(nick) ? (u16)nick[i] : 0);
    const u16 moves[4] = {10, 424, 24, 122};
    const u8 pp[4] = {35, 15, 30, 29};
    for (u32 i = 0; i < 4; i++)
    {
        Put16(core, 0x5A + i * 2, moves[i]);
        core[0x62 + i] = pp[i];
    }
    Put32(core, 0x74, 0x3FFFFFFF);  // IVs, not an egg
    Put32(calc, 0x00, 0x08);        // status word
    calc[0x04] = 17;
    Put16(calc, 0x08, 45);
    Put16(calc, 0x0A, 51);
}

static void CheckMon(const Pk7 &mon)
{
    CHECK(mon.species == 726);
    CHECK(mon.heldItem == 217);
    CHECK(mon.ability == 92);
    CHECK(mon.nature == 10);
    CHECK(mon.nickname[0] == 'T' && mon.nickname[7] == 't' && mon.nickname[8] == 0);
    CHECK(mon.moves[1] == 424 && mon.moves[3] == 122);
    CHECK(mon.pp[0] == 35 && mon.pp[3] == 29);
    CHECK(!mon.isEgg);
    CHECK(mon.calcValid);
    CHECK(mon.level == 17 && mon.hp == 45 && mon.maxHp == 51 && mon.status == 0x08);
}

static void TestEncryptedRoundTrip(void)
{
    // Every shuffle value (bits 13-17 of the encryption constant, mod 24).
    for (u32 sv = 0; sv < 32; sv++)
    {
        const u32 ec = 0xB1350000u ^ (sv << 13) ^ 0x1E6B;
        u8 core[Pk7CoreSize], calc[Pk7CalcSize], plain[Pk7CoreSize];
        MakeMon(core, calc, ec);
        std::memcpy(plain, core, Pk7CoreSize);
        EncodePk7(core, calc);
        CHECK(std::memcmp(core + 8, plain + 8, Pk7CoreSize - 8) != 0);
        const Pk7 mon = DecodePk7(core, calc);
        CHECK(mon.form == Pk7Form::Encrypted);
        CHECK(mon.encryptionConstant == ec);
        CheckMon(mon);
    }
}

static void TestDecrypted(void)
{
    // The accessor's working state: encode, then undo only the XOR.
    u8 core[Pk7CoreSize], calc[Pk7CalcSize];
    MakeMon(core, calc, 0x12345678);
    EncodePk7(core, calc);
    Pk7Crypt(core + 8, Pk7CoreSize - 8, 0x12345678);
    Pk7Crypt(calc, Pk7CalcSize, 0x12345678);
    const Pk7 mon = DecodePk7(core, calc);
    CHECK(mon.form == Pk7Form::Decrypted);
    CheckMon(mon);
}

static void TestInvalid(void)
{
    u8 core[Pk7CoreSize] = {}, calc[Pk7CalcSize] = {};
    CHECK(DecodePk7(core, calc).form == Pk7Form::Invalid);     // empty slot

    MakeMon(core, calc, 0xCAFEF00D);
    EncodePk7(core, calc);
    core[0x30] ^= 0x01;                                         // torn read
    CHECK(DecodePk7(core, calc).form == Pk7Form::Invalid);

    MakeMon(core, calc, 0xCAFEF00D);
    EncodePk7(core, nullptr);
    const Pk7 mon = DecodePk7(core, nullptr);                   // no stats
    CHECK(mon.form == Pk7Form::Encrypted && !mon.calcValid && mon.species == 726);
}

static void TestEgg(void)
{
    u8 core[Pk7CoreSize], calc[Pk7CalcSize];
    MakeMon(core, calc, 0x0BADBEEF);
    Put32(core, 0x74, 0x40000000);
    EncodePk7(core, calc);
    CHECK(DecodePk7(core, calc).isEgg);
}

static void TestJudgeAndMarkers(void)
{
    // Plain PK7 byte layout (before EncodePk7 shuffles/encrypts):
    // +0C TID, +0E SID, +15 ability number, +16 markings, +18 PID;
    // +1E..23 EVs: HP/Atk/Def/Spe/SpA/SpD;
    // +74 IV word: 5 bits each in that order, bit30 egg, bit31 nickname;
    // +DE HT bits: HP/Atk/Def/SpA/SpD/Spe (different order!).
    for (u32 sv = 0; sv < 24; sv++)
        for (int decrypted = 0; decrypted < 2; decrypted++)
            for (int ht = 0; ht < 6; ht++)
            {
                u8 core[Pk7CoreSize], calc[Pk7CalcSize];
                const u32 ec = sv << 13;
                MakeMon(core, calc, ec);
                Put16(core, 0x0C, 0x1234);
                Put16(core, 0x0E, 0x5678);
                // XOR = 15 (shiny) or 16 (not shiny), exact boundary.
                Put32(core, 0x18, ((u32)(0x1234 ^ 0x5678 ^ (ht % 2 ? 16 : 15)) << 16));
                core[0x15] = (u8)(ht % 3 == 0 ? 0x84 : ht % 3 == 1 ? 1 : 2);
                core[0x16] = 4; // must NOT be read as the HA flag
                core[0x1E] = 252; core[0x1F] = 1; core[0x20] = 2;
                core[0x21] = 3; core[0x22] = 4; core[0x23] = 248;
                Put32(core, 0x74, 31u | (30u << 5) | (29u << 10) | (0u << 15)
                                      | (16u << 20) | (1u << 25) | (1u << 31));
                core[0xDE] = (u8)(1 << ht);
                EncodePk7(core, calc);
                if (decrypted)
                {
                    Pk7Crypt(core + 8, Pk7CoreSize - 8, ec);
                    Pk7Crypt(calc, Pk7CalcSize, ec);
                }
                const Pk7 mon = DecodePk7(core, calc);
                CHECK(mon.form == (decrypted ? Pk7Form::Decrypted : Pk7Form::Encrypted));
                const u8 ivs[6] = { 31, 30, 29, 16, 1, 0 };
                const u8 evs[6] = { 252, 1, 2, 4, 248, 3 };
                for (int i = 0; i < 6; i++)
                {
                    CHECK(mon.ivs[i] == ivs[i] && mon.evs[i] == evs[i]);
                    CHECK(mon.hyperTrained[i] == (i == ht));
                }
                CHECK(!mon.isEgg); // nickname bit doesn't spill into IVs/egg
                CHECK(mon.shiny == (ht % 2 == 0));
                CHECK(mon.hiddenAbility == (ht % 3 == 0));
            }
}

static void TestBagSlot(void)
{
    // Potion x9; Burn Heal x3 with the NEW badge; free-space position 5.
    BagSlot s = DecodeBagSlot(17 | (9 << 10));
    CHECK(s.item == 17 && s.count == 9 && s.freeSpace == 0 && !s.isNew);
    s = DecodeBagSlot(19 | (3 << 10) | (1u << 30));
    CHECK(s.item == 19 && s.count == 3 && s.isNew);
    s = DecodeBagSlot(958 | (999 << 10) | (5u << 20));
    CHECK(s.item == 958 && s.count == 999 && s.freeSpace == 5 && !s.isNew);
}

static void EncryptMsgLine(const u16 *text, u32 line, u16 *out, u32 *length)
{
    u16 key = (u16)(0x2983 * (line + 3));
    u32 n = 0;
    for (;; n++)
    {
        out[n] = text[n] ^ key;
        key = (u16)((key << 3) | (key >> 13));
        if (text[n] == 0)
            break;
    }
    *length = n + 1;
}

static bool Equals(const u16 *text, const char *expect)
{
    for (u32 i = 0;; i++)
    {
        if (text[i] != (u16)(u8)expect[i])
            return false;
        if (expect[i] == 0)
            return true;
    }
}

static void TestMsgLine(void)
{
    u16 encrypted[32], out[32];
    u32 length = 0;
    const u16 pikachu[] = {'P', 'i', 'k', 'a', 'c', 'h', 'u', 0};
    EncryptMsgLine(pikachu, 25, encrypted, &length);
    CHECK(DecryptMsgLine(encrypted, length, 25, out, 32) == 7 && Equals(out, "Pikachu"));
    DecryptMsgLine(encrypted, length, 26, out, 32);                                     // wrong key
    CHECK(!Equals(out, "Pikachu"));
    CHECK(DecryptMsgLine(encrypted, length, 25, out, 4) == 3 && Equals(out, "Pik"));   // capped
    CHECK((encrypted[0] ^ 'P') == (u16)(0x2983 * 28));

    // Line 0 key is 0x7C89 (docs/RE2-text.md).
    const u16 potion[] = {'P', 'o', 't', 'i', 'o', 'n', 0x10, 3, 0x1101, 0x00FE, 0x0100, 's', 0};
    EncryptMsgLine(potion, 0, encrypted, &length);
    CHECK((encrypted[0] ^ 'P') == 0x7C89);
    CHECK(DecryptMsgLine(encrypted, length, 0, out, 32) == 6 && Equals(out, "Potion"));

    // Plural tag mid-word: singular "y", plural "ies".
    const u16 berry[] = {'O', 'r', 'a', 'n', ' ', 'B', 'e', 'r', 'r', 0x10, 3, 0x1101, 0x00FE, 0x0301,
                         'y', 'i', 'e', 's', 0};
    EncryptMsgLine(berry, 155, encrypted, &length);
    CHECK(DecryptMsgLine(encrypted, length, 155, out, 32) == 10 && Equals(out, "Oran Berry"));

    // Other tags are dropped; a tag running past the line ends it.
    const u16 other[] = {'A', 0x10, 2, 0x0100, 0x0005, 'B', 0x10, 9, 'C', 0};
    EncryptMsgLine(other, 7, encrypted, &length);
    CHECK(DecryptMsgLine(encrypted, length, 7, out, 32) == 2 && Equals(out, "AB"));
}

static void TestUtf8(void)
{
    const u16 text[] = {'N', 'i', 'd', 'o', 'r', 'a', 'n', 0x2640, 0};
    char out[32];
    CHECK(Utf16ToUtf8(text, 9, out, sizeof(out)) == 10);
    CHECK(std::strcmp(out, "Nidoran\xE2\x99\x80") == 0);
    const u16 e[] = {'F', 0xE9, 0};
    CHECK(Utf16ToUtf8(e, 3, out, sizeof(out)) == 3 && std::strcmp(out, "F\xC3\xA9") == 0);
    CHECK(Utf16ToUtf8(text, 9, out, 8) == 7);   // truncated, still terminated
    CHECK(out[7] == 0);
}

int main()
{
    TestEncryptedRoundTrip();
    TestDecrypted();
    TestInvalid();
    TestEgg();
    TestJudgeAndMarkers();
    TestBagSlot();
    TestMsgLine();
    TestUtf8();
    if (s_failures)
    {
        std::printf("party_data_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("party_data_test: ok\n");
    return 0;
}
