#include "Usum.hpp"
#include "Party.hpp"

#include <cstring>

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Scene.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // USUM v1.2, main image. Found and checked as described in
    // docs/RE3-party-bag.md.
    static const u32 GameManagerGlobal = Usum::GameManagerGlobal;  // GameSys::GameManager*
    static const u32 ManagerGameData   = 0x24;      // -> GameSys::GameData
    static const u32 GameDataSaveData  = 0x04;      // -> Savedata::SaveData
    static const u32 GameDataParty     = 0x0C;      // -> pml::PokeParty (player)
    static const u32 VtSaveData        = Usum::VtSaveData;
    static const u32 VtMyItem          = Usum::VtMyItem;
    static const u32 VtPokemonParam    = Usum::VtPokemonParam;

    // Party screen (scene vtable 0x647FCC): App::PokeList::StaticWork at
    // scene +0xA8 (vtable 0x647F9C; also the battle Pokémon list's param,
    // docs/RE1-battle.md). +8 -> {pml::PokeParty*, ...}; display order u8[6]
    // at +0x53.
    static const u32 VtPartyScene = Usum::VtPartyScene, VtStaticWork = Usum::VtStaticWork;
    static const u32 SceneStaticWork = 0xA8, StaticWorkPartyRef = 0x08, StaticWorkOrder = 0x53;
    static const u32 StaticWorkCursor = 0x52;   // u8 screen position (docs/OFFSETS.md)

    // Bag scene (vtable 0x6488C8, field Bag and the Party screen's Restore):
    // +0x88 -> its item list. Found by diffing heap dumps at three cursor
    // positions (docs/OFFSETS.md).
    static const u32 VtBagScene = Usum::VtBagScene, BagSceneList = 0x88;
    static const u32 ListCount = 0x0C, ListRows = 0x14, ListRow = 0x18, ListScroll = 0x1C;

    // Move teaching (WazaOshie CRO object on the scene stack, as in battle,
    // docs/MOVE-LEARN.md): +0xB0 -> param {u8 +1, u16 new move +8}, +0xB5
    // u8 1 while one move's page is up.
    static const u32 VtWaza = Usum::WazaView, WazaParam = 0xB0, WazaPage = 0xB5;
    static const u32 WazaPokeId = 0x01, WazaMove = 0x08, ManagerSlots = 8;
    static const u16 MoveCount = 729;

    // Summary (Status scene, a CRO): +0x58 -> WordSet loader, +0x10 -> entries
    // {buffer, size, u32 dat, MsgData*} (as the battle's, docs/OFFSETS.md).
    static const u32 StatusLoader = 0x58, LoaderEntries = 0x10, LoaderStride = 0x10;
    static const u32 EntryDat = 0x08, EntryMsg = 0x0C;

    // pml::PokeParty: 6 PokemonParam pointers, u8 count at +0x18.
    static const u32 PartyCount = 0x18;
    // PokemonParam: +4 core data (0xE8), +8 party stats (0x1C).
    static const u32 ParamCore = 0x04, ParamCalc = 0x08;

    // Savedata::MyItem at SaveData +8: pocket pointer table at +0xE2C.
    static const u32 SaveDataMyItem = 0x08, MyItemPockets = 0xE2C;
    static const u16 PocketCapacity[(int)Pocket::Count] = {60, 427, 108, 67, 198, 35, 11};

    // Resident message files (docs/RE2-text.md).
    static const u32 SpeciesMsgGlobal = Usum::SpeciesMsgGlobal;   // MsgData* (file 60)
    static const u32 MsgCacheTable = Usum::MsgCacheTable;      // 8 x {.., +8 u16 dat, +0xC MsgData*}
    static const u32 VtMsgData = Usum::VtMsgData;
    static const u32 SpeciesDat = 60, ItemDat = 42;

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

    static bool Readable(u32 address, u32 size)
    {
        return address != 0 && Process::CheckAddress(address, MEMPERM_READ)
               && Process::CheckAddress(address + size - 1, MEMPERM_READ);
    }

    static bool ReadBytes(u32 address, u8 *out, u32 size)
    {
        if (!Readable(address, size))
            return false;
        for (u32 i = 0; i < size; i += 4)
        {
            const u32 v = Read32(address + i);
            for (u32 k = 0; k < 4 && i + k < size; k++)
                out[i + k] = (u8)(v >> (k * 8));
        }
        return true;
    }

    static u32 GameData(void)
    {
        const u32 manager = Read32(GameManagerGlobal);
        const u32 data = Readable(manager, ManagerGameData + 4) ? Read32(manager + ManagerGameData) : 0;
        return Readable(data, 0x14) ? data : 0;
    }

    // Fills state.order from the Party screen's work object if that screen
    // is up and the object refers to this party.
    static void ReadPartyScreen(u32 sceneManager, PartyState &state)
    {
        const u32 manager = Read32(sceneManager);
        const u32 scene = manager ? Read32(manager) : 0;
        if (scene == 0 || Read32(scene) != VtPartyScene)
            return;
        const u32 work = Read32(scene + SceneStaticWork);
        if (!Readable(work, 0x60) || Read32(work) != VtStaticWork
            || Read32(Read32(work + StaticWorkPartyRef)) != state.party)
            return;
        u8 order[6];
        u32 seen = 0;
        for (u32 i = 0; i < state.count; i++)
        {
            order[i] = Read8(work + StaticWorkOrder + i);
            if (order[i] >= state.count || (seen & (1u << order[i])))
                return;
            seen |= 1u << order[i];
        }
        for (u32 i = 0; i < state.count; i++)
            state.order[i] = order[i];
        state.partyScreen = true;
        const u8 cursor = Read8(work + StaticWorkCursor);
        state.screenCursor = cursor < state.count ? (s8)cursor : -1;
    }

    Pk7 ReadPokemonParam(u32 param)
    {
        if (!Readable(param, 0x10) || Read32(param) != VtPokemonParam)
            return Pk7();
        u8 core[Pk7CoreSize], calc[Pk7CalcSize];
        if (!ReadBytes(Read32(param + ParamCore), core, Pk7CoreSize))
            return Pk7();
        const bool haveCalc = ReadBytes(Read32(param + ParamCalc), calc, Pk7CalcSize);
        return DecodePk7(core, haveCalc ? calc : nullptr);
    }

    PartyState ReadParty(u32 sceneManager)
    {
        PartyState state;
        const u32 data = GameData();
        if (data == 0 || Read32(Read32(data + GameDataSaveData)) != VtSaveData)
            return state;
        const u32 party = Read32(data + GameDataParty);
        if (!Readable(party, 0x1C))
            return state;
        state.party = party;
        state.count = Read8(party + PartyCount);
        if (state.count > 6)
            return state;

        for (u32 i = 0; i < state.count; i++)
        {
            PartySlot &slot = state.slots[i];
            const u32 param = Read32(party + i * 4);
            if (!Readable(param, 0x10) || Read32(param) != VtPokemonParam)
                continue;
            slot.address = param;
            slot.mon = ReadPokemonParam(param);
        }
        state.valid = true;
        ReadPartyScreen(sceneManager, state);
        return state;
    }

    BagState ReadBag(void)
    {
        BagState state;
        const u32 data = GameData();
        const u32 save = data ? Read32(data + GameDataSaveData) : 0;
        if (!Readable(save, SaveDataMyItem + MyItemPockets + 0x20) || Read32(save) != VtSaveData)
            return state;
        const u32 myItem = save + SaveDataMyItem;
        if (Read32(myItem) != VtMyItem)
            return state;
        state.myItem = myItem;

        // Each pocket must lie inside MyItem's item area (+4 .. +0xE2C).
        for (u32 p = 0; p < (u32)Pocket::Count; p++)
        {
            const u32 first = Read32(myItem + MyItemPockets + p * 4);
            if (first < myItem + 4 || first + PocketCapacity[p] * 4 > myItem + MyItemPockets)
                return state;
            state.pocket[p] = first;
            state.capacity[p] = PocketCapacity[p];
        }
        state.valid = true;
        return state;
    }

    BagSlot ReadBagSlot(const BagState &bag, Pocket pocket, u32 index)
    {
        const u32 p = (u32)pocket;
        if (!bag.valid || p >= (u32)Pocket::Count || index >= bag.capacity[p])
            return BagSlot();
        return DecodeBagSlot(Read32(bag.pocket[p] + index * 4));
    }

    const char *PocketName(Pocket pocket)
    {
        static const char *const names[] = {"Medicine", "Items", "TMs", "Berries", "Key Items", "Z-Crystals",
                                            "Rotom Powers"};
        return (u32)pocket < (u32)Pocket::Count ? names[(u32)pocket] : "?";
    }

    // One line of a full-load MsgData, checked as docs/RE2-text.md proposes.
    // `lines` (if not 0) is the line count the file must have.
    std::string MsgLine(u32 msg, u32 dat, u32 line, u32 lines)
    {
        if (!Readable(msg, 0x30) || Read32(msg) != VtMsgData || Read8(msg + 0x08) != 1 || Read32(msg + 0x0C) != dat)
            return "";
        const u32 header = Read32(msg + 0x14), section = Read32(msg + 0x18);
        if (!Readable(header, 4) || !Readable(section, 4) || line >= Read16(header + 2)
            || (lines != 0 && Read16(header + 2) != lines))
            return "";
        const u32 size = Read32(section);
        const u32 entry = section + 4 + line * 8;
        if (4 + line * 8 + 8 > size)
            return "";
        const u32 offset = Read32(entry);
        const u32 length = Read16(entry + 4);
        if (length == 0 || length > 96 || offset + length * 2 > size || !Readable(section + offset, length * 2))
            return "";

        u16 encrypted[96], text[96];
        for (u32 i = 0; i < length; i++)
            encrypted[i] = Read16(section + offset + i * 2);
        DecryptMsgLine(encrypted, length, line, text, 96);
        char utf8[3 * 96 + 1];
        Utf16ToUtf8(text, 96, utf8, sizeof(utf8));
        return utf8;
    }

    std::string SpeciesName(u16 species)
    {
        // Form 0 of a species is on line `species` (0x31D290).
        return MsgLine(Read32(SpeciesMsgGlobal), SpeciesDat, species);
    }

    std::string ItemName(u16 item)
    {
        for (u32 i = 0; i < 8; i++)
        {
            const u32 slot = MsgCacheTable + i * 0x10;
            if (Read16(slot + 0x08) == ItemDat)
                return MsgLine(Read32(slot + 0x0C), ItemDat, item);
        }
        return "";
    }

    BagListState ReadBagList(u32 sceneManager)
    {
        BagListState state;
        const u32 manager = Read32(sceneManager);
        const u32 scene = manager ? Read32(manager) : 0;
        if (!Readable(scene, BagSceneList + 4) || Read32(scene) != VtBagScene)
            return state;
        const u32 list = Read32(scene + BagSceneList);
        if (!Readable(list, ListScroll + 4))
            return state;
        state.count = Read32(list + ListCount);
        state.rows = Read32(list + ListRows);
        state.row = Read32(list + ListRow);
        const u32 bits = Read32(list + ListScroll);
        std::memcpy(&state.scroll, &bits, 4);
        state.valid = state.count <= 427 && state.rows >= 1 && state.rows <= 10 && state.row < state.rows
                      && state.scroll >= 0 && state.scroll <= 427;
        return state;
    }

    TeachScreenState ReadTeachScreen(u32 sceneManager)
    {
        TeachScreenState state;
        const u32 manager = Read32(sceneManager);
        if (!Readable(manager, ManagerSlots * 4))
            return state;
        for (u32 i = 0; i < ManagerSlots; i++)
        {
            const u32 object = Read32(manager + i * 4);
            if (!Readable(object, WazaPage + 1))
                break;
            const u32 vtable = Read32(object);
            const u32 module = CroModuleOf(vtable);
            if (module == 0 || vtable != module + VtWaza || CroModuleName(module) != "WazaOshie")
                continue;
            const u32 param = Read32(object + WazaParam);
            if (!Readable(param, WazaMove + 2))
                return state;
            state.newMove = Read16(param + WazaMove);
            state.pokeId = Read8(param + WazaPokeId);
            state.confirm = Read8(object + WazaPage) == 1;
            state.valid = state.newMove != 0 && state.newMove < MoveCount;
            return state;
        }
        return state;
    }

    u32 SummaryWordLoader(u32 sceneManager)
    {
        const u32 manager = Read32(sceneManager);
        const u32 scene = manager ? Read32(manager) : 0;
        if (!Readable(scene, StatusLoader + 4) || SceneModule(sceneManager) != "Status")
            return 0;
        const u32 loader = Read32(scene + StatusLoader);
        return Readable(loader, LoaderEntries + 4) && Readable(Read32(loader + LoaderEntries), 4) ? loader : 0;
    }

    std::string LoaderText(u32 loader, u32 dat, u32 line)
    {
        // Line counts of the English files (docs/RE2-text.md); a file with
        // another count is not used.
        const u32 lines = dat == SummaryMoveDat ? 729 : dat == SummaryAbilityDat ? 234 : dat == SummaryNatureDat ? 26 : 0;
        if (loader == 0 || lines == 0 || line >= lines)
            return "";
        const u32 entries = Read32(loader + LoaderEntries);
        for (u32 i = 0; i < 4; i++)
        {
            const u32 entry = entries + i * LoaderStride;
            if (!Readable(entry, LoaderStride) || Read32(entry + EntryDat) != dat)
                continue;
            // Loaded from the loader's buffer, so the MsgData's own dat field is 0.
            return MsgLine(Read32(entry + EntryMsg), 0, line, lines);
        }
        return "";
    }

    static std::string Named(const std::string &name, u32 id)
    {
        return name.empty() ? Utils::Format("#%lu", id) : Utils::Format("%s(%lu)", name.c_str(), id);
    }

    std::string DescribeParty(const PartyState &state)
    {
        if (!state.valid)
            return "party: not found (no save loaded?)";
        static const char *const forms[] = {"invalid", "enc", "dec"};
        std::string out = Utils::Format("party: %u (%08lX)", state.count, state.party);
        for (u32 i = 0; i < state.count; i++)
        {
            const PartySlot &slot = state.slots[i];
            const Pk7 &mon = slot.mon;
            out += Utils::Format("\n  %lu %08lX", i + 1, slot.address);
            if (mon.form == Pk7Form::Invalid)
            {
                out += " unreadable";
                continue;
            }
            char nick[3 * 13 + 1];
            Utf16ToUtf8(mon.nickname, 13, nick, sizeof(nick));
            out += " " + Named(SpeciesName(mon.species), mon.species) + " \"" + nick + "\"";
            if (mon.isEgg)
                out += " egg";
            if (mon.calcValid)
                out += Utils::Format(" L%u hp %u/%u status %lX", mon.level, mon.hp, mon.maxHp, mon.status);
            else
                out += " stats?";
            out += " item " + (mon.heldItem ? Named(ItemName(mon.heldItem), mon.heldItem) : std::string("-"));
            out += " moves";
            for (u32 m = 0; m < 4; m++)
                out += Utils::Format(" %u:%u", mon.moves[m], mon.pp[m]);
            out += Utils::Format(" [%s]", forms[(int)mon.form]);
        }
        if (state.partyScreen)
        {
            out += "\n  party screen order (slot per position):";
            for (u32 i = 0; i < state.count; i++)
                out += Utils::Format(" %u", state.order[i] + 1);
            out += Utils::Format(", cursor %d", state.screenCursor + 1);
        }
        return out;
    }

    std::string DescribeBag(const BagState &state)
    {
        if (!state.valid)
            return "bag: not found (no save loaded?)";
        std::string out = Utils::Format("bag: MyItem %08lX", state.myItem);
        for (u32 p = 0; p < (u32)Pocket::Count; p++)
        {
            // Slots left at count 0 (an item used up) keep their ID; the Bag
            // does not show them.
            std::string items, hidden;
            u32 used = 0;
            for (u32 i = 0; i < state.capacity[p]; i++)
            {
                const BagSlot slot = ReadBagSlot(state, (Pocket)p, i);
                if (slot.item == 0)
                    continue;
                if (slot.count == 0)
                {
                    hidden += " " + Named(ItemName(slot.item), slot.item);
                    continue;
                }
                used++;
                items += (used > 1 ? ", " : " ") + Named(ItemName(slot.item), slot.item)
                         + Utils::Format(" x%u", slot.count);
                if (slot.isNew)
                    items += " new";
                if (slot.freeSpace)
                    items += Utils::Format(" free%u", slot.freeSpace);
            }
            out += Utils::Format("\n  %s %lu/%u:", PocketName((Pocket)p), used, state.capacity[p]) + items;
            if (!hidden.empty())
                out += " | count 0:" + hidden;
        }
        return out;
    }
}
