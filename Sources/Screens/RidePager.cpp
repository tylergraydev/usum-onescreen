#include "Usum.hpp"
#include "Screens/RidePager.hpp"

#ifdef __3DS__
#include <CTRPluginFramework.hpp>
#include "Scene.hpp"
#endif

namespace OneScreen
{
    bool RidePagerValid(const RidePagerState &s)
    {
        if (!s.object || !s.count || s.count > RidePagerRows || s.cursor >= s.count
            || (s.current != 0xFF && s.current >= RidePagerRows))
            return false;
        unsigned seen = 0;
        for (unsigned r = 0; r < s.count; ++r)
        {
            if (s.ids[r] >= RidePagerRows || (seen & (1u << s.ids[r])))
                return false;
            seen |= 1u << s.ids[r];
        }
        return true;
    }

    const char *RidePagerName(unsigned id)
    {
        // No Ride Pager message-file/line mapping has been located yet.
        static const char *const names[] = { "Tauros Charge", "Stoutland Search", "Mudsdale Gallop",
            "Machamp Shove", "Lapras Paddle", "Sharpedo Jet", "Charizard Glide" };
        return id < RidePagerRows ? names[id] : "Unknown ride";
    }

    const char *RidePagerRowButton(unsigned row)
    {
        static const char *const buttons[] = { "ride_pager.row_0", "ride_pager.row_1", "ride_pager.row_2",
            "ride_pager.row_3", "ride_pager.row_4", "ride_pager.row_5", "ride_pager.row_6" };
        return row < RidePagerRows ? buttons[row] : nullptr;
    }

#ifdef __3DS__
    RidePagerState ReadRidePager(u32 sceneManager, const std::string &scene)
    {
        using namespace CTRPluginFramework;
        RidePagerState s;
        if (scene != "FieldRo")
            return s;
        const auto read = [](u32 at) { u32 value = 0; Process::Read32(at, value); return value; };
        const auto readable = [](u32 at, u32 bytes) {
            return at && at <= ~0u - bytes && Process::CheckAddress(at, MEMPERM_READ)
                && Process::CheckAddress(at + bytes - 1, MEMPERM_READ);
        };
        const u32 manager = readable(sceneManager, 4) ? read(sceneManager) : 0;
        if (!readable(manager, 32))
            return s;
        for (u32 i = 0; i < 8; ++i)
        {
            const u32 object = read(manager + 4 * i);
            if (!readable(object, 0x18))
                continue;
            const u32 vt = read(object), module = CroModuleOf(vt);
            if (!module || vt != module + Usum::RidePagerFrame || CroModuleName(module) != "FieldMenu")
                continue;
            const u32 list = read(object + 0x14);
            if (!readable(list, 0x65) || read(list) != module + Usum::RidePagerList)
                continue;
            s.object = list;
            s.count = read(list + 0x54);
            s.cursor = read(list + 0x4C);
            Process::Read8(list + 0x0C, s.current);
            u8 dialog = 0;
            Process::Read8(list + 0x64, dialog);
            s.dialog = dialog != 0;
            for (unsigned r = 0; r < RidePagerRows; ++r)
                Process::Read8(list + 0x59 + r, s.ids[r]);
            return RidePagerValid(s) ? s : RidePagerState{};
        }
        return s;
    }
#endif

    void RidePagerScreen::Begin(std::vector<std::string> &)
    {
        m_state = RidePagerState{};
        m_ready = m_busy = false;
        m_usable = m_registered = m_stable = 0;
        m_candidate = ~0u;
        ++m_version;
    }

    void RidePagerScreen::SetState(const RidePagerState &s)
    {
        bool structure = s.object != m_state.object || s.count != m_state.count || s.dialog != m_state.dialog;
        for (unsigned r = 0; r < RidePagerRows; ++r)
            structure = structure || s.ids[r] != m_state.ids[r];
        if (structure || !RidePagerValid(s))
        {
            m_ready = false;
            m_stable = m_usable = m_registered = 0;
            m_candidate = ~0u;
        }
        if (structure || s.cursor != m_state.cursor || s.current != m_state.current)
            ++m_version;
        m_state = s;
    }

    const std::vector<SamplePoint> &RidePagerScreen::Samples() const
    {
        static const std::vector<SamplePoint> points = [] {
            std::vector<SamplePoint> p;
            for (int r = 0; r < RidePagerRows; ++r)
            {
                p.push_back({ 290, (s16)(30 * r + 15) });
                p.push_back({ 44, (s16)(30 * r + 15) });
            }
            return p;
        }();
        return points;
    }

    void RidePagerScreen::Sampled(const Sample *samples, std::vector<std::string> &log)
    {
        unsigned usable = 0, registered = 0;
        bool valid = RidePagerValid(m_state) && !m_state.dialog;
        for (unsigned r = 0; valid && r < m_state.count; ++r)
        {
            const Sample &s = samples[2 * r], &plus = samples[2 * r + 1];
            const bool bright = s.r >= 185 && s.r <= 230 && s.g >= 235 && s.b >= 240;
            const bool grey = s.r >= 85 && s.r <= 130 && s.g >= 140 && s.g <= 190 && s.b >= 190 && s.b <= 235;
            const bool orange = plus.r >= 235 && plus.g >= 125 && plus.g <= 180 && plus.b <= 30;
            const bool empty = plus.r >= 190 && plus.r <= 230 && plus.g >= 190 && plus.g <= 230
                && plus.b >= 190 && plus.b <= 230;
            valid = (bright || grey) && (orange || empty);
            if (bright) usable |= 1u << r;
            if (orange) registered |= 1u << r;
        }
        if (!valid)
        {
            m_ready = false;
            m_stable = 0;
            m_candidate = ~0u;
            return;
        }
        const unsigned candidate = usable | (registered << RidePagerRows);
        if (candidate != m_candidate)
        {
            m_candidate = candidate;
            m_stable = 0;
            m_ready = false;
        }
        if (m_stable < 3) ++m_stable;
        if (m_stable == 3 && !m_ready)
        {
            m_usable = usable;
            m_registered = registered;
            m_ready = true;
            ++m_version;
            log.push_back(Status());
        }
    }

    bool RidePagerScreen::Ready() const { return m_ready && RidePagerValid(m_state) && !m_state.dialog; }
    bool RidePagerScreen::Available(unsigned row) const { return Ready() && row < m_state.count && (m_usable & (1u << row)); }
    bool RidePagerScreen::Registered(unsigned row) const { return Ready() && row < m_state.count && (m_registered & (1u << row)); }
    u32 RidePagerScreen::CaptureKeys() const
    {
        return Ready() ? ScreenKey::DPad | ScreenKey::A | ScreenKey::B | (1u << 11) : 0; // Y also selects/dismounts
    }

    static const ActionDef &PagerTap(unsigned row)
    {
        static const std::vector<ActionDef> defs = [] {
            std::vector<ActionDef> out;
            for (unsigned r = 0; r <= RidePagerRows; ++r)
                out.push_back({ r == RidePagerRows ? "ride-pager-back" : RidePagerRowButton(r), "Ride Pager tap",
                    InScene(SceneField), { StepTap(r == RidePagerRows ? "ride_pager.back" : RidePagerRowButton(r)), StepWait(60) } });
            return out;
        }();
        return defs[row];
    }

    const ActionDef *RidePagerScreen::Key(u32 key, std::vector<std::string> &log)
    {
        if (!Ready() || m_busy) return nullptr;
        const ActionDef *def = nullptr;
        if (key == ScreenKey::B) def = &PagerTap(RidePagerRows);
        else if (key == ScreenKey::Up || key == ScreenKey::Down)
            def = &ScreenPressAction(key == ScreenKey::Up ? "ride-pager-up" : "ride-pager-down", SceneField, key, 24);
        else if (key == ScreenKey::A || key == (1u << 11))
        {
            if (!Available(m_state.cursor))
            {
                log.push_back("ride-pager: unavailable row, no input sent");
                return nullptr;
            }
            const ActionDef &tap = PagerTap(m_state.cursor);
            // Rows 1/2/4/5 are measured, not tap-verified. Keep the queue's
            // verification rule: the game's A selects its memory cursor.
            def = CheckAction(tap).empty() ? &tap : &ScreenPressAction("ride-pager-select", SceneField, ScreenKey::A, 60);
        }
        if (def) { m_busy = true; ++m_version; }
        return def;
    }

    void RidePagerScreen::ActionDone(std::vector<std::string> &)
    {
        m_busy = m_ready = false;
        m_stable = 0;
        m_candidate = ~0u;
        ++m_version;
    }

    std::string RidePagerScreen::Status() const
    {
        return std::string("ride-pager: ") + (Ready() ? "ready" : "waiting") + ", row "
            + std::to_string(m_state.cursor) + ", current " + (m_state.current == 0xFF ? "on foot" : RidePagerName(m_state.current))
            + ", usable " + std::to_string(m_usable) + ", registered " + std::to_string(m_registered);
    }

    void RidePagerScreen::Draw(UI::Canvas &canvas) const
    {
        using namespace UI::Theme;
        DrawWaterBackground(canvas, Area());
        for (unsigned r = 0; r < m_state.count && r < RidePagerRows; ++r)
        {
            const UI::Rect row = { 10, (s32)(3 + 30 * r), 380, 27 };
            const bool available = Available(r);
            DrawRowBackground(canvas, row, RowKind::Option);
            if (!available) canvas.FillRect(row, UI::Rgb(80, 100, 125, 180));
            if (r == m_state.cursor) DrawCursor(canvas, 29, row.y + row.h / 2);
            TextStyle label;
            label.color = available ? OptionText : UI::Rgb(65, 75, 90);
            DrawText(canvas, RidePagerName(m_state.ids[r]), { 34, row.y, 200, row.h }, label);
            TextStyle badge;
            badge.size = TextSize::Small;
            badge.align = UI::Align::Right;
            const bool current = m_state.current == m_state.ids[r];
            badge.color = label.color;
            std::string marks = current ? "Riding" : available ? "" : "Unavailable";
            DrawText(canvas, marks, { 226, row.y, 140, row.h }, badge);
            if (Registered(r))
            {
                badge.color = UI::Rgb(170, 70, 0);
                DrawText(canvas, "+", { 370, row.y, 18, row.h }, badge);
            }
        }
        DrawScreenPrompts(canvas, Area(), { { Button::B, "Back" } },
            { { Button::A, m_busy ? "Please wait" : m_state.current == m_state.ids[m_state.cursor] ? "Get down" : "Ride" } });
    }
}
