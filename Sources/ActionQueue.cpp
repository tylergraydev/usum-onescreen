#include "ActionQueue.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace OneScreen
{
    const char *const SceneField   = "FieldRo";
    const char *const SceneDex     = "Zukan";
    const char *const SceneParty   = "main:00647FCC";
    const char *const SceneBag     = "main:006488C8";
    const char *const SceneTownMap = "TownMap";
    const char *const SceneBattle  = "Battle";
    const char *const SceneStatus  = "Status";
    const char *const SceneBox     = "Box";

    static std::string Format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    static std::string Format(const char *fmt, ...)
    {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        return buf;
    }

    static std::string SceneName(const std::string &scene)
    {
        return scene.empty() ? "(none)" : scene;
    }

    bool ActionCond::Matches(const ActionWorld &world) const
    {
        if (scene != nullptr && (world.scene == scene) == sceneNot)
            return false;
        return (world.flags & set) == set && (world.flags & clear) == 0;
    }

    std::string ActionCond::Describe(void) const
    {
        static const struct { u32 flag; const char *name; } flags[] =
        {
            { WorldXMenu, "x-menu" }, { WorldBattleCommand, "battle-command" }, { WorldXMenuPage2, "x-menu-page-2" },
            { WorldBattleMoves, "battle-moves" }, { WorldPartyList, "party-list" },
            { WorldBoxIdle, "box-idle" }, { WorldBoxModal, "box-modal" }, { WorldBoxCarrying, "box-carrying" },
            { WorldLearnAsk, "learn-ask" }, { WorldLearnList, "learn-list" }, { WorldLearnConfirm, "learn-confirm" },
        };
        std::string out = scene == nullptr ? "any scene" : std::string(sceneNot ? "not " : "") + SceneName(scene);
        for (const auto &f : flags)
        {
            if (set & f.flag)
                out += std::string(" +") + f.name;
            if (clear & f.flag)
                out += std::string(" -") + f.name;
        }
        return out;
    }

    ActionCond AnyScene(void)
    {
        return ActionCond();
    }

    ActionCond InScene(const char *scene)
    {
        ActionCond c;
        c.scene = scene;
        return c;
    }

    ActionCond NotInScene(const char *scene)
    {
        ActionCond c;
        c.scene = scene;
        c.sceneNot = true;
        return c;
    }

    std::string ActionStep::Describe(void) const
    {
        switch (kind)
        {
        case StepKind::Tap:     return Format("tap %s", button);
        case StepKind::Press:   return Format("press %08lX", (unsigned long)keys);
        case StepKind::Wait:    return Format("wait %lu", (unsigned long)frames);
        case StepKind::WaitFor: return "wait-for " + until.Describe();
        case StepKind::SucceedIf: return "succeed-if " + until.Describe();
        case StepKind::XMenuEntry: return Format("x-menu entry %d", entry);
        case StepKind::BackOut: return "back-out";
        }
        return "?";
    }

    ActionStep StepTap(const char *button, u32 frames)
    {
        ActionStep s;
        s.kind = StepKind::Tap;
        s.button = button;
        s.frames = frames;
        return s;
    }

    static void (*s_menuPrepare)(std::vector<std::string> &) = nullptr;

    void ActionSetXMenuPrepare(void (*prepare)(std::vector<std::string> &))
    {
        s_menuPrepare = prepare;
    }

    static int s_menuSlots[12] = { 0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, 9 };
    static const char *const MenuButtons[12] = {
        "xmenu.slot1", "xmenu.slot2", "xmenu.slot3", "xmenu.slot4", "xmenu.slot5", "xmenu.slot6",
        "xmenu_page2.slot1", "xmenu_page2.slot2", "xmenu_page2.slot3",
        "xmenu_page2.slot4", "xmenu_page2.slot5", "xmenu_page2.slot6",
    };

    void ActionSetXMenuSlots(const int slots[12])
    {
        for (int id = 0; id < 12; id++)
            s_menuSlots[id] = slots[id] >= 0 && slots[id] < 12 ? slots[id] : -1;
    }

    int ActionXMenuSlot(int id)
    {
        return id >= 0 && id < 12 ? s_menuSlots[id] : -1;
    }

    const char *ActionXMenuButton(int id)
    {
        const int slot = ActionXMenuSlot(id);
        return slot >= 0 ? MenuButtons[slot] : nullptr;
    }

    ActionStep StepXMenuEntry(int id)
    {
        ActionStep s;
        s.kind = StepKind::XMenuEntry;
        s.entry = id;
        return s;
    }

    ActionStep StepPress(u32 keys, u32 frames)
    {
        ActionStep s;
        s.kind = StepKind::Press;
        s.keys = keys;
        s.frames = frames;
        return s;
    }

    ActionStep StepWait(u32 frames)
    {
        ActionStep s;
        s.kind = StepKind::Wait;
        s.frames = frames;
        return s;
    }

    ActionStep StepWaitFor(const ActionCond &until, u32 timeout)
    {
        ActionStep s;
        s.kind = StepKind::WaitFor;
        s.until = until;
        s.frames = timeout;
        return s;
    }

    ActionStep StepSucceedIf(const ActionCond &cond)
    {
        ActionStep s;
        s.kind = StepKind::SucceedIf;
        s.until = cond;
        return s;
    }

    ActionStep StepBackOut(u32 timeout)
    {
        ActionStep s;
        s.kind = StepKind::BackOut;
        s.frames = timeout;
        return s;
    }

    // Timeouts are in frames (60 a second). Transitions between scenes take
    // 1-2 s; a wild battle's intro up to ~12 s and Run back to the field ~15 s
    // (scenarios/wild-battle-run.txt).
    static std::vector<ActionDef> MakeActions(void)
    {
        const ActionCond fieldClosed = InScene(SceneField).Without(WorldXMenu);
        const ActionCond xmenu = InScene(SceneField).With(WorldXMenu);

        std::vector<ActionDef> list;
        // Rotom's face doesn't always answer the first tap, and while Rotom
        // chats (a question with two answers) it ignores the face: answering
        // would change the game, so go through the X menu instead. Its
        // Pokédex returns to the X menu; `close` backs out of both.
        list.push_back({ "open-pokedex", "field: tap Rotom's face, else X menu: Pokedex", fieldClosed, {
            StepTap("field.rotom_face"),
            StepWaitFor(InScene(SceneDex), 150).Retry(0, 1).Optional(),
            StepSucceedIf(InScene(SceneDex)),
            StepPress(ActionKeyX).SkipIf(xmenu),
            StepWaitFor(xmenu, 90),
            StepXMenuEntry(1),
            StepWaitFor(InScene(SceneDex), 240).Retry(5, 1),
        } });
        // The X menu remembers its cursor, so entries are tapped, never picked
        // with the D-pad. X is pressed only if the menu isn't already up.
        list.push_back({ "open-party", "X menu: tap Pokemon", InScene(SceneField), {
            StepPress(ActionKeyX).SkipIf(xmenu),
            StepWaitFor(xmenu, 90),
            StepXMenuEntry(0),
            StepWaitFor(InScene(SceneParty), 240).Retry(2, 1),
        } });
        list.push_back({ "open-bag", "X menu: tap Bag", InScene(SceneField), {
            StepPress(ActionKeyX).SkipIf(xmenu),
            StepWaitFor(xmenu, 90),
            StepXMenuEntry(2),
            StepWaitFor(InScene(SceneBag), 240).Retry(2, 1),
        } });
        // Taps during the intro do nothing, so wait for the command menu and
        // for it to slide in (a tap as it appeared only moved the cursor); if
        // it is still up after the tap, tap again.
        list.push_back({ "battle-run", "battle: tap Run", InScene(SceneBattle), {
            StepWaitFor(InScene(SceneBattle).With(WorldBattleCommand), 1200),
            StepWait(30),
            StepTap("battle_command.run"),
            StepWaitFor(AnyScene().Without(WorldBattleCommand), 120).Retry(0, 2),
            StepWaitFor(InScene(SceneField), 1800),
        } });
        list.push_back({ "close", "back out to the field", NotInScene(SceneBattle), {
            StepBackOut(1200),
        } });

        // The native battle menu (Screens/BattleMenu.hpp): one tap per choice.
        // The menu waits until the game's menu has settled before it starts
        // one (a tap as the command menu slid in only moved its cursor); if
        // the game's menu is still up after the tap, tap again. They end as
        // soon as the menu goes, wherever the game goes next (a trainer
        // battle refuses Run with a message, then shows the commands again).
        const ActionCond command = InScene(SceneBattle).With(WorldBattleCommand);
        const ActionCond moves = InScene(SceneBattle).With(WorldBattleMoves);
        const auto pick = [&](const char *name, const char *summary, const char *button, bool fromMoves) {
            return ActionDef{ name, summary, fromMoves ? moves : command, {
                StepTap(button),
                StepWaitFor(AnyScene().Without(fromMoves ? WorldBattleMoves : WorldBattleCommand), 90).Retry(0, 2),
            } };
        };
        list.push_back(pick("battle-fight", "battle: tap Fight", "battle_command.fight", false));
        list.push_back(pick("battle-bag", "battle: tap Bag", "battle_command.bag", false));
        list.push_back(pick("battle-pokemon", "battle: tap Pokemon", "battle_command.pokemon", false));
        list.push_back(pick("battle-flee", "battle: tap Run (stays in the battle if it can't run)",
                            "battle_command.run", false));
        list.push_back(pick("battle-move1", "battle: use move 1", "battle_moves.move1", true));
        list.push_back(pick("battle-move2", "battle: use move 2", "battle_moves.move2", true));
        list.push_back(pick("battle-move3", "battle: use move 3", "battle_moves.move3", true));
        list.push_back(pick("battle-move4", "battle: use move 4", "battle_moves.move4", true));
        list.push_back({ "battle-moves-back", "battle: move list back to the commands", moves, {
            StepTap("battle_moves.back"),
            StepWaitFor(command, 90).Retry(0, 2),
        } });
        return list;
    }

    const std::vector<ActionDef> &Actions(void)
    {
        static const std::vector<ActionDef> list = MakeActions();
        return list;
    }

    const ActionDef *FindAction(const std::string &name)
    {
        for (const ActionDef &def : Actions())
            if (name == def.name)
                return &def;
        return nullptr;
    }

    const LayoutButton *FindLayoutButton(const char *name)
    {
        for (const LayoutButton &button : LayoutButtons)
            if (std::strcmp(button.name, name) == 0)
                return &button;
        return nullptr;
    }

    const char *BackButtonFor(const std::string &scene, u32 /*flags*/)
    {
        if (scene == SceneField)
            return nullptr;
        if (scene == "MyAlbum")    return "xmenu.back";
        if (scene == SceneDex)     return "pokedex_top.back";
        if (scene == SceneParty)   return "party.back";
        if (scene == SceneBag)     return "bag.back";
        if (scene == SceneTownMap) return "town_map.back";
        return nullptr;
    }

    u32 BackKeysFor(const std::string &scene, u32 flags)
    {
        return scene == SceneField && (flags & WorldXMenu) ? ActionKeyB : 0;
    }

    std::string CheckAction(const ActionDef &def)
    {
        for (u32 i = 0; i < def.steps.size(); i++)
        {
            const ActionStep &step = def.steps[i];
            if (step.kind == StepKind::XMenuEntry)
            {
                if (step.entry == 9)
                    return Format("step %lu: qr_scanner not available", (unsigned long)i + 1);
                const char *name = ActionXMenuButton(step.entry);
                if (name == nullptr)
                    return Format("step %lu: x-menu entry %d locked or absent", (unsigned long)i + 1, step.entry);
                const LayoutButton *button = FindLayoutButton(name);
                if (button == nullptr || button->verified != LayoutVerified::Tap)
                    return Format("step %lu: x-menu slot was never verified by a tap", (unsigned long)i + 1);
            }
            if (step.kind == StepKind::Tap)
            {
                const LayoutButton *button = step.button ? FindLayoutButton(step.button) : nullptr;
                if (button == nullptr)
                    return Format("step %lu: unknown button %s", (unsigned long)i + 1, step.button ? step.button : "(null)");
                if (button->verified != LayoutVerified::Tap)
                    return Format("step %lu: %s was never verified by a tap", (unsigned long)i + 1, step.button);
            }
            if (step.retryFrom >= 0 && (u32)step.retryFrom > i)
                return Format("step %lu: retries forward", (unsigned long)i + 1);
        }
        return "";
    }

    bool ActionRunner::Start(const ActionDef &def, const ActionWorld &world, std::vector<std::string> &log)
    {
        if (m_def != nullptr)
        {
            log.push_back(Format("action: %s refused: %s is running", def.name, m_def->name));
            return false;
        }
        if ((world.flags & WorldTrainerBattle) != 0
            && (std::strcmp(def.name, "battle-flee") == 0 || std::strcmp(def.name, "battle-run") == 0))
        {
            log.push_back("action: No running from a Trainer battle!");
            return false;
        }
        if ((world.flags & WorldRoyalBattle) != 0 && std::strcmp(def.name, "battle-bag") == 0)
        {
            log.push_back("action: No Bag in a Battle Royal");
            return false;
        }
        if (s_menuPrepare != nullptr)
            for (const ActionStep &step : def.steps)
                if (step.kind == StepKind::XMenuEntry)
                {
                    s_menuPrepare(log);
                    break;
                }
        const std::string bad = CheckAction(def);
        if (!bad.empty())
        {
            log.push_back(Format("action: %s refused: %s", def.name, bad.c_str()));
            return false;
        }
        if (!def.needs.Matches(world))
        {
            log.push_back(Format("action: %s refused: needs %s, now %s", def.name, def.needs.Describe().c_str(),
                                 SceneName(world.scene).c_str()));
            return false;
        }
        m_succeeded = false;
        m_def = &def;
        m_step = 0;
        m_frame = 0;
        m_total = 0;
        m_retries.assign(def.steps.size(), 0);
        m_taps = 0;
        m_tapping = nullptr;
        m_pressing = 0;
        m_stable = 0;
        m_last = world;
        log.push_back(Format("action: %s started (%s)", def.name, def.summary));
        return true;
    }

    void ActionRunner::Next(void)
    {
        m_step++;
        m_frame = 0;
        m_taps = 0;
        m_tapping = nullptr;
        m_pressing = 0;
        m_stable = 0;
    }

    void ActionRunner::Finish(bool ok, const std::string &why, const ActionWorld &world, std::vector<std::string> &log)
    {
        m_succeeded = ok;
        if (ok)
            m_lastResult = Format("%s ok in %lu frames, scene %s", m_def->name, (unsigned long)m_total,
                                  SceneName(world.scene).c_str());
        else
            m_lastResult = Format("%s FAILED at step %lu/%lu (%s): %s; scene %s after %lu frames", m_def->name,
                                  (unsigned long)m_step + 1, (unsigned long)m_def->steps.size(),
                                  m_def->steps[m_step].Describe().c_str(), why.c_str(),
                                  SceneName(world.scene).c_str(), (unsigned long)m_total);
        log.push_back("action: " + m_lastResult);
        m_def = nullptr;
    }

    void ActionRunner::Cancel(std::vector<std::string> &log)
    {
        if (m_def == nullptr)
            return;
        ActionWorld world = m_last;
        Finish(false, "cancelled", world, log);
    }

    ActionOutput ActionRunner::Update(const ActionWorld &world, std::vector<std::string> &log)
    {
        ActionOutput out;
        if (m_def == nullptr)
            return out;

        if (world.scene == m_last.scene && world.flags == m_last.flags)
            m_stable++;
        else
            m_stable = 0;
        m_last = world;
        m_total++;

        if (m_step >= m_def->steps.size())
        {
            Finish(true, "", world, log);
            return out;
        }

        while (m_step < m_def->steps.size() && m_frame == 0 && m_def->steps[m_step].hasSkip
               && m_def->steps[m_step].skipIf.Matches(world))
            Next();
        if (m_step >= m_def->steps.size())
        {
            Finish(true, "", world, log);
            return out;
        }

        const ActionStep &step = m_def->steps[m_step];
        const u32 frame = m_frame++;
        if (frame == 0 && step.hasGuard && !step.guard.Matches(world))
        {
            Finish(false, "input guard changed", world, log);
            return out;
        }
        switch (step.kind)
        {
        case StepKind::Tap:
        {
            const LayoutButton *button = FindLayoutButton(step.button);
            if (frame < step.frames)
            {
                out.touching = true;
                out.x = (u16)button->tapX;
                out.y = (u16)button->tapY;
            }
            else if (frame + 1 >= step.frames + ActionGapFrames)
                Next();
            break;
        }
        case StepKind::XMenuEntry:
        {
            if (frame == 0)
            {
                const char *name = ActionXMenuButton(step.entry);
                if (name == nullptr || step.entry == 9)
                {
                    Finish(false, "x-menu entry not available", world, log);
                    break;
                }
                m_menuPage = ActionXMenuSlot(step.entry) / 6;
                m_tapping = FindLayoutButton(name);
                m_menuPhase = 0;
                m_tapFrame = 0;
                m_taps = 0;
            }
            // Page arrow, probe (45 frames, two retries), settle, then slot.
            const bool page2 = (world.flags & WorldXMenuPage2) != 0;
            if (m_menuPhase == 0)
            {
                if (m_tapFrame == 0 && world.scene == SceneField && (world.flags & WorldXMenu)
                    && page2 == (m_menuPage == 1))
                {
                    m_menuPhase = 2;
                    break;
                }
                const LayoutButton *arrow = FindLayoutButton(m_menuPage ? "xmenu.next_page" : "xmenu_page2.prev_page");
                if (m_tapFrame < ActionHoldFrames)
                {
                    out.touching = true;
                    out.x = (u16)arrow->tapX;
                    out.y = (u16)arrow->tapY;
                }
                if (++m_tapFrame >= ActionHoldFrames + ActionGapFrames)
                {
                    m_menuPhase = 1;
                    m_tapFrame = 0;
                }
            }
            else if (m_menuPhase == 1)
            {
                if (world.scene == SceneField && (world.flags & WorldXMenu) && page2 == (m_menuPage == 1))
                {
                    m_menuPhase = 2;
                    m_tapFrame = 0;
                }
                else if (++m_tapFrame >= 45)
                {
                    if (++m_taps > 2)
                        Finish(false, "X menu page timeout", world, log);
                    else
                    {
                        m_menuPhase = 0;
                        m_tapFrame = 0;
                    }
                }
            }
            else if (m_menuPhase == 2)
            {
                if (++m_tapFrame >= 10)
                {
                    m_menuPhase = 3;
                    m_tapFrame = 0;
                }
            }
            else
            {
                if (m_tapFrame == 0 && (page2 != (m_menuPage == 1) || world.scene != SceneField
                                        || !(world.flags & WorldXMenu)))
                {
                    Finish(false, "X menu page changed before tap", world, log);
                    break;
                }
                if (m_tapFrame < ActionHoldFrames)
                {
                    out.touching = true;
                    out.x = (u16)m_tapping->tapX;
                    out.y = (u16)m_tapping->tapY;
                }
                if (++m_tapFrame >= ActionHoldFrames + ActionGapFrames)
                    Next();
            }
            break;
        }
        case StepKind::Press:
            if (frame < step.frames)
                out.keys = step.keys;
            else if (frame + 1 >= step.frames + ActionGapFrames)
                Next();
            break;
        case StepKind::Wait:
            if (frame + 1 >= step.frames)
                Next();
            break;
        case StepKind::WaitFor:
            if (step.until.Matches(world))
                Next();
            else if (frame >= step.frames)
            {
                u8 &used = m_retries[m_step];
                if (step.retryFrom >= 0 && used < step.retries)
                {
                    used++;
                    log.push_back(Format("action: %s step %lu (%s) timed out, retry %u from step %d", m_def->name,
                                         (unsigned long)m_step + 1, step.Describe().c_str(), used, step.retryFrom + 1));
                    m_step = (u32)step.retryFrom;
                    m_frame = 0;
                }
                else if (step.optional)
                {
                    log.push_back(Format("action: %s step %lu (%s) timed out, going on", m_def->name,
                                         (unsigned long)m_step + 1, step.Describe().c_str()));
                    Next();
                }
                else
                    Finish(false, Format("timeout after %lu frames", (unsigned long)step.frames), world, log);
            }
            break;
        case StepKind::SucceedIf:
            if (step.until.Matches(world))
            {
                Finish(true, "", world, log);
                return out;
            }
            Next();
            break;
        case StepKind::BackOut:
        {
            // A back tap or press in progress: hold, then the gap.
            if (m_tapping != nullptr || m_pressing != 0)
            {
                if (m_tapFrame < ActionHoldFrames)
                {
                    out.keys = m_pressing;
                    out.touching = m_tapping != nullptr;
                    out.x = m_tapping ? (u16)m_tapping->tapX : 0;
                    out.y = m_tapping ? (u16)m_tapping->tapY : 0;
                }
                if (++m_tapFrame >= ActionHoldFrames + ActionGapFrames)
                {
                    m_tapping = nullptr;
                    m_pressing = 0;
                    m_stable = 0;
                }
                break;
            }
            if (frame >= step.frames)
            {
                Finish(false, Format("timeout after %lu frames", (unsigned long)step.frames), world, log);
                break;
            }
            if (m_taps > 0 && !m_tapChanged)
            {
                m_sinceTap++;
                m_tapChanged = world.scene != m_tapWorld.scene || world.flags != m_tapWorld.flags;
            }
            // Decide only once the screen has looked the same for a while: a
            // scene change passes through (none), and the X menu fades in.
            if (world.scene.empty() || m_stable < SettleFrames)
                break;
            if (world.scene == SceneField && !(world.flags & WorldXMenu))
            {
                if (m_stable >= FieldSettleFrames)
                    Next();
                break;
            }
            const char *name = BackButtonFor(world.scene, world.flags);
            const u32 keys = BackKeysFor(world.scene, world.flags);
            if (name == nullptr && keys == 0)
            {
                Finish(false, "no back button known for this screen", world, log);
                break;
            }
            // Leaving a screen can take a while (the Pokédex keeps its scene
            // for over a second): tap again only once the last tap changed
            // something, or after RetapFrames.
            if (m_taps > 0 && !m_tapChanged && m_sinceTap < RetapFrames)
                break;
            if (m_taps >= BackOutMaxTaps)
            {
                Finish(false, Format("still not in the field after %lu back taps", (unsigned long)m_taps), world, log);
                break;
            }
            m_taps++;
            m_tapping = name ? FindLayoutButton(name) : nullptr;
            m_pressing = name ? 0 : keys;
            m_tapWorld = world;
            m_tapChanged = false;
            m_sinceTap = 0;
            log.push_back(Format("action: %s back %lu: %s on %s", m_def->name, (unsigned long)m_taps,
                                 name ? name : Format("press %08lX", (unsigned long)keys).c_str(),
                                 SceneName(world.scene).c_str()));
            out.keys = m_pressing;
            out.touching = m_tapping != nullptr;
            out.x = m_tapping ? (u16)m_tapping->tapX : 0;
            out.y = m_tapping ? (u16)m_tapping->tapY : 0;
            m_tapFrame = 1;
            break;
        }
        }

        if (m_def != nullptr && m_step >= m_def->steps.size())
            Finish(true, "", world, log);
        return out;
    }

    std::string ActionRunner::Status(void) const
    {
        std::string out;
        if (m_def != nullptr && m_step < m_def->steps.size())
            out = Format("action: running %s, step %lu/%lu (%s), frame %lu", m_def->name, (unsigned long)m_step + 1,
                         (unsigned long)m_def->steps.size(), m_def->steps[m_step].Describe().c_str(),
                         (unsigned long)m_frame);
        else
            out = "action: idle";
        if (!m_lastResult.empty())
            out += ", last: " + m_lastResult;
        return out;
    }
}
