#include "Screens/MapView.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "UI/Layout.hpp"
#include "UI/Text.hpp"

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
        view.hasIsland = where.mapView != 0;
        view.island = where.mapIndex;
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

    static constexpr UI::Color PanelBg  = Rgb(22, 30, 52);
    static constexpr UI::Color Border   = Rgb(120, 150, 220);
    static constexpr UI::Color Sea      = Rgb(30, 70, 110);
    static constexpr UI::Color TextMain = Rgb(250, 252, 255);
    static constexpr UI::Color TextDim  = Rgb(150, 165, 200);
    static constexpr UI::Color Accent   = Rgb(255, 236, 120);
    static constexpr UI::Color Arrow    = Rgb(235, 80, 70);

    // A compass rose in `box`: N on top, the player's facing as a red arrow.
    static void DrawCompass(UI::Canvas &canvas, const UI::Rect &box, int yaw)
    {
        const float cx = box.x + box.w / 2.0f, cy = box.y + box.h / 2.0f + 6;
        const float r = (box.w < box.h ? box.w : box.h) / 2.0f - 8;
        UI::TextStyle n;
        n.color = TextDim;
        n.align = UI::Align::Center;
        UI::DrawText(canvas, "N", UI::Rect{ box.x, (int)(cy - r) - 20, box.w, 18 }, n);
        canvas.Line(cx, cy - r, cx, cy + r, 1, Border);
        canvas.Line(cx - r, cy, cx + r, cy, 1, Border);
        // Screen x is east, y south: yaw 0 points down, 90 right.
        const float a = yaw * 3.14159265f / 180.0f;
        const float dx = std::sin(a), dy = std::cos(a);
        const float tipX = cx + dx * r, tipY = cy + dy * r;
        const float baseX = cx - dx * r * 0.35f, baseY = cy - dy * r * 0.35f;
        const float w = r * 0.45f;
        canvas.FillTriangle(tipX, tipY, baseX - dy * w, baseY + dx * w, baseX + dy * w, baseY - dx * w, Arrow);
    }

    void DrawMap(UI::Canvas &canvas, const MapView &view)
    {
        canvas.FillRoundRect(MapArea, MapRadius, PanelBg);
        canvas.StrokeRoundRect(MapArea, MapRadius, Border, 2);

        // Header: place name and its second line on the left, island and
        // position on the right.
        const int left = MapArea.x + 12, right = MapArea.Right() - 12;
        UI::TextStyle title;
        title.font = UI::Font::Large;
        title.color = TextMain;
        UI::TextStyle plain;
        plain.color = TextDim;
        UI::TextStyle side = plain;
        side.align = UI::Align::Right;

        std::string place = view.place;
        if (!view.known)
            place = "Map";
        else if (place.empty())
            place = "Zone " + std::to_string(view.zone);
        UI::DrawText(canvas, place, UI::Rect{ left, MapArea.y + 10, 236, 26 }, title);
        if (view.known && view.sub[0])
            UI::DrawText(canvas, view.sub, UI::Rect{ left, MapArea.y + 38, 236, 20 }, plain);

        if (view.known)
        {
            UI::TextStyle island = side;
            island.color = TextMain;
            UI::DrawText(canvas, view.hasIsland ? MapIslandName(view.island) : "",
                         UI::Rect{ right - 124, MapArea.y + 12, 124, 20 }, island);
            char pos[48];
            std::snprintf(pos, sizeof(pos), "%d, %d", view.x, view.z);
            UI::DrawText(canvas, pos, UI::Rect{ right - 124, MapArea.y + 38, 124, 20 }, side);
        }

        // The map window: the copy goes on top of this; without one, say why.
        const UI::Rect frame = UI::Inset(MapWindow, -2);
        canvas.FillRect(MapWindow, Sea);
        canvas.StrokeRect(frame, Border, 2);
        if (!view.picture)
        {
            UI::TextStyle note = plain;
            note.align = UI::Align::Center;
            note.color = TextMain;
            const char *why = view.known ? "Rotom's map isn't up" : "Not in the field";
            UI::DrawText(canvas, why, UI::Rect{ MapWindow.x, MapWindow.y + MapWindow.h / 2 - 10, MapWindow.w, 20 },
                         note);
        }

        // Left of the window: the facing. Footer: inside / outside and the
        // facing in words on the left, how to close on the right.
        const UI::Rect compass = { MapArea.x + 4, MapWindow.y + 24, MapWindow.x - MapArea.x - 10, 70 };
        const UI::Rect footer = { left, frame.Bottom() + 4, right - left, 18 };
        if (view.known)
        {
            DrawCompass(canvas, compass, view.yaw);
            UI::TextStyle where = plain;
            where.color = view.indoor ? Accent : TextDim;
            std::string text = std::string(view.indoor ? "Inside" : "Outside") + ", facing " + MapCompass(view.yaw);
            UI::DrawText(canvas, text, UI::Rect{ footer.x, footer.y, footer.w / 2, footer.h }, where);
        }
        std::string hint = "B Close";
        if (view.key[0])
            hint = std::string(view.key) + " / " + hint;
        UI::DrawText(canvas, hint, UI::Rect{ footer.x + footer.w / 2, footer.y, footer.w / 2, footer.h }, side);
    }
}
