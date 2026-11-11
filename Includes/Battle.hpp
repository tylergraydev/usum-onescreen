#pragma once

#include <string>
#include "BattleRules.hpp"

#include <3ds/types.h>

namespace OneScreen
{
    // One battler as the battle engine keeps it (see docs/RE1-battle.md).
    struct BattleMon
    {
        u32 pokemonParam = 0;   // record +0: static candidate, runtime guarded
        u32 address = 0;        // the game's record, 0 if absent
        u16 species = 0;
        u8  level = 0;
        u16 hp = 0, maxHp = 0;
        u16 ability = 0;
        u8 types[3] = {18, 18, 18};
        u8 stages[7] = {6, 6, 6, 6, 6, 6, 6};
        u16 moves[4] = {};
        u8  pp[4] = {}, maxPp[4] = {};
        u8  partyIndex = 0xFF;  // record +0x19: battle-team index, NOT field-party slot
    };

    // Which sub-screen the battle UI shows (top of the scene manager's stack).
    // PokemonPage: one Pokémon's page from the Pokémon list (Switch /
    // Restore / Summary). Question: the battle's two-choice question (Use
    // next Pokémon? / learning a move); `question` says which.
    enum class BattleMenu { None, Command, Moves, Pokemon, Bag, Other, PokemonPage, Question, Target };

    // One entry of the battle Bag's open pocket, as the game lists it.
    struct BattleBagEntry
    {
        u16 item = 0;
        u16 count = 0;
    };

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
        bool bagItemPage = false;       // Bag: an item's page (Use) instead of the list
        BattleMon player, opponent;     // party slot 0 of client 0 and client 1
        u8  partyCount[4] = {};
        BattleMon clients[4][6];
        BattleKind kind = BattleKind::Unknown;
        u8 battleType = 0xFF, battleFormat = 0xFF;
        // The player's battle party in battle order (slot 0 = the active
        // battler); what the battle Pokémon list shows, slot by slot.
        BattleMon party[6];
        // The battle's WordSet (busy object +0x94), for MoveName.
        u32 wordSet = 0;
        // Move list (menu == Moves): per move its type and effectiveness
        // against the target, as the list shows them.
        u8  moveType[4] = {}, moveEffect[4] = {};
        // Bag: the open pocket's items (the game's own copy, in its order;
        // up to 64), "Which Pokémon do you want to use this item on?" and
        // its cursor (a slot of the battle party), and the item an item's
        // page shows (index in the pocket).
        BattleBagEntry bagItems[64];
        u8  bagItemCount = 0;
        bool bagPick = false;
        u8  bagPickCursor = 0xFF;
        u8  bagPageIndex = 0xFF;
        // Question: its message (0x4E "Use next Pokémon?", 0x2F learning a
        // move) and cursor.
        u16 question = 0;
        u8  questionCursor = 0;
    };

    BattleState ReadBattle(u32 sceneManager);

    // Move name from the move-name file (118) the battle's WordSet loaded,
    // UTF-8; empty if it is not there or does not check out.
    std::string MoveName(const BattleState &state, u16 move);
    std::string DescribeBattle(const BattleState &state);

    // Readable memory blocks of the game process, one "start-end/perm" per
    // block, for choosing what to dump.
    std::string MemoryMap(void);

    // Writes SIZE bytes at ADDR to /usum-dump-NAME.bin on the SD card
    // (unreadable pages as zeros). Returns a log line.
    std::string DumpMemory(u32 address, u32 size, const std::string &name);
}
