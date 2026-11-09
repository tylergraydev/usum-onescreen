using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace UsumOneScreen
{
    static class Backups
    {
        public static string Folder(Home home) => Path.Combine(home.Root, "backups", Dump.Title(home.CurrentTitle));
        static bool IsName(string name) => Regex.IsMatch(name, @"^\d{8}-\d{6}$");
        // Reject junctions/symlinks rather than copying/deleting outside this home.
        public static void Safe(Home home, string path)
        {
            string full = Path.GetFullPath(path);
            if (!full.StartsWith(home.Root.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new SetupException("Save path is outside the portable home.");
            for (string p = full; !Home.SamePath(p, home.Root); p = Path.GetDirectoryName(p))
                if ((Directory.Exists(p) || File.Exists(p)) && (File.GetAttributes(p) & FileAttributes.ReparsePoint) != 0)
                    throw new SetupException("Save backups refuse linked files or folders.");
        }
        internal static void CheckTree(Home home, string path)
        {
            Safe(home, path);
            foreach (string file in Directory.GetFiles(path)) Safe(home, file);
            foreach (string dir in Directory.GetDirectories(path)) CheckTree(home, dir);
        }
        internal static void Copy(Home home, string source, string target)
        {
            Safe(home, source); Safe(home, target);
            Directory.CreateDirectory(target);
            foreach (string file in Directory.GetFiles(source))
            {
                Safe(home, file);
                File.Copy(file, Path.Combine(target, Path.GetFileName(file)), true);
            }
            foreach (string dir in Directory.GetDirectories(source)) Copy(home, dir, Path.Combine(target, Path.GetFileName(dir)));
        }
        public static string SavePath(Home home)
        {
            string root = Path.Combine(home.Sdmc, "Nintendo 3DS");
            Safe(home, root);
            if (!Directory.Exists(root)) return null;
            var found = new System.Collections.Generic.List<string>();
            foreach (var id0 in Directory.GetDirectories(root))
            {
                Safe(home, id0);
                foreach (var id1 in Directory.GetDirectories(id0))
                {
                    Safe(home, id1);
                    string data = Path.Combine(id1, "title", "00040000", (home.CurrentTitle & 0xffffffff).ToString("x8"), "data");
                    Safe(home, data);
                    if (Directory.Exists(data)) found.Add(data);
                }
            }
            if (found.Count > 1) throw new SetupException("Multiple save folders found; refusing an ambiguous backup or restore.");
            return found.SingleOrDefault();
        }
        public static string[] List(Home home)
        {
            string folder = Folder(home);
            Safe(home, folder);
            if (!Directory.Exists(folder)) return new string[0];
            return Directory.GetDirectories(folder).Where(p => IsName(Path.GetFileName(p))).OrderByDescending(p => p, StringComparer.Ordinal).ToArray();
        }
        public static long Size(Home home, string path)
        {
            CheckTree(home, path);
            return Directory.GetFiles(path, "*", SearchOption.AllDirectories).Sum(f => new FileInfo(f).Length);
        }
        public static string Create(Home home)
        {
            if (home.IsRunning()) throw new SetupException("Close Azahar before backing up saves.");
            string source = SavePath(home);
            if (source == null) return null;
            CheckTree(home, source);
            if (!Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories).Any()) return null;
            string folder = Folder(home);
            Safe(home, folder);
            Directory.CreateDirectory(folder);
            DateTime now = DateTime.Now;
            DateTime stamp = now.AddTicks(-(now.Ticks % TimeSpan.TicksPerSecond));
            var existing = List(home);
            if (existing.Length > 0 && DateTime.TryParseExact(Path.GetFileName(existing[0]), "yyyyMMdd-HHmmss", System.Globalization.CultureInfo.InvariantCulture,
                System.Globalization.DateTimeStyles.None, out DateTime latest) && latest >= stamp) stamp = latest.AddSeconds(1);
            string name = stamp.ToString("yyyyMMdd-HHmmss", System.Globalization.CultureInfo.InvariantCulture);
            string pending = Path.Combine(folder, "pending-" + Guid.NewGuid().ToString("N"));
            try
            {
                Copy(home, source, Path.Combine(pending, "data"));
                File.WriteAllText(Path.Combine(pending, "source.txt"), source.Substring(home.Sdmc.Length + 1));
                Directory.Move(pending, Path.Combine(folder, name));
            }
            finally { if (Directory.Exists(pending)) Directory.Delete(pending, true); }
            foreach (var old in List(home).Skip(10)) { CheckTree(home, old); Directory.Delete(old, true); }
            return name;
        }
        public static void Restore(Home home, string name)
        {
            if (home.IsRunning()) throw new SetupException("Close Azahar before restoring a save.");
            if (!IsName(name)) throw new SetupException("Use a backup name from backups.");
            string backup = Path.Combine(Folder(home), name);
            Safe(home, backup);
            if (!Directory.Exists(backup)) throw new SetupException("Backup not found.");
            Safe(home, Path.Combine(backup, "source.txt"));
            if (!File.Exists(Path.Combine(backup, "source.txt"))) throw new SetupException("Invalid backup save path.");
            RestoreSnapshot(home, backup);
        }
        internal static void RestoreSnapshot(Home home, string backup, string fallbackTarget = null)
        {
            if (home.IsRunning()) throw new SetupException("Close Azahar before restoring a save.");
            CheckTree(home, backup);
            string sourceFile = Path.Combine(backup, "source.txt");
            string relative = File.Exists(sourceFile) ? File.ReadAllText(sourceFile) : null;
            string target;
            if (relative == null)
                target = SavePath(home) ?? fallbackTarget ?? throw new SetupException("No save destination; start the game once first.");
            else
            {
                string[] parts = relative.Split(Path.DirectorySeparatorChar);
                string low = (home.CurrentTitle & 0xffffffff).ToString("x8");
                if (parts.Length != 7 || parts.Any(p => p.Length == 0 || p.EndsWith(".") || p.EndsWith(" ") || p.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0) || parts[0] != "Nintendo 3DS" ||
                    parts[3] != "title" || parts[4] != "00040000" || parts[5] != low || parts[6] != "data")
                    throw new SetupException("Invalid backup save path.");
                target = SavePath(home) ?? Path.Combine(home.Sdmc, relative);
            }
            Safe(home, target);
            if (Directory.Exists(target)) CheckTree(home, target);
            string staging = target + ".restore-" + Guid.NewGuid().ToString("N"), previous = staging + ".old";
            try
            {
                Copy(home, Path.Combine(backup, "data"), staging);
                Create(home); // Stage first: retention may remove the selected backup.
                if (Directory.Exists(target)) Directory.Move(target, previous);
                try { Directory.Move(staging, target); }
                catch { if (Directory.Exists(previous)) Directory.Move(previous, target); throw; }
                if (Directory.Exists(previous)) Directory.Delete(previous, true);
            }
            finally { if (Directory.Exists(staging)) Directory.Delete(staging, true); }
        }
    }
}
