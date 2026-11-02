#pragma once

#include <string>

#include <3ds/types.h>

namespace OneScreen
{
    // One battler as the battle engine keeps it (see docs/RE1-battle.md).
    struct BattleMon
    {
        u32 address = 0;        // the game's record, 0 if absent
        u16 species = 0;
        u8  level = 0;
        u16 hp = 0, maxHp = 0;
        u16 ability = 0;
        u16 moves[4] = {};
        u8  pp[4] = {}, maxPp[4] = {};
    };

    // Which sub-screen the battle UI shows (top of the scene manager's stack).
    enum class BattleMenu { None, Command, Moves, Pokemon, Bag, Other };

    struct BattleState
    {
        bool valid = false;     // in a Battle scene and the objects checked out
        u32 module = 0;         // Battle CRO header
        BattleMenu menu = BattleMenu::None;
        u32 menuObject = 0;     // top of the UI stack
        u32 menuVtable = 0;     // its vtable, module-relative when in a CRO
        // Highlighted entry: command 0 Fight / 1 Pokemon / 2 Bag / 3 Run;
        // move index; Pokemon list slot; Bag item in the pocket.
        u8  cursor = 0;
        u8  listCount = 0;      // entries in the move list / Pokemon list / Bag pocket
        u8  bagPocket = 0;      // Bag tab, 0 = leftmost
        BattleMon player, opponent;     // party slot 0 of client 0 and client 1
        u8  partyCount[2] = {};
        // Move list (menu == Moves): per move its type and effectiveness
        // against the target, as the list shows them.
        u8  moveType[4] = {}, moveEffect[4] = {};
    };

    BattleState ReadBattle(u32 sceneManager);
    std::string DescribeBattle(const BattleState &state);

    // Readable memory blocks of the game process, one "start-end/perm" per
    // block, for choosing what to dump.
    std::string MemoryMap(void);

    // Writes SIZE bytes at ADDR to /usum-dump-NAME.bin on the SD card
    // (unreadable pages as zeros). Returns a log line.
    std::string DumpMemory(u32 address, u32 size, const std::string &name);
}
