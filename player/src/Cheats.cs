using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace UsumOneScreen
{
    static class Cheats
    {
        public sealed class Entry
        {
            public string Name;
            public string[] Lines;
            public bool Enabled;
            public override string ToString() => Name;
        }
        public static string FilePath(Home home) => Path.Combine(home.User, "cheats", Dump.Title(home.CurrentTitle) + ".txt");
        public static List<Entry> Load(Home home)
        {
            string text;
            using (var stream = typeof(Cheats).Assembly.GetManifestResourceStream("UsumOneScreen.Cheats." + Dump.Title(home.CurrentTitle) + ".txt"))
            using (var reader = new StreamReader(stream)) text = reader.ReadToEnd();
            var entries = Parse(text);
            if (File.Exists(FilePath(home)))
            {
                var existing = Parse(File.ReadAllText(FilePath(home)));
                foreach (var entry in entries) entry.Enabled = existing.Any(e => e.Name == entry.Name && e.Enabled);
            }
            // Never propagate a conflicting pair from a manually edited file.
            var shiny = entries.Where(e => e.Name.StartsWith("Always shiny", StringComparison.OrdinalIgnoreCase)).ToList();
            if (shiny.Count(e => e.Enabled) > 1) foreach (var e in shiny) e.Enabled = false;
            return entries;
        }
        static List<Entry> Parse(string text)
        {
            var entries = new List<Entry>();
            Entry entry = null;
            var lines = new List<string>();
            foreach (string line in text.Replace("\r\n", "\n").Split('\n'))
            {
                string trimmed = line.Trim();
                if (trimmed.StartsWith("[") && trimmed.EndsWith("]"))
                {
                    if (entry != null) entry.Lines = lines.ToArray();
                    entry = new Entry { Name = trimmed.Substring(1, trimmed.Length - 2) };
                    entries.Add(entry); lines.Clear();
                }
                else if (entry != null)
                {
                    if (trimmed == "*citra_enabled") entry.Enabled = true;
                    else lines.Add(line);
                }
            }
            if (entry != null) entry.Lines = lines.ToArray();
            return entries;
        }
        public static void Enable(List<Entry> entries, Entry entry, bool enabled)
        {
            if (enabled && entry.Name.StartsWith("Always shiny", StringComparison.OrdinalIgnoreCase))
                foreach (var other in entries.Where(e => e != entry && e.Name.StartsWith("Always shiny", StringComparison.OrdinalIgnoreCase))) other.Enabled = false;
            entry.Enabled = enabled;
        }
        public static void Save(Home home, List<Entry> entries)
        {
            if (home.IsRunning()) throw new SetupException("Close Azahar before changing cheats.");
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath(home)));
            var text = new StringBuilder();
            foreach (var entry in entries)
            {
                text.AppendLine("[" + entry.Name + "]");
                if (entry.Enabled) text.AppendLine("*citra_enabled");
                foreach (var line in entry.Lines) text.AppendLine(line);
            }
            File.WriteAllText(FilePath(home), text.ToString(), new UTF8Encoding(false));
        }
        public static void Sync(Home home) => Save(home, Load(home));
    }
}
