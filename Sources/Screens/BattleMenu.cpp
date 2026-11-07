#include "Screens/BattleMenu.hpp"

#include <cstdio>

#include "ActionQueue.hpp"
#include "Screens/PartyMenu.hpp"

namespace OneScreen
{
namespace Screens
{
    const char *TypeName(u8 type)
    {
        static const char *const names[] = {
            "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel",
            "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark", "Fairy",
        };
        return type < 18 ? names[type] : "?";
    }

    UI::Color TypeColor(u8 type)
    {
        using UI::Rgb;
        static const UI::Color colors[] = {
            Rgb(168, 168, 120), Rgb(192, 48, 40),   Rgb(168, 144, 240), Rgb(160, 64, 160),
            Rgb(224, 192, 104), Rgb(184, 160, 56),  Rgb(168, 184, 32),  Rgb(112, 88, 152),
            Rgb(184, 184, 208), Rgb(240, 128, 48),  Rgb(104, 144, 240), Rgb(120, 200, 80),
            Rgb(248, 208, 48),  Rgb(248, 88, 136),  Rgb(152, 216, 216), Rgb(112, 56, 248),
            Rgb(112, 88, 72),   Rgb(238, 153, 172),
        };
        return type < 18 ? colors[type] : Rgb(110, 110, 110);
    }

    const char *EffectLabel(u8 effect)
    {
        switch (effect)
        {
        case 0:  return "No effect";
        case 2:  return "Super effective";
        case 3:  return "Not very effective";
        default: return "";
        }
    }

    const char *BattlePocketName(u8 pocket)
    {
        static const char *const names[] = {
            "Rotom Powers", "HP/PP Restore", "Status Restore", "Pok\xC3\xA9 Balls", "Battle Items",
        };
        return pocket < BattlePocketCount ? names[pocket] : "?";
    }

    const char *BattlePocketButton(u8 pocket)
    {
        static const char *const buttons[] = {
            "battle_bag.pocket_rotom_powers", "battle_bag.pocket_hp_pp", "battle_bag.pocket_status",
            "battle_bag.pocket_balls", "battle_bag.pocket_battle_items",
        };
        return pocket < BattlePocketCount ? buttons[pocket] : nullptr;
    }

    const char *TargetButton(u8 slot)
    {
        static const char *const buttons[] = { "battle_target.top_left", "battle_target.top_right",
                                               "battle_target.bottom_left", "battle_target.bottom_right" };
        return slot < TargetCount ? buttons[slot] : nullptr;
    }

    const char *TargetLabel(u8 slot)
    {
        static const char *const labels[] = { "Top left", "Top right", "Bottom left", "Bottom right" };
        return slot < TargetCount ? labels[slot] : "?";
    }

    bool IsBallItem(u16 item)
    {
        // Master .. Cherish, Fast .. Park, Dream, Beast.
        return (item >= 1 && item <= 16) || (item >= 492 && item <= 500) || item == 576 || item == 851;
    }

    bool IsHpOnlyItem(u16 item)
    {
        // Max / Hyper / Super Potion, Potion; Fresh Water, Soda Pop,
        // Lemonade, Moomoo Milk, Energy Powder, Energy Root; Berry Juice;
        // Oran, Sitrus Berry.
        switch (item)
        {
        case 17: case 24: case 25: case 26:
        case 30: case 31: case 32: case 33: case 34: case 35:
        case 43: case 155: case 158:
            return true;
        default:
            return false;
        }
    }

    const char *CommandLabel(BattleCommand command)
    {
        static const char *const names[] = { "Fight", "Bag", "Pok\xC3\xA9mon", "Run" };
        return (u8)command < (u8)BattleCommand::Count ? names[(u8)command] : "?";
    }

    u8 BuildMoveRows(MoveRow rows[4], const u16 moves[4], const u8 pp[4], const u8 maxPp[4], const u8 type[4],
                     const u8 effect[4], u8 listCount)
    {
        u8 count = 0;
        for (u8 i = 0; i < 4 && moves[i] != 0; i++)
        {
            MoveRow &row = rows[count++];
            row = MoveRow();
            row.move = moves[i];
            row.pp = pp[i];
            row.maxPp = maxPp[i];
            if (i < listCount)
            {
                row.type = type[i];
                row.effect = effect[i];
            }
        }
        for (u8 i = count; i < 4; i++)
            rows[i] = MoveRow();
        return count;
    }

    u8 StepCursor(u8 cursor, u8 count, s32 delta)
    {
        if (count == 0)
            return 0;
        return (u8)(((s32)cursor + delta % count + count) % count);
    }

    static std::string Format(const char *fmt, unsigned a, unsigned b = 0)
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), fmt, a, b);
        return buf;
    }

    static const char *const CommandActions[] = { "battle-fight", "battle-bag", "battle-pokemon", "battle-flee" };
    static const char *const MoveActions[] = { "battle-move1", "battle-move2", "battle-move3", "battle-move4" };
    static const char *const MoveBack = "battle-moves-back";
    static const char *const PartySlots[] = { "party.slot1", "party.slot2", "party.slot3",
                                              "party.slot4", "party.slot5", "party.slot6" };
    static const char *const PickSlots[] = { "bag_party.slot1", "bag_party.slot2", "bag_party.slot3",
                                             "bag_party.slot4", "bag_party.slot5", "bag_party.slot6" };

    // Which page shows for the game's screen. Other screens (intro objects,
    // questions the menu doesn't know) get the helper; one not reached from
    // the Bag or Pokémon list shows only once it has stayed (Visible).
    static BattlePage PageFor(const BattleSnapshot &snap)
    {
        if (snap.afterCatch)
            return BattlePage::Game;
        if (!snap.inBattle)
            return BattlePage::Hidden;
        switch (snap.menu)
        {
        case GameMenu::Command:     return BattlePage::Command;
        case GameMenu::Moves:       return BattlePage::Moves;
        case GameMenu::Pokemon:
        case GameMenu::PokemonPage: return BattlePage::Party;
        case GameMenu::Bag:         return BattlePage::Bag;
        case GameMenu::Question:
            return snap.question == QuestionNextPokemon ? BattlePage::Question : BattlePage::Game;
        case GameMenu::Other:       return BattlePage::Game;
        default:                    return BattlePage::Hidden;
        }
    }

    void BattleMenu::Enter(BattlePage page, const BattleSnapshot &snap)
    {
        m_fromGame = (m_page == BattlePage::Game && m_menu != GameMenu::Other) || m_page == BattlePage::Party
                     || m_page == BattlePage::Bag;
        // The Pokémon list without the Pokémon command: the game asks for
        // the next Pokémon (a faint), so there is no way back.
        if (page == BattlePage::Party)
            m_forced = !m_sentPokemon;
        if (page != BattlePage::Hidden)
            m_sentPokemon = false;
        m_page = page;
        m_menu = snap.menu;
        m_afterCatch = snap.afterCatch;
        m_stable = 0;
        m_pending = false;
        m_pendingFrames = 0;
        m_queued = nullptr;
        m_note.clear();
        m_popup = false;
        m_popupCursor = 0;
        m_goal = Goal::None;
        m_busyText.clear();
        m_stepSent = false;
        // Fight first every turn; the move list starts where the game's does
        // (it remembers the last move used).
        m_cursor = page == BattlePage::Moves && snap.gameCursor < snap.moveCount ? snap.gameCursor : 0;
        if (page == BattlePage::Party)
        {
            // The first Pokémon that could come in.
            for (u8 i = 1; i < snap.partyCount; i++)
                if (snap.party[i].hp > 0 && !snap.party[i].egg)
                {
                    m_cursor = i;
                    break;
                }
        }
        m_bagCursor = 0;
        m_bagTop = 0;
        m_wantPocket = -1;
        m_seenPocket = snap.bagPocket;
        m_seenPick = false;
        m_pickCursor = 0;
    }

    void BattleMenu::StartGoal(Goal goal, int arg, const std::string &busy, u16 item)
    {
        m_goal = goal;
        m_goalArg = arg;
        m_goalItem = item;
        m_goalFrames = 0;
        m_goalSteps = 0;
        m_tries = 0;
        m_lastTap = nullptr;
        m_stepSent = false;
        m_used = false;
        m_busyText = busy;
        m_note.clear();
    }

    void BattleMenu::EndGoal(const std::string &note)
    {
        m_goal = Goal::None;
        m_busyText.clear();
        m_note = note;
    }

    u32 BattleMenu::GameSignature(const BattleSnapshot &snap) const
    {
        u32 sig = (u32)snap.menu;
        sig = sig * 31 + snap.bagPocket;
        sig = sig * 31 + (snap.bagItemPage ? 1 : 0) + (snap.bagPick ? 2 : 0);
        sig = sig * 31 + snap.bagPickCursor;
        sig = sig * 31 + snap.bagPageIndex;
        sig = sig * 31 + snap.bagItemCount;
        sig = sig * 31 + snap.question;
        sig = sig * 31 + snap.partyCount;
        return sig;
    }

    void BattleMenu::Tap(BattleMenuOutput &out, const char *button, u32 limit)
    {
        // The action layer refuses a button nobody has tapped yet; say so
        // instead of waiting for it.
        const LayoutButton *known = button != nullptr ? FindLayoutButton(button) : nullptr;
        if (known == nullptr || known->verified != LayoutVerified::Tap)
        {
            EndGoal("Not tried in the game yet: use the bottom screen");
            return;
        }
        m_tries = button == m_lastTap ? m_tries + 1 : 1;
        m_lastTap = button;
        if (m_tries > limit)
        {
            EndGoal("The game didn't answer, try again");
            return;
        }
        out.tap = button;
        m_stepSent = true;
        m_goalSteps++;
    }

    void BattleMenu::PressB(BattleMenuOutput &out)
    {
        static const char *const pressB = "press B";
        m_tries = m_lastTap == pressB ? m_tries + 1 : 1;
        m_lastTap = pressB;
        if (m_tries > 3)
        {
            EndGoal("The game didn't answer, try again");
            return;
        }
        out.press = BattleKeys::B;
        m_stepSent = true;
        m_goalSteps++;
    }

    void BattleMenu::Drive(const BattleSnapshot &snap, BattleMenuOutput &out)
    {
        if (m_goal == Goal::None)
            return;
        if (++m_goalFrames > GoalFrames || m_goalSteps > GoalSteps)
        {
            EndGoal("The game didn't answer, try again");
            return;
        }
        if (snap.actionRunning)
            return;
        if (m_stepSent)
        {
            // Our step is done: look again once the game has settled.
            m_stepSent = false;
            m_gameStable = 0;
            return;
        }
        if (m_gameStable < StepSettleFrames || m_stable < ListSettleFrames || m_gapFrames > 0)
            return;

        switch (m_goal)
        {
        case Goal::Switch:
            if (snap.menu == GameMenu::Pokemon)
            {
                if (m_used)
                    EndGoal("The game didn't switch, try again");
                else if (m_goalArg >= 0 && m_goalArg < 6)
                    Tap(out, PartySlots[m_goalArg]);
            }
            else if (snap.menu == GameMenu::PokemonPage)
            {
                Tap(out, "battle_party_page.switch", 2);
                m_used = true;
            }
            else
                EndGoal("");
            break;
        case Goal::PartyBack:
            if (snap.menu == GameMenu::Pokemon)
            {
                Tap(out, "battle_party.back");
                m_used = true;
            }
            else if (snap.menu == GameMenu::PokemonPage)
                Tap(out, "battle_party_page.back");
            else
                EndGoal("");
            break;
        case Goal::Tab:
            if (snap.menu != GameMenu::Bag)
                EndGoal("");
            else if (snap.bagPick)
                PressB(out);
            else if (snap.bagItemPage)
                Tap(out, "battle_bag_item_page.back");
            else if (snap.bagPocket == m_goalArg)
                EndGoal("");
            else
                Tap(out, BattlePocketButton((u8)m_goalArg));
            break;
        case Goal::Use:
            if (snap.menu != GameMenu::Bag || snap.bagPick)
                EndGoal("");    // used (a ball, the Bag closed), or the game asks for a Pokémon
            else if (!snap.bagItemPage)
            {
                if (m_used)
                    EndGoal("The game didn't use it");
                else if (m_goalArg >= snap.bagItemCount || snap.bagItems[m_goalArg].id != m_goalItem)
                    EndGoal("The Bag changed, choose again");
                else
                    Tap(out, "bag.item_row1");  // its page; the arrows then go to the item
            }
            else if (snap.bagPageIndex >= snap.bagItemCount)
                EndGoal("The game didn't answer, try again");
            else if (snap.bagPageIndex != m_goalArg)
                Tap(out, snap.bagPageIndex < m_goalArg ? "battle_bag_item_page.next_item"
                                                       : "battle_bag_item_page.prev_item",
                    snap.bagItemCount + 1u);
            else if (snap.bagItems[m_goalArg].id != m_goalItem)
                EndGoal("The Bag changed, choose again");
            else
            {
                Tap(out, "battle_bag_item_page.use", 2);
                m_used = true;
            }
            break;
        case Goal::Pick:
            // A tap on a slot off the game's cursor only moves the cursor;
            // the next tap uses the item.
            if (snap.menu != GameMenu::Bag || !snap.bagPick)
                EndGoal("");
            else if (m_goalArg >= 0 && m_goalArg < 6)
            {
                Tap(out, PickSlots[m_goalArg], 3);
                m_used = true;
            }
            break;
        case Goal::BagList:
        case Goal::BagBack:
            if (snap.menu != GameMenu::Bag)
                EndGoal("");
            else if (snap.bagPick)
                PressB(out);
            else if (snap.bagItemPage)
                Tap(out, "battle_bag_item_page.back");
            else if (m_goal == Goal::BagBack)
            {
                Tap(out, "battle_bag.back");
                m_used = true;
            }
            else
                EndGoal("");
            break;
        case Goal::Target:
        case Goal::TargetBack:
            if (snap.menu != GameMenu::Other)
                EndGoal("");
            else
            {
                Tap(out, m_goal == Goal::Target ? TargetButton((u8)m_goalArg) : "battle_target.back", 2);
                m_used = true;
            }
            break;
        case Goal::Answer:
            if (snap.menu != GameMenu::Question)
                EndGoal("");
            else
            {
                Tap(out, m_goalArg == 0 ? "battle_ask.first" : "battle_ask.second");
                m_used = true;
            }
            break;
        default:
            EndGoal("");
            break;
        }
    }

    static std::string MonName(const BattleSnapshot &snap, u8 slot)
    {
        if (slot >= snap.partyCount)
            return "?";
        return snap.party[slot].name.empty() ? std::string("that Pok\xC3\xA9mon") : snap.party[slot].name;
    }

    void BattleMenu::UpdateParty(const BattleSnapshot &snap, u32 pressed)
    {
        const int count = snap.partyCount;
        if (m_cursor >= count && count > 0)
            m_cursor = (u8)(count - 1);
        if (m_popup)
        {
            if (pressed & (BattleKeys::Up | BattleKeys::Down))
                m_popupCursor ^= 1;
            else if (pressed & BattleKeys::B)
                m_popup = false;
            else if (pressed & BattleKeys::A)
            {
                m_popup = false;
                if (m_popupCursor == 0)
                    StartGoal(Goal::Switch, m_cursor, "Come back! Go, " + MonName(snap, m_cursor) + "!");
            }
            return;
        }

        int dx = 0, dy = 0;
        if (pressed & BattleKeys::Left)
            dx = -1;
        else if (pressed & BattleKeys::Right)
            dx = 1;
        else if (pressed & BattleKeys::Up)
            dy = -1;
        else if (pressed & BattleKeys::Down)
            dy = 1;
        if (dx != 0 || dy != 0)
        {
            m_cursor = (u8)PartyMove(m_cursor, dx, dy, count);
            m_note.clear();
        }
        else if ((pressed & BattleKeys::A) && m_cursor < count)
        {
            const BattlePartyMon &mon = snap.party[m_cursor];
            if (mon.egg)
                m_note = "An Egg can't battle!";
            else if (mon.hp == 0)
                m_note = MonName(snap, m_cursor) + " has no energy left to battle!";
            else if (m_cursor == 0)
                m_note = MonName(snap, m_cursor) + " is already in battle!";
            else
            {
                m_popup = true;
                m_popupCursor = 0;
                m_note.clear();
            }
        }
        else if (pressed & BattleKeys::B)
        {
            if (m_forced)
                m_note = "Choose a Pok\xC3\xA9mon to send out";
            else
                StartGoal(Goal::PartyBack, 0, "");
        }
    }

    void BattleMenu::UpdateBag(const BattleSnapshot &snap, u32 pressed)
    {
        if (snap.bagPocket != m_seenPocket)
        {
            m_seenPocket = snap.bagPocket;
            m_bagCursor = 0;
            m_bagTop = 0;
        }
        if (m_wantPocket == (s32)snap.bagPocket || m_goal != Goal::Tab)
            m_wantPocket = -1;
        if (snap.bagPick && !m_seenPick)
        {
            // Where the game's cursor is, or for an HP item the first
            // Pokémon it would help.
            m_pickCursor = snap.bagPickCursor < snap.partyCount ? snap.bagPickCursor : 0;
            const u16 item = snap.bagPageIndex < snap.bagItemCount ? snap.bagItems[snap.bagPageIndex].id : 0;
            const BattlePartyMon &on = snap.party[m_pickCursor];
            if (IsHpOnlyItem(item) && (on.hp == 0 || on.hp >= on.maxHp))
                for (u8 i = 0; i < snap.partyCount; i++)
                    if (snap.party[i].hp > 0 && snap.party[i].hp < snap.party[i].maxHp && !snap.party[i].egg)
                    {
                        m_pickCursor = i;
                        break;
                    }
        }
        m_seenPick = snap.bagPick;
        const u8 count = snap.bagItemCount;
        if (m_bagCursor >= count)
            m_bagCursor = count > 0 ? (u8)(count - 1) : 0;
        if (Busy() || snap.actionRunning)
            return;

        if (snap.bagPick)
        {
            if (pressed & BattleKeys::Up)
                m_pickCursor = StepCursor(m_pickCursor, snap.partyCount, -1);
            else if (pressed & BattleKeys::Down)
                m_pickCursor = StepCursor(m_pickCursor, snap.partyCount, 1);
            else if (pressed & BattleKeys::B)
                StartGoal(Goal::BagList, 0, "");
            else if ((pressed & BattleKeys::A) && m_pickCursor < snap.partyCount)
            {
                const BattlePartyMon &mon = snap.party[m_pickCursor];
                const u16 item = snap.bagPageIndex < count ? snap.bagItems[snap.bagPageIndex].id : 0;
                if (IsHpOnlyItem(item) && (mon.hp == 0 || mon.hp >= mon.maxHp))
                    m_note = "It won't have any effect.";
                else
                    StartGoal(Goal::Pick, m_pickCursor,
                              "Using " + (snap.bagPageIndex < count ? snap.bagItems[snap.bagPageIndex].name : std::string("it"))
                              + " on " + MonName(snap, m_pickCursor) + "...");
            }
            if (pressed & (BattleKeys::Up | BattleKeys::Down))
                m_note.clear();
            return;
        }

        if (pressed & (BattleKeys::Left | BattleKeys::L | BattleKeys::Right | BattleKeys::R))
        {
            const s32 delta = (pressed & (BattleKeys::Left | BattleKeys::L)) ? -1 : 1;
            m_wantPocket = StepCursor(snap.bagPocket, BattlePocketCount, delta);
            StartGoal(Goal::Tab, m_wantPocket, "");
        }
        else if (pressed & (BattleKeys::Up | BattleKeys::Down))
        {
            m_bagCursor = StepCursor(m_bagCursor, count, (pressed & BattleKeys::Up) ? -1 : 1);
            if (m_bagCursor < m_bagTop)
                m_bagTop = m_bagCursor;
            else if (m_bagCursor >= m_bagTop + BagRows)
                m_bagTop = (u8)(m_bagCursor - BagRows + 1);
            m_note.clear();
        }
        else if ((pressed & BattleKeys::A) && m_bagCursor < count)
        {
            const BattleBagItem &item = snap.bagItems[m_bagCursor];
            const std::string name = item.name.empty() ? std::string("it") : item.name;
            StartGoal(Goal::Use, m_bagCursor, (IsBallItem(item.id) ? "Throwing " : "Using ") + name + "...", item.id);
        }
        else if (pressed & BattleKeys::B)
            StartGoal(Goal::BagBack, 0, "");
    }

    void BattleMenu::UpdateQuestion(const BattleSnapshot &, u32 pressed)
    {
        if (pressed & (BattleKeys::Up | BattleKeys::Down))
            m_cursor ^= 1;
        else if (pressed & BattleKeys::A)
            StartGoal(Goal::Answer, m_cursor, "");
    }

    void BattleMenu::UpdateTarget(const BattleSnapshot &, u32 pressed)
    {
        // Two rows of two, as the game lays them out.
        if (pressed & (BattleKeys::Left | BattleKeys::Right))
            m_cursor ^= 1;
        else if (pressed & (BattleKeys::Up | BattleKeys::Down))
            m_cursor ^= 2;
        else if (pressed & BattleKeys::A)
            StartGoal(Goal::Target, m_cursor, "");
        else if (pressed & BattleKeys::B)
            StartGoal(Goal::TargetBack, 0, "");
        if (pressed & (BattleKeys::Left | BattleKeys::Right | BattleKeys::Up | BattleKeys::Down))
            m_note.clear();
    }

    BattleMenuOutput BattleMenu::Update(const BattleSnapshot &snap, u32 pressed)
    {
        BattleMenuOutput out;
        BattlePage page = PageFor(snap);
        if (m_moveAge < 0xFFFF)
            m_moveAge++;
        if (page == BattlePage::Command || !snap.inBattle)
            m_moveAge = 0xFFFF;
        if (page == BattlePage::Command)
            m_seenCommand = true;
        else if (!snap.inBattle)
            m_seenCommand = false;
        else if (page == BattlePage::Game && snap.menu == GameMenu::Other && m_moveAge < TargetWindowFrames)
            page = BattlePage::Target;
        if (m_page == BattlePage::Target && page == BattlePage::Game && snap.menu == GameMenu::Other)
            page = BattlePage::Target;  // stays the target screen while it is up
        // Between the Pokémon list and a Pokémon's page the game shows no
        // screen, then its command menu, for a few frames: until a goal's
        // last tap is sent, the page stays through that.
        const bool steps = m_page == BattlePage::Party || m_page == BattlePage::Bag || m_page == BattlePage::Question
                           || m_page == BattlePage::Target;
        if ((page == BattlePage::Hidden || page == BattlePage::Command) && steps && Busy() && !m_used && snap.inBattle
            && m_gapFrames < GapFrames)
        {
            m_gapFrames++;
            page = m_page;
        }
        else
            m_gapFrames = 0;
        if (page != m_page || snap.afterCatch != m_afterCatch || (page == BattlePage::Game && snap.menu != m_menu))
            Enter(page, snap);
        m_menu = snap.menu;
        if (m_stable < 0xFFFF)
            m_stable++;
        const u32 sig = GameSignature(snap);
        if (sig != m_gameSig)
        {
            m_gameSig = sig;
            m_gameStable = 0;
        }
        else if (m_gameStable < 0xFFFF)
            m_gameStable++;

        switch (m_page)
        {
        case BattlePage::Party:
        case BattlePage::Bag:
        case BattlePage::Question:
        case BattlePage::Target:
            out.capture = true;
            out.keys = m_page == BattlePage::Bag ? BattleBagKeys : BattleMenuKeys;
            if (!Busy() && !snap.actionRunning)
            {
                if (m_page == BattlePage::Party)
                    UpdateParty(snap, pressed);
                else if (m_page == BattlePage::Question)
                    UpdateQuestion(snap, pressed);
                else if (m_page == BattlePage::Target)
                    UpdateTarget(snap, pressed);
            }
            if (m_page == BattlePage::Bag)
                UpdateBag(snap, pressed);
            Drive(snap, out);
            return out;
        case BattlePage::Command:
        case BattlePage::Moves:
            break;
        default:
            return out;
        }
        out.capture = true;
        out.keys = BattleMenuKeys;

        if (m_pending && ++m_pendingFrames > PendingFrames)
        {
            // The action gave up (it logs why); let the player choose again.
            m_pending = false;
            // Still on the move list: the game refused the move (seen with
            // Disable; a Taunt or Encore would do the same). Elsewhere the
            // taps were lost.
            m_note = m_page == BattlePage::Moves ? "The game refused that move, pick another"
                     : m_cursor == (u8)BattleCommand::Run ? "Can't escape! (trapped?)"
                                                          : "No answer from the game, try again";
        }
        if (snap.actionRunning || m_pending)
            return out;

        if (m_queued == nullptr)
        {
            if (pressed & (BattleKeys::Up | BattleKeys::Down)
                || (m_layout == BattleLayout::Bar && (pressed & (BattleKeys::Left | BattleKeys::Right))))
                m_note.clear();
            if (m_page == BattlePage::Command)
            {
                const u8 count = (u8)BattleCommand::Count;
                if (pressed & BattleKeys::Up
                    || (m_layout == BattleLayout::Bar && (pressed & BattleKeys::Left)))
                    m_cursor = StepCursor(m_cursor, count, -1);
                else if (pressed & BattleKeys::Down
                         || (m_layout == BattleLayout::Bar && (pressed & BattleKeys::Right)))
                    m_cursor = StepCursor(m_cursor, count, 1);
                else if (pressed & BattleKeys::A)
                    m_queued = CommandActions[m_cursor];
            }
            else
            {
                if (pressed & BattleKeys::Up)
                    m_cursor = StepCursor(m_cursor, snap.moveCount, -1);
                else if (pressed & BattleKeys::Down)
                    m_cursor = StepCursor(m_cursor, snap.moveCount, 1);
                else if (m_layout == BattleLayout::Bar && (pressed & (BattleKeys::Left | BattleKeys::Right)))
                {
                    // Horizontal movement stays in the grid row; an odd final
                    // row with a single move stays put. Up/Down retain list order.
                    const u8 adjacent = m_cursor ^ 1;
                    if (adjacent < snap.moveCount)
                        m_cursor = adjacent;
                }
                else if (pressed & BattleKeys::B)
                    m_queued = MoveBack;
                else if ((pressed & BattleKeys::A) && m_cursor < snap.moveCount)
                {
                    // A move without PP isn't sent while another has some;
                    // with none left the game uses Struggle whichever is tapped.
                    bool anyPp = false;
                    for (u8 i = 0; i < snap.moveCount; i++)
                        anyPp = anyPp || snap.moves[i].pp > 0;
                    const MoveRow &row = snap.moves[m_cursor];
                    if (row.pp == 0 && anyPp)
                        m_note = (row.name.empty() ? std::string("That move") : row.name) + " has no PP left";
                    else
                        m_queued = MoveActions[m_cursor];
                }
            }
        }

        const u32 settle = m_page == BattlePage::Command ? CommandSettleFrames : MovesSettleFrames;
        if (m_queued != nullptr && m_stable >= settle)
        {
            out.action = m_queued;
            if (m_queued == CommandActions[(int)BattleCommand::Pokemon])
                m_sentPokemon = true;
            if (m_page == BattlePage::Moves && m_queued != MoveBack)
                m_moveAge = 0;
            m_queued = nullptr;
            m_pending = true;
            m_pendingFrames = 0;
        }
        return out;
    }

    bool BattleMenu::Visible(void) const
    {
        // Before the first command menu only the intro's objects come up.
        if (Prompt())
            return m_seenCommand && m_stable >= PromptShowFrames;
        return m_page != BattlePage::Hidden && (m_page != BattlePage::Command || m_stable >= ShowFrames);
    }

    bool BattleMenu::Prompt(void) const
    {
        return m_page == BattlePage::Game && (m_menu == GameMenu::Other || m_menu == GameMenu::Question) && !m_fromGame
               && !m_afterCatch;
    }

    std::string BattleMenu::Status(const BattleSnapshot &snap) const
    {
        static const char *const pages[] = { "hidden", "command", "moves", "game", "party", "bag", "question", "target" };
        std::string out = std::string("battle-ui: page ") + pages[(int)m_page];
        if (m_page == BattlePage::Command)
            out += Format(", cursor %u of %u (", m_cursor, (unsigned)BattleCommand::Count)
                   + CommandLabel((BattleCommand)m_cursor) + ")";
        else if (m_page == BattlePage::Moves && m_cursor < snap.moveCount)
        {
            const MoveRow &row = snap.moves[m_cursor];
            out += Format(", cursor %u of %u (", m_cursor, snap.moveCount) + row.name + ", " + TypeName(row.type);
            const char *effect = EffectLabel(row.effect);
            out += std::string(effect[0] ? ", " : "") + effect + Format(", PP %u/%u)", row.pp, row.maxPp);
        }
        else if (m_page == BattlePage::Party)
        {
            out += Format(", cursor %u of %u", m_cursor, snap.partyCount);
            if (m_cursor < snap.partyCount)
            {
                const BattlePartyMon &mon = snap.party[m_cursor];
                out += " (" + mon.name + Format(" Lv%u %u/", mon.level, mon.hp) + Format("%u)", mon.maxHp);
            }
            if (m_forced)
                out += ", forced";
            if (m_popup)
                out += m_popupCursor == 0 ? ", menu Switch" : ", menu Cancel";
        }
        else if (m_page == BattlePage::Bag)
        {
            out += std::string(", ") + BattlePocketName(snap.bagPocket);
            if (snap.bagPick)
                out += Format(", pick %u of %u", m_pickCursor, snap.partyCount)
                       + (m_pickCursor < snap.partyCount
                          ? " (" + snap.party[m_pickCursor].name
                            + Format(" %u/%u)", snap.party[m_pickCursor].hp, snap.party[m_pickCursor].maxHp)
                          : std::string());
            else
            {
                out += Format(", cursor %u of %u", m_bagCursor, snap.bagItemCount);
                if (m_bagCursor < snap.bagItemCount)
                    out += " (" + snap.bagItems[m_bagCursor].name + Format(" x%u)", snap.bagItems[m_bagCursor].count);
            }
            if (snap.bagItemPage)
                out += ", item page";
        }
        else if (m_page == BattlePage::Question)
            out += Format(", cursor %u (", m_cursor) + (m_cursor == 0 ? "Use next Pok\xC3\xA9mon)" : "Run)");
        else if (m_page == BattlePage::Target)
            out += Format(", cursor %u (", m_cursor) + TargetLabel(m_cursor) + Format("), vt %08X", snap.otherVtable);
        else if (m_page == BattlePage::Game)
        {
            static const char *const menus[] = { "none", "command", "moves", "pokemon", "bag", "other",
                                                 "pokemon page", "question" };
            out += snap.afterCatch ? std::string(", after catch") : std::string(", game ") + menus[(int)snap.menu];
            if (Prompt())
                out += Format(", prompt, vt %08X", snap.otherVtable) + (snap.menu == GameMenu::Question
                                                                         ? Format(", message 0x%X", snap.question)
                                                                         : std::string());
        }
        if (m_pending)
            out += ", sent";
        if (Busy())
            out += ", busy" + (m_busyText.empty() ? std::string() : ": " + m_busyText);
        if (!m_note.empty())
            out += ", note: " + m_note;
        return out;
    }
}
}
