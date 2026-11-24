#include "SettingsFile.hpp"

#include <vector>

namespace OneScreen
{
    namespace
    {
        struct IniLine
        {
            std::string body;   // without the line ending
            std::string eol;    // "\r\n", "\n", or "" for a last line without one
        };

        std::string Lower(std::string s)
        {
            for (char &c : s)
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c - 'A' + 'a');
            return s;
        }

        bool IsSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        std::string Trim(const std::string &s)
        {
            size_t a = 0, b = s.size();
            while (a < b && IsSpace(s[a]))
                a++;
            while (b > a && IsSpace(s[b - 1]))
                b--;
            return s.substr(a, b - a);
        }

        std::vector<IniLine> SplitLines(const std::string &text)
        {
            std::vector<IniLine> lines;
            size_t start = 0;
            while (start < text.size())
            {
                size_t end = text.find('\n', start);
                IniLine line;
                if (end == std::string::npos)
                {
                    line.body = text.substr(start);
                    start = text.size();
                }
                else
                {
                    line.body = text.substr(start, end - start);
                    line.eol = "\n";
                    if (!line.body.empty() && line.body.back() == '\r')
                    {
                        line.body.pop_back();
                        line.eol = "\r\n";
                    }
                    start = end + 1;
                }
                lines.push_back(line);
            }
            return lines;
        }

        // The line as ParseSettings sees it: comment cut, trimmed, no BOM.
        std::string Content(const std::string &body, bool first)
        {
            std::string s = body;
            if (first && s.compare(0, 3, "\xEF\xBB\xBF") == 0)
                s.erase(0, 3);
            const size_t comment = s.find_first_of(";#");
            if (comment != std::string::npos)
                s.erase(comment);
            return Trim(s);
        }

        bool IsHeader(const std::string &content, std::string &name)
        {
            if (content.size() < 2 || content.front() != '[' || content.back() != ']')
                return false;
            name = Lower(Trim(content.substr(1, content.size() - 2)));
            return true;
        }

        bool IsKey(const std::string &content, const std::string &key)
        {
            const size_t eq = content.find('=');
            return eq != std::string::npos && Lower(Trim(content.substr(0, eq))) == key;
        }

        // `body` with the value after '=' replaced; indentation, key spelling,
        // the spacing before '=' and any trailing comment are kept.
        std::string ReplaceValue(const std::string &body, bool first, const std::string &value)
        {
            size_t from = 0;
            if (first && body.compare(0, 3, "\xEF\xBB\xBF") == 0)
                from = 3;
            const size_t eq = body.find('=', from);
            std::string head = body.substr(0, eq);
            while (!head.empty() && (head.back() == ' ' || head.back() == '\t'))
                head.pop_back();
            std::string out = head + " = " + value;

            const size_t comment = body.find_first_of(";#", eq);
            if (comment != std::string::npos)
            {
                size_t gap = comment;
                while (gap > eq + 1 && (body[gap - 1] == ' ' || body[gap - 1] == '\t'))
                    gap--;
                const std::string spacing = body.substr(gap, comment - gap);
                out += (spacing.empty() ? std::string(" ") : spacing) + body.substr(comment);
            }
            return out;
        }
    }

    std::string SetIniValue(const std::string &text, const std::string &section,
                            const std::string &key, const std::string &value)
    {
        const std::string wantSection = Lower(Trim(section));
        const std::string wantKey = Lower(Trim(key));
        const std::string newLine = Trim(key) + " = " + value;
        const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";

        std::vector<IniLine> lines = SplitLines(text);

        bool inSection = wantSection.empty();
        int firstBlockEnd = -1;         // last key line (or header) of the first block
        bool firstBlockOpen = inSection;
        bool sawSection = inSection;

        for (size_t i = 0; i < lines.size(); i++)
        {
            const std::string content = Content(lines[i].body, i == 0);
            if (content.empty())
                continue;

            std::string name;
            if (IsHeader(content, name))
            {
                inSection = name == wantSection;
                if (firstBlockOpen && !inSection)
                    firstBlockOpen = false;
                if (inSection && !sawSection)
                {
                    sawSection = true;
                    firstBlockOpen = true;
                    firstBlockEnd = (int)i;
                }
                continue;
            }
            if (!inSection)
                continue;
            if (IsKey(content, wantKey))
            {
                lines[i].body = ReplaceValue(lines[i].body, i == 0, value);
                std::string out;
                for (const IniLine &l : lines)
                    out += l.body + l.eol;
                return out;
            }
            if (firstBlockOpen)
                firstBlockEnd = (int)i;
        }

        if (sawSection && firstBlockEnd >= 0)
        {
            // Into the first block, after its last key line.
            if (lines[firstBlockEnd].eol.empty())
                lines[firstBlockEnd].eol = eol;
            lines.insert(lines.begin() + firstBlockEnd + 1, IniLine{ newLine, eol });
        }
        else if (sawSection)
        {
            // Keys before any header (empty section name) and none yet: at the
            // top, after a UTF-8 BOM if there is one.
            std::string bom;
            if (!lines.empty() && lines[0].body.compare(0, 3, "\xEF\xBB\xBF") == 0)
            {
                bom = "\xEF\xBB\xBF";
                lines[0].body.erase(0, 3);
            }
            lines.insert(lines.begin(), IniLine{ bom + newLine, eol });
        }
        else
        {
            if (!lines.empty())
            {
                if (lines.back().eol.empty())
                    lines.back().eol = eol;
                if (!Trim(lines.back().body).empty())
                    lines.push_back(IniLine{ "", eol });
            }
            lines.push_back(IniLine{ "[" + Trim(section) + "]", eol });
            lines.push_back(IniLine{ newLine, eol });
        }

        std::string out;
        for (const IniLine &l : lines)
            out += l.body + l.eol;
        return out;
    }

    static const char *DebugNames[] = {
        "xmenu", "keyboard", "pokedex", "subscreens", "bag", "map", "rotom",
        "party", "boxes", "theme", "battlescreen", "panel", "actions", "cursor",
        "buttons", "pcanywhere", "widescreen", "autopanel", "scene", "commands", "framesync",
        "fasttext", "assets", "shot", "uidemo", "fieldstate", "status", "framecallback",
        "battlewidescreen", "quicksave", "photomode", "sidepanels", "gpuquad"
    };
    static_assert(sizeof(DebugNames) / sizeof(DebugNames[0]) == (unsigned)DebugSubsystem::Count,
                  "debug names must match DebugSubsystem");
    static_assert((unsigned)DebugSubsystem::Count < 64, "debug mask must fit uint64_t");

    uint64_t ParseDebugDisabled(const std::string &value, std::vector<std::string> &warnings,
                                const std::string &where)
    {
        uint64_t mask = 0;
        size_t start = 0;
        while (start <= value.size())
        {
            size_t end = value.find(',', start);
            if (end == std::string::npos) end = value.size();
            const std::string name = Lower(Trim(value.substr(start, end - start)));
            if (name == "all")
                mask |= (1ull << (unsigned)DebugSubsystem::Count) - 1;
            else if (!name.empty())
            {
                bool known = false;
                for (unsigned i = 0; i < (unsigned)DebugSubsystem::Count; ++i)
                    if (name == DebugNames[i])
                    {
                        mask |= 1ull << i;
                        known = true;
                        break;
                    }
                if (!known)
                    warnings.push_back(where + "unknown debug subsystem '" + name + "', ignored");
            }
            if (end == value.size()) break;
            start = end + 1;
        }
        return mask;
    }

    std::string DescribeDebugDisabled(uint64_t mask)
    {
        std::string out;
        for (unsigned i = 0; i < (unsigned)DebugSubsystem::Count; ++i)
            if (mask & (1ull << i))
            {
                if (!out.empty()) out += ',';
                out += DebugNames[i];
            }
        return out;
    }

    static std::string Squash(const std::string &s)
    {
        std::string out;
        for (char c : Lower(s))
            if (c != '-' && c != '_' && !IsSpace(c))
                out += c;
        return out;
    }

    bool ParseBattleLayout(const std::string &value, BattleLayout &out)
    {
        const std::string v = Squash(value);
        if (v == "classic")   out = BattleLayout::Classic;
        else if (v == "bar")  out = BattleLayout::Bar;
        else if (v == "rail") out = BattleLayout::Rail;
        else
            return false;
        return true;
    }

    const char *BattleLayoutName(BattleLayout layout)
    {
        switch (layout)
        {
        case BattleLayout::Bar:  return "bar";
        case BattleLayout::Rail: return "rail";
        default:                 return "classic";
        }
    }

    const char *BattleLayoutTitle(BattleLayout layout)
    {
        switch (layout)
        {
        case BattleLayout::Bar:  return "Bar";
        case BattleLayout::Rail: return "Rail";
        default:                 return "Classic";
        }
    }

    BattleLayout NextBattleLayout(BattleLayout layout)
    {
        switch (layout)
        {
        case BattleLayout::Classic: return BattleLayout::Bar;
        case BattleLayout::Bar:     return BattleLayout::Rail;
        default:                    return BattleLayout::Classic;
        }
    }
}
