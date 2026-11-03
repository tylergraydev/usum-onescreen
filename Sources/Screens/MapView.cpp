#include "Screens/MapView.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "UI/Layout.hpp"
#include "UI/Text.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
    using UI::Rgb;

    bool operator==(const MapView &a, const MapView &b)
    {
        return a.known == b.known && a.picture == b.picture && a.indoor == b.indoor && a.hasIsland == b.hasIsland
            && a.island == b.island && a.zone == b.zone && a.x == b.x && a.z == b.z && a.yaw == b.yaw
            && std::strcmp(a.place, b.place) == 0 && std::strcmp(a.sub, b.sub) == 0
            && std::strcmp(a.key, b.key) == 0;
    }

    const char *MapIslandName(u32 index)
    {
        static const char *const islands[] = { "Melemele Island", "Akala Island", "Ula'ula Island", "Poni Island" };
        return index < 4 ? islands[index] : "";
    }

    const char *MapCompass(int yaw)
    {
        static const char *const names[] = { "S", "SE", "E", "NE", "N", "NW", "W", "SW" };
        const int sector = (((yaw % 360) + 360 + 22) % 360) / 45;
        return names[sector];
    }

    static void Copy(char *dst, size_t size, const std::string &src)
    {
        std::snprintf(dst, size, "%s", src.c_str());
    }

    MapView MakeMapView(const WhereState &where, const std::vector<std::string> &names, const std::string &key)
    {
        MapView view;
        Copy(view.key, sizeof(view.key), key);
        if (!where.valid)
            return view;
        view.known = true;
        view.zone = where.zone;
        view.indoor = where.zoneData && where.indoor;
        view.picture = where.mapView != 0;
        // The zone's own map (zone data +0x4E, map index + 1), else the
        // Rotom map view's index.
        view.hasIsland = (where.zoneData && where.mapArea != 0) || where.mapView != 0;
        view.island = where.zoneData && where.mapArea != 0 ? where.mapArea - 1u : where.mapIndex;
        view.x = (int)std::lround(where.x);
        view.z = (int)std::lround(where.z);
        view.yaw = where.yaw;
        if (where.zoneData && where.nameLine < names.size())
        {
            Copy(view.place, sizeof(view.place), names[where.nameLine]);
            if (where.nameLine + 1u < names.size())
                Copy(view.sub, sizeof(view.sub), names[where.nameLine + 1]);
        }
        return view;
    }

    bool MapZoneFilter::Feed(u16 zone, u16 entryZone, int &ignored)
    {
        ignored = -1;
        if (m_current < 0 || zone == m_current)
        {
            if (m_pending >= 0)
                ignored = m_pending;
            m_pending = -1;
            m_count = m_agree = 0;
            const bool first = m_current < 0;
            m_current = zone;
            return first;
        }
        if (zone != m_pending)
        {
            if (m_pending >= 0)
                ignored = m_pending;
            m_pending = zone;
            m_count = m_agree = 0;
        }
        m_count++;
        m_agree = zone == entryZone ? m_agree + 1 : 0;
        if (m_agree < m_stable && m_count < m_patient)
            return false;
        m_current = zone;
        m_pending = -1;
        m_count = m_agree = 0;
        return true;
    }

    std::string DescribeMapView(const MapView &view)
    {
        if (!view.known)
            return "not in the field";
        char out[256];
        std::snprintf(out, sizeof(out), "zone %u \"%s\"%s%s%s, %s, %s, x %d z %d facing %s, %s", view.zone,
                      view.place, view.sub[0] ? " / \"" : "", view.sub, view.sub[0] ? "\"" : "",
                      view.indoor ? "indoors" : "outdoors",
                      view.hasIsland ? MapIslandName(view.island) : "no island", view.x, view.z,
                      MapCompass(view.yaw), view.picture ? "picture" : "no picture");
        return out;
    }

    MapKeyResult MapKeys(bool open, bool canOpen, u32 pressed, u32 mapKey)
    {
        if (mapKey != 0 && (pressed & mapKey) == mapKey)
            return open ? MapKeyResult::Close : canOpen ? MapKeyResult::Open : MapKeyResult::None;
        if (open && (pressed & MapKeyB))
            return MapKeyResult::Close;
        return MapKeyResult::None;
    }


    // --- Drawing ---

    // The look of the game's Rotom Dex map (the bottom screen with the map
    // up) and Town Map, measured on screenshots (docs/MAP.md): Rotom's red
    // body around a recessed map window; the Town Map's light blue sea with
    // its grid where there is no picture. Text, the header window and the
    // prompts come from UI::Theme (docs/VISUAL-STYLE.md).
    namespace T = UI::Theme;

    static constexpr UI::Color BodyLine   = Rgb(0x6B, 0x21, 0x21);  // Rotom's dark edge
    static constexpr UI::Color BodyTop    = Rgb(0xE7, 0x4A, 0x42);  // body, top to bottom
    static constexpr UI::Color BodyMid    = Rgb(0xCB, 0x3E, 0x36);
    static constexpr UI::Color BodyBottom = Rgb(0xA8, 0x31, 0x31);
    static constexpr UI::Color Recess     = Rgb(0x9C, 0x30, 0x30);  // around the map window
    static constexpr UI::Color RecessLine = Rgb(0x3B, 0x33, 0x33);
    static constexpr UI::Color Sea        = Rgb(0x0A, 0xB5, 0xEA);  // Town Map sea
    static constexpr UI::Color SeaGrid    = Rgb(0x32, 0xBE, 0xE9);
    static constexpr s32 SeaGridStep      = 22;
    static constexpr UI::Color Inside     = T::OptionGlint;         // "Inside" in yellow

    static UI::Color Mix(UI::Color a, UI::Color b, float t)
    {
        return Rgb((UI::u8)(a.r + (b.r - a.r) * t), (UI::u8)(a.g + (b.g - a.g) * t), (UI::u8)(a.b + (b.b - a.b) * t));
    }

    // Rotom's body: a dark edge, then red getting darker towards the bottom.
    static void DrawBody(UI::Canvas &canvas)
    {
        canvas.FillRoundRect(MapArea, MapRadius, BodyLine);
        const UI::Rect inner = UI::Inset(MapArea, 2);
        const s32 radius = MapRadius - 2;
        canvas.FillRoundRect(inner, radius, BodyBottom);
        canvas.SetClip(UI::Rect{ inner.x, inner.y, inner.w, radius });
        canvas.FillRoundRect(inner, radius, BodyTop);
        canvas.ResetClip();
        const s32 top = inner.y + radius, rows = inner.h - 2 * radius;
        for (s32 i = 0; i < rows; i++)
        {
            const float t = (float)i / (rows - 1);
            const UI::Color c = t < 0.5f ? Mix(BodyTop, BodyMid, t * 2) : Mix(BodyMid, BodyBottom, t * 2 - 1);
            canvas.HLine(inner.x, top + i, inner.w, c);
        }
    }

    // A compass in a small window: N on top, the player's facing as the
    // game's red arrow.
    static void DrawCompass(UI::Canvas &canvas, const UI::Rect &box, int yaw)
    {
        T::DrawWindow(canvas, box);
        T::TextStyle n;
        n.size = T::TextSize::Small;
        n.color = T::WindowText;
        n.align = UI::Align::Center;
        T::DrawText(canvas, "N", UI::Rect{ box.x, box.y + 5, box.w, 15 }, n);
        const s32 dial = box.h - 26;
        const float cx = box.x + box.w / 2.0f, cy = box.y + 20 + dial / 2.0f;
        const float r = (box.w < dial ? box.w : dial) / 2.0f - 4;
        canvas.Line(cx, cy - r, cx, cy + r, 1, T::WindowRim);
        canvas.Line(cx - r, cy, cx + r, cy, 1, T::WindowRim);
        // Screen x is east, y south: yaw 0 points down, 90 right.
        const float a = yaw * 3.14159265f / 180.0f;
        const float dx = std::sin(a), dy = std::cos(a);
        const float tipX = cx + dx * r, tipY = cy + dy * r;
        const float baseX = cx - dx * r * 0.6f, baseY = cy - dy * r * 0.6f;
        const float w = r * 0.4f, e = w + 1.5f;
        canvas.FillTriangle(tipX + dx * 1.5f, tipY + dy * 1.5f, baseX - dy * e - dx, baseY + dx * e - dy,
                            baseX + dy * e - dx, baseY - dx * e - dy, T::CursorEdge);
        canvas.FillTriangle(tipX, tipY, baseX - dy * w, baseY + dx * w, baseX + dy * w, baseY - dx * w, T::CursorRed);
    }

    // The map key's prompt ("ZR"): the theme's chip with a named badge, the
    // way it draws L and R (the theme has no ZR).
    static s32 KeyPromptWidth(const std::string &key)
    {
        return 4 + T::MeasureText(key, T::TextSize::Small) + 10 + 4;
    }

    static void DrawKeyPrompt(UI::Canvas &canvas, s32 x, s32 y, const std::string &key)
    {
        const s32 w = KeyPromptWidth(key);
        canvas.FillRoundRect(UI::Rect{ x, y, w, T::PromptHeight }, T::PromptHeight / 2, T::PromptFill);
        const UI::Rect badge{ x + 4, y + (T::PromptHeight - 16) / 2, w - 8, 16 };
        canvas.FillRoundRect(badge, 8, Rgb(0xFF, 0xFF, 0xFF));
        canvas.FillRoundRect(UI::Inset(badge, 1), 7, Rgb(0x10, 0x10, 0x10));
        T::TextStyle s;
        s.size = T::TextSize::Small;
        s.color = T::PromptText;
        s.align = UI::Align::Center;
        T::DrawText(canvas, key, badge, s);
    }

    void DrawMap(UI::Canvas &canvas, const MapView &view)
    {
        DrawBody(canvas);

        // Header window: the place name and its second line on the left,
        // the island and the position on the right.
        T::DrawWindow(canvas, MapHeader);
        const s32 left = MapHeader.x + T::WindowPad + 2, right = MapHeader.Right() - T::WindowPad - 2;
        const s32 bodyH = T::LineHeight(T::TextSize::Body), smallH = T::LineHeight(T::TextSize::Small);
        const s32 line1 = MapHeader.y + T::WindowPad, line2 = line1 + bodyH;
        T::TextStyle title;
        title.color = T::WindowText;
        T::TextStyle small;
        small.size = T::TextSize::Small;
        small.color = T::WindowRim;

        std::string place = view.place;
        if (!view.known)
            place = "Map";
        else if (place.empty())
            place = "Zone " + std::to_string(view.zone);
        // The right column takes what its text needs, up to half the window.
        const std::string island = view.known && view.hasIsland ? MapIslandName(view.island) : "";
        char pos[48] = {};
        if (view.known)
            std::snprintf(pos, sizeof(pos), "%d, %d", view.x, view.z);
        s32 sideW = std::max(T::MeasureText(island, T::TextSize::Body), T::MeasureText(pos, T::TextSize::Small));
        sideW = std::min(sideW, (right - left) / 2);
        const s32 mainW = right - left - sideW - 8;
        T::DrawText(canvas, place, UI::Rect{ left, line1, mainW, bodyH }, title);
        if (view.known && view.sub[0])
            T::DrawText(canvas, view.sub, UI::Rect{ left, line2, mainW, smallH }, small);
        T::TextStyle side = title;
        side.align = UI::Align::Right;
        T::DrawText(canvas, island, UI::Rect{ right - sideW, line1, sideW, bodyH }, side);
        side = small;
        side.align = UI::Align::Right;
        T::DrawText(canvas, pos, UI::Rect{ right - sideW, line2, sideW, smallH }, side);

        // The map window, recessed into the body as on Rotom: the copy goes
        // on top of it; without one, the Town Map's sea and a note why.
        const UI::Rect recess = UI::Inset(MapWindow, -MapRecess);
        canvas.FillRect(recess, Recess);
        canvas.StrokeRect(recess, BodyLine, 1);
        canvas.StrokeRect(UI::Inset(MapWindow, -2), RecessLine, 2);
        canvas.FillRect(MapWindow, Sea);
        for (s32 x = MapWindow.x + SeaGridStep - 1; x < MapWindow.Right(); x += SeaGridStep)
            canvas.VLine(x, MapWindow.y, MapWindow.h, SeaGrid);
        for (s32 y = MapWindow.y + SeaGridStep - 1; y < MapWindow.Bottom(); y += SeaGridStep)
            canvas.HLine(MapWindow.x, y, MapWindow.w, SeaGrid);
        if (!view.picture)
        {
            const char *why = view.known ? "Rotom's map isn't up" : "Not in the field";
            const s32 h = bodyH + 2 * T::WindowPad;
            const UI::Rect note = { MapWindow.x + 30, MapWindow.y + (MapWindow.h - h) / 2, MapWindow.w - 60, h };
            T::DrawWindow(canvas, note);
            T::TextStyle s;
            s.color = T::WindowText;
            s.align = UI::Align::Center;
            T::DrawText(canvas, why, UI::Inset(note, T::WindowPad), s);
        }

        // Left of the window: the facing. Footer: inside / outside and the
        // facing in words on the left, the prompts to close on the right.
        const s32 footerY = recess.Bottom() + (MapArea.Bottom() - recess.Bottom() - T::PromptHeight) / 2;
        s32 x = MapArea.Right() - 12 - T::PromptWidth(T::Button::B, "Close");
        T::DrawButtonPrompt(canvas, x, footerY, T::Button::B, "Close");
        if (view.key[0] && std::strcmp(view.key, "B") != 0)
        {
            x -= 4 + KeyPromptWidth(view.key);
            DrawKeyPrompt(canvas, x, footerY, view.key);
        }
        if (view.known)
        {
            DrawCompass(canvas, MapCompassBox, view.yaw);
            T::TextStyle where;
            where.color = view.indoor ? Inside : T::WindowText;
            where.outline = BodyLine;
            const std::string text = std::string(view.indoor ? "Inside" : "Outside") + ", facing " + MapCompass(view.yaw);
            const s32 textX = MapArea.x + 14;
            T::DrawText(canvas, text, UI::Rect{ textX, footerY, x - 8 - textX, T::PromptHeight }, where);
        }
    }
}
