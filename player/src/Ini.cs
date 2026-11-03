using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace UsumOneScreen
{
    // Azahar's qt-config.ini (QSettings format). Azahar reads a value only when
    // `key\default=false` sits next to it, so Set writes both, like
    // tools/instance.py's set_ini. Everything else in the file is kept.
    sealed class QtIni
    {
        readonly List<string> lines;

        QtIni(List<string> lines) { this.lines = lines; }

        public static QtIni Load(string path) =>
            new QtIni(File.Exists(path) ? File.ReadAllLines(path).ToList() : new List<string>());

        public void Save(string path)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            File.WriteAllText(path, string.Join("\n", lines).TrimEnd('\n') + "\n", new UTF8Encoding(false));
        }

        (int start, int end) Section(string section, bool create)
        {
            int start = lines.IndexOf($"[{section}]");
            if (start < 0)
            {
                if (!create)
                    return (-1, -1);
                if (lines.Count > 0 && lines[lines.Count - 1] != "")
                    lines.Add("");
                lines.Add($"[{section}]");
                start = lines.Count - 1;
            }
            int end = start + 1;
            while (end < lines.Count && !lines[end].StartsWith("["))
                end++;
            return (start, end);
        }

        public string Get(string section, string key)
        {
            var (start, end) = Section(section, false);
            for (int i = start + 1; start >= 0 && i < end; i++)
                if (lines[i].StartsWith(key + "="))
                    return lines[i].Substring(key.Length + 1);
            return null;
        }

        public void Set(string section, string key, string value)
        {
            Remove(section, k => k == key);
            var (_, end) = Section(section, true);
            while (end > 0 && lines[end - 1] == "")
                end--;
            lines.InsertRange(end, new[] { $"{key}\\default=false", $"{key}={value}" });
        }

        // A key Azahar reads without a \default flag (QSettings array sizes).
        public void SetPlain(string section, string key, string value)
        {
            Remove(section, k => k == key);
            var (_, end) = Section(section, true);
            while (end > 0 && lines[end - 1] == "")
                end--;
            lines.Insert(end, $"{key}={value}");
        }

        // Removes every key in [section] whose name (without \default) matches.
        public void Remove(string section, Func<string, bool> match)
        {
            var (start, end) = Section(section, false);
            if (start < 0)
                return;
            for (int i = end - 1; i > start; i--)
            {
                int eq = lines[i].IndexOf('=');
                if (eq <= 0)
                    continue;
                string key = lines[i].Substring(0, eq);
                if (key.EndsWith("\\default"))
                    key = key.Substring(0, key.Length - "\\default".Length);
                if (match(key))
                    lines.RemoveAt(i);
            }
        }
    }

    // The plugin's usum-onescreen.ini. Values are changed in place so the
    // comments the plugin wrote stay; a missing key goes at the end of its
    // section. The plugin's parser (Sources/Settings.cpp) treats ; and # as
    // comments anywhere, and keys are unique across sections.
    sealed class PluginIni
    {
        public static readonly string[] Keys =
            { "panel_key", "touch_key", "map_key", "cursor_move", "cursor_speed", "size", "corner", "auto" };

        public static string SectionOf(string key) =>
            key == "size" || key == "corner" || key == "auto" ? "panel" : "controls";

        // The plugin's own defaults (Includes/Settings.hpp), used when the file
        // doesn't exist yet; the plugin writes its full commented file on first boot.
        public static readonly Dictionary<string, string> Defaults = new Dictionary<string, string>
        {
            ["panel_key"] = "L+R", ["touch_key"] = "ZL", ["map_key"] = "ZR", ["cursor_move"] = "cstick",
            ["cursor_speed"] = "6", ["size"] = "1/2", ["corner"] = "bottom-right", ["auto"] = "off",
        };

        readonly List<string> lines;

        PluginIni(List<string> lines) { this.lines = lines; }

        public static PluginIni Load(string path)
        {
            if (!File.Exists(path))
                return new PluginIni(new List<string>
                {
                    "; USUM One Screen settings, written by the USUM One Screen app.",
                    "; Delete this file to get the defaults back; the plugin then writes its own",
                    "; commented version with every setting explained.",
                });
            string text = File.ReadAllText(path);
            if (text.Length > 0 && text[0] == '﻿')    // StartsWith("﻿") is true for any string
                text = text.Substring(1);
            return new PluginIni(text.Replace("\r\n", "\n").Split('\n').ToList());
        }

        public void Save(string path)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            File.WriteAllText(path, string.Join("\n", lines).TrimEnd('\n') + "\n", new UTF8Encoding(false));
        }

        static string Strip(string line)
        {
            int c = line.IndexOfAny(new[] { ';', '#' });
            return (c >= 0 ? line.Substring(0, c) : line).Trim();
        }

        int Find(string key)
        {
            for (int i = 0; i < lines.Count; i++)
            {
                string s = Strip(lines[i]);
                int eq = s.IndexOf('=');
                if (eq > 0 && string.Equals(s.Substring(0, eq).Trim(), key, StringComparison.OrdinalIgnoreCase))
                    return i;
            }
            return -1;
        }

        public string Get(string key)
        {
            int i = Find(key);
            if (i < 0)
                return Defaults.TryGetValue(key, out var d) ? d : null;
            string s = Strip(lines[i]);
            return s.Substring(s.IndexOf('=') + 1).Trim();
        }

        public void Set(string key, string value)
        {
            int i = Find(key);
            if (i >= 0)
            {
                // Keep the key's spacing and any comment after the value.
                string line = lines[i];
                int eq = line.IndexOf('=');
                int c = line.IndexOfAny(new[] { ';', '#' }, eq);
                string comment = c >= 0 ? line.Substring(c) : "";
                string pad = c >= 0 ? new string(' ', Math.Max(1, c - eq - 2 - value.Length)) : "";
                lines[i] = line.Substring(0, eq + 1) + " " + value + (c >= 0 ? pad + comment : "");
                return;
            }
            string section = "[" + SectionOf(key) + "]";
            int start = lines.FindIndex(l => string.Equals(Strip(l), section, StringComparison.OrdinalIgnoreCase));
            if (start < 0)
            {
                if (lines.Count > 0 && lines[lines.Count - 1].Trim() != "")
                    lines.Add("");
                lines.Add(section);
                lines.Add($"{key} = {value}");
                return;
            }
            int end = start + 1;
            while (end < lines.Count && !Strip(lines[end]).StartsWith("["))
                end++;
            while (end > start + 1 && lines[end - 1].Trim() == "")
                end--;
            lines.Insert(end, $"{key} = {value}");
        }
    }
}
