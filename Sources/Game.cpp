#include <string>

#include "Game.hpp"

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    // USA v1.2 only. Ultra Sun and Ultra Moon v1.2 share every address the
    // plugin uses (checked with tools/sigscan.py and a code.bin/CRO diff,
    // docs/PORTING.md). Genuine v1.2 updates report 0x0820; 0x0800 is accepted
    // for decrypted copies whose TMD says 0x0800 (same code.bin).
    static const Offsets s_usumV12 =
    {
        { 0x00279BB4, { 0xE92D5FF0, 0xE1A06000, 0xE28D0028, 0xE3A08000 } },
        { 0x002826F0, { 0xE92D4FFF, 0xE24DD004, 0xE3A0C000, 0xE3520000 } },
        { 0x00282548, { 0xE92D4FFF, 0xE24DD014, 0xE3A0C000, 0xE3520000 } },
        0x006A599C,
        { 0x0027A5BC, { 0xE92D43F0, 0xE24DD024, 0xE28D4040, 0xE1811802 } },
        { 0x00279E28, { 0xE92D43F0, 0xE24DD024, 0xE28D4040, 0xE89401E0 } },
    };

    static const GameInfo s_games[] =
    {
        { "Pokemon Ultra Sun", 0x00040000001B5000ULL, 0x0820, true, s_usumV12 },
        { "Pokemon Ultra Sun", 0x00040000001B5000ULL, 0x0800, true, s_usumV12 },
        { "Pokemon Ultra Moon", 0x00040000001B5100ULL, 0x0820, true, s_usumV12 },
    };

    const GameInfo *DetectGame(void)
    {
        const u64 titleId = CTRPluginFramework::Process::GetTitleID();
        const u16 version = CTRPluginFramework::Process::GetVersion();

        for (const GameInfo &game : s_games)
        {
            if (game.titleId == titleId && game.version == version)
                return &game;
        }

        return nullptr;
    }

    static bool VerifyTarget(const char *name, const HookTarget &target, std::string &report)
    {
        using CTRPluginFramework::Process;
        using CTRPluginFramework::Utils;

        if (target.address == 0)
        {
            report += Utils::Format("%s: not found yet\n", name);
            return true;
        }

        // Another hook (e.g. CTRPF's own OSD hook) may already have replaced the
        // first instruction with a branch; the rest of the prologue must match.
        u32 first = 0;
        if (!Process::Read32(target.address, first))
        {
            report += Utils::Format("%s: unreadable at %08X\n", name, target.address);
            return false;
        }
        const bool branched = (first & 0xFF000000) == 0xEA000000 && first != target.prologue[0];
        if (!branched && first != target.prologue[0])
        {
            report += Utils::Format("%s: MISMATCH at %08X (got %08X, want %08X)\n",
                                    name, target.address, first, target.prologue[0]);
            return false;
        }

        for (u32 i = 1; i < 4; i++)
        {
            u32 value = 0;
            const u32 address = target.address + i * 4;
            if (!Process::Read32(address, value) || value != target.prologue[i])
            {
                report += Utils::Format("%s: MISMATCH at %08X (got %08X, want %08X)\n",
                                        name, address, value, target.prologue[i]);
                return false;
            }
        }

        if (branched)
        {
            s32 offset = (s32)(first << 8) >> 6;   // sign-extended imm24 * 4
            const u32 dest = target.address + 8 + offset;
            u32 destWord = 0;
            Process::Read32(dest, destWord);
            report += Utils::Format("%s: ok at %08X, already hooked (b %08X, first word there %08X)\n",
                                    name, target.address, dest, destWord);
        }
        else
            report += Utils::Format("%s: ok at %08X\n", name, target.address);
        return true;
    }

    bool VerifyHooks(const GameInfo &game, std::string &report)
    {
        bool ok = VerifyTarget("gspSetFramebuffer", game.offsets.gspSetFramebuffer, report);
        ok = VerifyTarget("hidTouchRead", game.offsets.hidTouchRead, report) && ok;
        ok = VerifyTarget("hidPadRead", game.offsets.hidPadRead, report) && ok;
        ok = VerifyTarget("gxDisplayTransfer", game.offsets.gxDisplayTransfer, report) && ok;
        ok = VerifyTarget("gxTextureCopy", game.offsets.gxTextureCopy, report) && ok;
        return ok;
    }
}
