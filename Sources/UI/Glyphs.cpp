#include "UI/Theme.hpp"

namespace OneScreen
{
namespace UI
{
namespace Theme
{
    const char *ButtonName(Button button, GlyphFamily family)
    {
        static const char *names[][10] = {
            { "A", "B", "X", "Y", "L", "R", "START", "SELECT", "ZL", "ZR" },
            { "B", "A", "Y", "X", "LB", "RB", "Menu", "View", "LT", "RT" },
            { "Circle", "Cross", "Triangle", "Square", "L1", "R1", "Options", "Share", "L2", "R2" },
            { "A", "B", "X", "Y", "L", "R", "+", "-", "ZL", "ZR" },
        };
        return names[(unsigned)family][(unsigned)button];
    }

    std::string FormatPanelHint(const std::string &text)
    {
        std::string out = text;
        size_t at = 0;
        const std::string mapped = "(" + FormatPromptText("L+R");
        while ((at = out.find("(L+R", at)) != std::string::npos)
        {
            out.replace(at, 4, mapped);
            at += mapped.size();
        }
        return out;
    }

    std::string FormatPromptText(const std::string &text)
    {
        if (GetSettings().glyphs == GlyphFamily::ThreeDS) return text;
        std::string out;
        auto word = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                                            || (c >= '0' && c <= '9') || c == '_'; };
        for (size_t i = 0; i < text.size();)
        {
            if (!word(text[i])) { out += text[i++]; continue; }
            size_t end = i + 1;
            while (end < text.size() && word(text[end])) ++end;
            const std::string token = text.substr(i, end - i);
            bool found = false;
            for (unsigned b = 0; b < 10; ++b)
                if (token == ButtonName((Button)b, GlyphFamily::ThreeDS)
                    || (b == (unsigned)Button::Start && token == "Start")
                    || (b == (unsigned)Button::Select && token == "Select"))
                {
                    out += ButtonName((Button)b, GetSettings().glyphs);
                    found = true;
                    break;
                }
            if (!found) out += token;
            i = end;
        }
        return out;
    }

}
}
}
