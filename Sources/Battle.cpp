#include "Battle.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <vector>

#include "Party.hpp"
#include "Scene.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // Battle CRO, USUM v1.2. Vtables are offsets from the module header so
    // they hold wherever the module loads. Found and checked as described in
    // docs/RE1-battle.md.
    static const u32 VtView        = 0x10A528;  // view object at scene +0x94
    static const u32 VtMenuCommand = 0x10B598;  // Fight / Pokémon / Bag / Run
    static const u32 VtMenuMoves   = 0x10B724;  // move list
    static const u32 VtBusy        = 0x10B43C;  // under the menus: animations, messages

    static const u32 SceneView     = 0x94;      // scene -> view object
    static const u32 ViewPokeCon   = 0xD0;      // view -> poke container
    static const u32 PokeConParty  = 0x04;      // party[client]: 6 record pointers + u32 count
    static const u32 PartyStride   = 0x1C;
    static const u32 ManagerSlots  = 8;         // scene manager: stack of scene objects

    // Battler record.
    static const u32 MonSpecies = 0x0C, MonMaxHp = 0x0E, MonHp = 0x10, MonAbility = 0x16, MonLevel = 0x18;
    static const u32 MonMoves = 0x1FC, MoveStride = 0x0E;   // u16 move, u8 pp, u8 max pp, ...

    // Command menu: highlighted button.
    static const u32 CommandCursor = 0x278;

    // Move list object.
    static const u32 ListCount = 0xA8, ListFirst = 0xB0, ListStride = 0x18, ListCursor = 0x114;

    // Pokémon list: a main-image class; its parameter object holds the cursor.
    static const u32 VtPokeList = 0x648394, VtPokeListParam = 0x647F9C;
    static const u32 PokeListParam = 0xA8, ParamCount = 0x28, ParamCursor = 0x52;

    // Bag: Bag CRO frame -> child frame (linked at frame +0x40, node at child
    // +4) -> main-image Bag view (vtable 0x6488C8) -> item list.
    static const u32 VtBagFrame = 0x16E24, VtBagChild = 0x16EB0;
    static const u32 BagChildLink = 0x40, BagPocket = 0xD8, BagView = 0xCC, BagViewList = 0x88;
    static const u32 VtBagView = 0x6488C8;
    static const u32 ItemListCount = 0x10, ItemListCursor = 0x18;
    // Bag frame: u8 1 while the item list shows, 0 on an item's page (Use).
    static const u32 BagFrameListShown = 0x6E;

    // The busy object (always on the stack in battle) holds the battle's
    // WordSet; its loader (+0x14) keeps the message files it loaded:
    // u32 count +0xC, entries +0x10 of {buffer, size, u32 dat, MsgData*}.
    static const u32 BusyWordSet = 0x94, WordSetLoader = 0x14;
    static const u32 LoaderCount = 0x0C, LoaderEntries = 0x10, LoaderStride = 0x10;
    static const u32 EntryDat = 0x08, EntryMsg = 0x0C;
    static const u32 MoveNameDat = 118, MoveNameLines = 729;

    static u8 Read8(u32 address)
    {
        u8 v = 0;
        Process::Read8(address, v);
        return v;
    }

    static u16 Read16(u32 address)
    {
        u16 v = 0;
        Process::Read16(address, v);
        return v;
    }

    static u32 Read32(u32 address)
    {
        u32 v = 0;
        Process::Read32(address, v);
        return v;
    }

    static bool ReadMon(u32 address, BattleMon &mon)
    {
        if (address == 0 || !Process::CheckAddress(address, MEMPERM_READ)
            || !Process::CheckAddress(address + 0x300, MEMPERM_READ))
            return false;
        mon.address = address;
        mon.species = Read16(address + MonSpecies);
        mon.maxHp = Read16(address + MonMaxHp);
        mon.hp = Read16(address + MonHp);
        mon.ability = Read16(address + MonAbility);
        mon.level = Read8(address + MonLevel);
        for (u32 i = 0; i < 4; i++)
        {
            const u32 move = address + MonMoves + i * MoveStride;
            mon.moves[i] = Read16(move);
            mon.pp[i] = Read8(move + 2);
            mon.maxPp[i] = Read8(move + 3);
        }
        return true;
    }

    BattleState ReadBattle(u32 sceneManager)
    {
        BattleState state;
        const u32 manager = Read32(sceneManager);
        const u32 scene = manager ? Read32(manager) : 0;
        const u32 module = scene ? CroModuleOf(Read32(scene)) : 0;
        if (module == 0 || CroModuleName(module) != "Battle")
            return state;
        state.module = module;

        const u32 view = Read32(scene + SceneView);
        if (view == 0 || Read32(view) != module + VtView)
            return state;
        const u32 pokecon = Read32(view + ViewPokeCon);
        if (pokecon == 0)
            return state;

        for (u32 client = 0; client < 2; client++)
        {
            const u32 party = pokecon + PokeConParty + client * PartyStride;
            state.partyCount[client] = Read8(party + 0x18);
            ReadMon(Read32(party), client == 0 ? state.player : state.opponent);
        }
        for (u32 slot = 0; slot < 6 && slot < state.partyCount[0]; slot++)
            ReadMon(Read32(pokecon + PokeConParty + slot * 4), state.party[slot]);
        state.valid = state.player.address != 0 && state.opponent.address != 0;

        for (u32 i = 0; i < ManagerSlots; i++)
        {
            const u32 object = Read32(manager + i * 4);
            if (object == 0)
                break;
            state.menuObject = object;
            if (Read32(object) == module + VtBusy)
                state.wordSet = Read32(object + BusyWordSet);
        }
        const u32 top = state.menuObject;
        const u32 vtable = top ? Read32(top) : 0;
        const u32 vtModule = CroModuleOf(vtable);
        const std::string vtModuleName = vtModule == module ? "Battle" : CroModuleName(vtModule);
        state.menuVtable = vtModule ? vtable - vtModule : vtable;

        if (top == scene || (vtModuleName == "Battle" && state.menuVtable == VtBusy))
            state.menu = BattleMenu::None;
        else if (vtModuleName == "Battle" && state.menuVtable == VtMenuCommand)
        {
            state.menu = BattleMenu::Command;
            state.cursor = Read8(top + CommandCursor);
            state.listCount = 4;
        }
        else if (vtModuleName == "Battle" && state.menuVtable == VtMenuMoves)
        {
            state.menu = BattleMenu::Moves;
            state.listCount = Read8(top + ListCount);
            state.cursor = Read8(top + ListCursor);
            for (u32 i = 0; i < 4 && i < state.listCount; i++)
            {
                const u32 entry = top + ListFirst + i * ListStride;
                state.moveEffect[i] = Read8(entry + 2);
                state.moveType[i] = Read8(entry + 0x15);
            }
        }
        else if (vtModule == 0 && vtable == VtPokeList)
        {
            state.menu = BattleMenu::Pokemon;
            const u32 param = Read32(top + PokeListParam);
            if (param != 0 && Read32(param) == VtPokeListParam)
            {
                state.listCount = Read8(param + ParamCount);
                state.cursor = Read8(param + ParamCursor);
            }
        }
        else if (vtModuleName == "Bag" && state.menuVtable == VtBagFrame)
        {
            state.menu = BattleMenu::Bag;
            state.bagItemPage = Read8(top + BagFrameListShown) == 0;
            const u32 link = Read32(top + BagChildLink);
            const u32 child = link ? link - 4 : 0;
            if (child != 0 && Read32(child) == vtModule + VtBagChild)
            {
                state.bagPocket = Read8(child + BagPocket);
                const u32 view = Read32(child + BagView);
                const u32 list = view != 0 && Read32(view) == VtBagView ? Read32(view + BagViewList) : 0;
                if (list != 0)
                {
                    state.listCount = Read8(list + ItemListCount);
                    state.cursor = Read8(list + ItemListCursor);
                }
            }
        }
        else
            state.menu = BattleMenu::Other;
        return state;
    }

    std::string MoveName(const BattleState &state, u16 move)
    {
        if (state.wordSet == 0 || move == 0 || move >= MoveNameLines
            || !Process::CheckAddress(state.wordSet, MEMPERM_READ))
            return "";
        const u32 loader = Read32(state.wordSet + WordSetLoader);
        if (loader == 0 || !Process::CheckAddress(loader, MEMPERM_READ))
            return "";
        const u32 count = Read32(loader + LoaderCount), entries = Read32(loader + LoaderEntries);
        if (count > 16 || entries == 0 || !Process::CheckAddress(entries, MEMPERM_READ))
            return "";
        for (u32 i = 0; i < count; i++)
        {
            const u32 entry = entries + i * LoaderStride;
            if (Read32(entry + EntryDat) != MoveNameDat)
                continue;
            // Loaded from the loader's buffer, so the MsgData's own dat field is 0.
            const u32 msg = Read32(entry + EntryMsg);
            const u32 header = Read32(msg + 0x14);
            if (header == 0 || Read16(header + 2) != MoveNameLines)
                return "";
            return MsgLine(msg, 0, move);
        }
        return "";
    }

    static std::string DescribeMon(const char *who, const BattleMon &mon, u8 count)
    {
        std::string out = Utils::Format("%s %08lX sp %u L%u hp %u/%u ab %u party %u moves", who, mon.address,
                                        mon.species, mon.level, mon.hp, mon.maxHp, mon.ability, count);
        for (u32 i = 0; i < 4; i++)
            out += Utils::Format(" %u:%u/%u", mon.moves[i], mon.pp[i], mon.maxPp[i]);
        return out;
    }

    std::string DescribeBattle(const BattleState &state)
    {
        if (state.module == 0)
            return "battle: not in a battle";
        if (!state.valid)
            return Utils::Format("battle: Battle module at %08lX, objects not found", state.module);

        static const char *const menus[] = {"none", "command", "moves", "pokemon", "bag", "other"};
        std::string out = Utils::Format("battle: menu %s (%08lX vt %08lX)", menus[(int)state.menu],
                                        state.menuObject, state.menuVtable);
        if (state.menu != BattleMenu::None && state.menu != BattleMenu::Other)
            out += Utils::Format(" cursor %u of %u", state.cursor, state.listCount);
        if (state.menu == BattleMenu::Bag)
            out += Utils::Format(" pocket %u%s", state.bagPocket, state.bagItemPage ? " item page" : "");
        if (state.menu == BattleMenu::Moves)
        {
            out += " [type/eff";
            for (u32 i = 0; i < state.listCount && i < 4; i++)
                out += Utils::Format(" %u/%u", state.moveType[i], state.moveEffect[i]);
            out += "]";
        }
        out += "\n  " + DescribeMon("player", state.player, state.partyCount[0]);
        out += "\n  " + DescribeMon("opponent", state.opponent, state.partyCount[1]);
        std::string names;
        for (u32 i = 0; i < 4; i++)
            if (state.player.moves[i] != 0)
                names += (names.empty() ? "" : ", ") + MoveName(state, state.player.moves[i]);
        out += "\n  player move names: " + (names.empty() ? std::string("(none)") : names);
        return out;
    }

    std::string MemoryMap(void)
    {
        std::string out = "memmap:";
        u32 address = 0x00100000;
        while (address < 0x40000000)
        {
            MemInfo info = {};
            PageInfo page = {};
            if (R_FAILED(svcQueryMemory(&info, &page, address)) || info.size == 0)
                break;
            if (info.state != MEMSTATE_FREE && (info.perm & MEMPERM_READ) != 0)
                out += Utils::Format(" %08lX-%08lX/%lX", info.base_addr, info.base_addr + info.size, info.perm);
            address = info.base_addr + info.size;
        }
        return out;
    }

    std::string DumpMemory(u32 address, u32 size, const std::string &name)
    {
        File file;
        const std::string path = "/usum-dump-" + name + ".bin";
        if (File::Open(file, path, File::WRITE | File::CREATE | File::TRUNCATE) != File::SUCCESS)
            return "dump: can't open " + path;

        std::vector<u8> zeros(0x1000, 0);
        u32 written = 0;
        for (u32 at = address; at < address + size; at += 0x1000)
        {
            const u32 chunk = address + size - at < 0x1000 ? address + size - at : 0x1000;
            if (Process::CheckAddress(at, MEMPERM_READ))
            {
                file.Write((const void *)at, chunk);
                written += chunk;
            }
            else
                file.Write(zeros.data(), chunk);
        }
        file.Close();
        return Utils::Format("dump %s: %08lX+%lX (%lX readable)", name.c_str(), address, size, written);
    }
}
