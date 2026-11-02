#include <string>

#include "Game.hpp"

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    // USA v1.2 only. Hooks are tied to exact addresses, so any other region or
    // update needs its own entry and offset table.
    //
    // Ultra Sun: title ID and version confirmed in Azahar on 2026-11-02. The
    // installed v1.2 update's TMD says title version 0x0800 (2048), and
    // Process::GetVersion() reports the same value.
    // Ultra Moon has no local dump yet; its title ID and version are assumed to
    // match Ultra Sun's pattern and are unconfirmed, and its offsets are unknown.
    static const GameInfo s_games[] =
    {
        { "Pokemon Ultra Sun", 0x00040000001B5000ULL, 0x0800, true,
          {
              { 0x00279BB4, { 0xE92D5FF0, 0xE1A06000, 0xE28D0028, 0xE3A08000 } },
              { 0x002826F0, { 0xE92D4FFF, 0xE24DD004, 0xE3A0C000, 0xE3520000 } },
              { 0x00282548, { 0xE92D4FFF, 0xE24DD014, 0xE3A0C000, 0xE3520000 } },
              0x006A599C,
          } },
        { "Pokemon Ultra Moon", 0x00040000001B5100ULL, 0x0800, false,
          { { 0, {} }, { 0, {} }, { 0, {} }, 0 } },
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
        return ok;
    }
}
