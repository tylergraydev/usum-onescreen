#pragma once

#include <string>

#include <3ds/types.h>

#include "BoxData.hpp"

namespace OneScreen
{
    // The PC boxes in the save data (docs/RE6-boxes.md). Valid in every scene
    // after the save is loaded; the Box screen writes a move here as soon as
    // the Pokémon is dropped.
    struct BoxStorage
    {
        bool valid = false;
        u32 layout = 0;         // Savedata::BOX
        u32 pokemon = 0;        // Savedata::BoxPokemon
        u32 unlocked = 0;       // boxes the player can use
    };

    // The open Box screen (scene Box), read through App::Box::BoxLowBgDraw.
    struct BoxView
    {
        bool valid = false;
        u32 draw = 0;           // App::Box::BoxLowBgDraw
        BoxMode mode = BoxMode::Unknown;
        u32 cursor = 0;         // BoxCursor* values (BoxData.hpp)
        u32 tray = 0;           // the box on screen, 0-based
        // The screen's own copy of the party: it compacts it while you move
        // Pokémon and the real party (Party.hpp) only changes when the PC
        // closes. Empty slots are at the end.
        bool partyValid = false;
        u32 party[BoxPartySlots] = {};  // pml::pokepara::PokemonParam
    };

    BoxStorage ReadBoxStorage(void);
    BoxView ReadBoxView(u32 sceneManager);

    std::string BoxName(const BoxStorage &storage, u32 tray);
    Pk7 ReadBoxMon(const BoxStorage &storage, u32 tray, u32 slot);
    // A PokemonParam's Pokémon (core and party stats).
    Pk7 ReadParam(u32 param);

    // ActionWorld flags for the Box screen's mode (WorldBoxIdle etc.), 0
    // outside it. Cheap enough for every frame.
    u32 BoxWorldFlags(u32 sceneManager);

    // Log lines for the `box [N]` command: box N (1-based; default the one on
    // screen, else 1) and, with the Box screen open, its state and party.
    std::string DescribeBox(u32 sceneManager, int box);
}
