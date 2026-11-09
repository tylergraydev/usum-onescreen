using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;

namespace UsumOneScreen
{
    // Slots use opaque directory IDs; player names never become filesystem paths.
    static class SaveSlots
    {
        public static string Folder(Home home) => Path.Combine(home.Root, "slots", Dump.Title(home.CurrentTitle));
        public static void ValidateName(string name)
        {
            if (string.IsNullOrWhiteSpace(name) || name.Length > 64 || name != name.Trim() ||
                name == "." || name == ".." || name.EndsWith(".") ||
                name.Any(c => char.IsControl(c) || "<>:\"/\\|?*".Contains(c)))
                throw new SetupException("Slot names need 1–64 characters, no path/control characters or leading/trailing spaces or trailing dots.");
        }
        public static string Name(string path) => File.ReadAllText(Path.Combine(path, "name.txt"), Encoding.UTF8);
        public static string[] List(Home home)
        {
            string folder = Folder(home);
            Backups.Safe(home, folder);
            if (!Directory.Exists(folder)) return new string[0];
            var paths = Directory.GetDirectories(folder).Where(p => Regex.IsMatch(Path.GetFileName(p), "^[a-f0-9]{32}$")).ToArray();
            foreach (string path in paths) { Backups.CheckTree(home, path); ValidateName(Name(path)); }
            return paths.OrderBy(Name, StringComparer.OrdinalIgnoreCase).ToArray();
        }
        public static string Find(Home home, string name)
        {
            ValidateName(name);
            var matches = List(home).Where(p => string.Equals(Name(p), name, StringComparison.OrdinalIgnoreCase)).ToArray();
            if (matches.Length != 1) throw new SetupException(matches.Length == 0 ? "Slot not found." : "Ambiguous slot name.");
            return matches[0];
        }
        static void NewName(Home home, string name, string except = null)
        {
            ValidateName(name);
            if (List(home).Any(p => p != except && string.Equals(Name(p), name, StringComparison.OrdinalIgnoreCase)))
                throw new SetupException("A slot with that name already exists for this game.");
        }
        internal static string Pending(Home home)
        {
            string path = Path.Combine(Folder(home), "pending-" + Guid.NewGuid().ToString("N"));
            Backups.Safe(home, path); Directory.CreateDirectory(path); return path;
        }
        internal static void Publish(Home home, string pending, string name)
        {
            NewName(home, name);
            File.WriteAllText(Path.Combine(pending, "name.txt"), name, new UTF8Encoding(false));
            Directory.Move(pending, Path.Combine(Folder(home), Guid.NewGuid().ToString("N")));
        }
        public static void Save(Home home, string name)
        {
            NewName(home, name);
            if (home.IsRunning()) throw new SetupException("Close Azahar before saving a slot.");
            string source = Backups.SavePath(home);
            if (source == null || Backups.Size(home, source) == 0) throw new SetupException("No save yet; cannot create a slot.");
            string pending = Pending(home);
            try
            {
                Backups.Copy(home, source, Path.Combine(pending, "data"));
                File.WriteAllText(Path.Combine(pending, "source.txt"), source.Substring(home.Sdmc.Length + 1));
                Publish(home, pending, name);
            }
            finally { if (Directory.Exists(pending)) Directory.Delete(pending, true); }
        }
        public static void Load(Home home, string name)
        {
            string slot = Find(home, name);
            // Imported slots carry no machine-specific SD identity. Use the current
            // save, or the sole existing portable SD identity; never guess between IDs.
            string target = null;
            if (!File.Exists(Path.Combine(slot, "source.txt")) && Backups.SavePath(home) == null)
            {
                string root = Path.Combine(home.Sdmc, "Nintendo 3DS");
                Backups.Safe(home, root);
                var identities = new List<string>();
                if (Directory.Exists(root))
                    foreach (string id0 in Directory.GetDirectories(root))
                    {
                        Backups.Safe(home, id0);
                        foreach (string id1 in Directory.GetDirectories(id0)) { Backups.Safe(home, id1); identities.Add(id1); }
                    }
                if (identities.Count != 1) throw new SetupException("No unambiguous save destination; start the game once first.");
                target = Path.Combine(identities[0], "title", "00040000", (home.CurrentTitle & 0xffffffff).ToString("x8"), "data");
            }
            Backups.RestoreSnapshot(home, slot, target);
        }
        public static void Rename(Home home, string oldName, string newName)
        {
            string path = Find(home, oldName); NewName(home, newName, path);
            string temp = Path.Combine(path, "name-" + Guid.NewGuid().ToString("N") + ".pending");
            string current = Path.Combine(path, "name.txt"), previous = temp + ".old";
            try
            {
                File.WriteAllText(temp, newName, new UTF8Encoding(false));
                File.Move(current, previous);
                try { File.Move(temp, current); }
                catch { File.Move(previous, current); throw; }
                File.Delete(previous);
            }
            finally { if (File.Exists(temp)) File.Delete(temp); }
        }
        public static void Delete(Home home, string name)
        {
            string path = Find(home, name); Backups.CheckTree(home, path); Directory.Delete(path, true);
        }
    }

    static class SaveArchive
    {
        const long MaxBytes = 256L * 1024 * 1024;
        const int MaxEntries = 4096;
        static readonly Regex Device = new Regex(@"^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)", RegexOptions.IgnoreCase);
        static string[] Parts(string name)
        {
            string[] parts = (name.EndsWith("/") ? name.Substring(0, name.Length - 1) : name).Split('/');
            if (parts.Any(p => p.Length == 0 || p == "." || p == ".." || p.EndsWith(".") || p.EndsWith(" ") ||
                p.Any(c => char.IsControl(c) || "<>:\"\\|?*".Contains(c)) || Device.IsMatch(p)))
                throw new SetupException("Unsafe zip path.");
            return parts;
        }
        static void Extract(ZipArchiveEntry entry, Stream output)
        {
            // Check actual decompressed bytes too, rather than trusting ZIP sizes.
            using (var input = entry.Open())
            {
                var buffer = new byte[81920];
                long copied = 0;
                int count;
                while ((count = input.Read(buffer, 0, buffer.Length)) != 0)
                {
                    copied += count;
                    if (copied > entry.Length) throw new SetupException("Zip entry exceeds its declared size.");
                    output.Write(buffer, 0, count);
                }
                if (copied != entry.Length) throw new SetupException("Truncated zip entry.");
            }
        }
        public static void Export(Home home, string output, string slot = null, string backup = null)
        {
            if (slot != null && backup != null) throw new SetupException("Choose either --slot or --backup.");
            string source, origin;
            if (slot != null) { source = Path.Combine(SaveSlots.Find(home, slot), "data"); origin = "slot: " + slot; }
            else if (backup != null)
            {
                string path = Backups.List(home).SingleOrDefault(p => Path.GetFileName(p) == backup);
                if (path == null) throw new SetupException("Backup not found.");
                source = Path.Combine(path, "data"); origin = "backup: " + backup;
            }
            else
            {
                if (home.IsRunning()) throw new SetupException("Close Azahar before exporting the current save.");
                source = Backups.SavePath(home); origin = "current";
            }
            if (source == null || !Directory.Exists(source) || Backups.Size(home, source) == 0)
                throw new SetupException("No save data to export.");
            string manifestText = "format=1\ntitle_id=" + Dump.Title(home.CurrentTitle) + "\ndate=" + DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture) +
                "\nsource=" + origin + "\nlauncher_version=" + Assembly.GetExecutingAssembly().GetName().Version + "\n";
            var files = Directory.GetFiles(source, "*", SearchOption.AllDirectories);
            if (files.Length + 1 > MaxEntries || files.Sum(f => new FileInfo(f).Length) + Encoding.UTF8.GetByteCount(manifestText) > MaxBytes)
                throw new SetupException("Save exceeds archive limits (256 MiB / 4096 entries).");
            foreach (string file in files) Parts("data/" + file.Substring(source.Length + 1).Replace('\\', '/'));
            output = Path.GetFullPath(output);
            foreach (string protectedFolder in new[] { home.AzaharDir, Path.Combine(home.Root, "slots"), Path.Combine(home.Root, "backups") })
                if (Home.SamePath(output, protectedFolder) || output.StartsWith(protectedFolder + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                    throw new SetupException("Export outside the emulator, slots and backups folders.");
            if (File.Exists(output) || Directory.Exists(output)) throw new SetupException("Export file already exists; choose a new file name.");
            string temp = output + ".pending-" + Guid.NewGuid().ToString("N");
            try
            {
                using (var zip = ZipFile.Open(temp, ZipArchiveMode.Create))
                {
                    using (var writer = new StreamWriter(zip.CreateEntry("manifest.txt").Open(), new UTF8Encoding(false)))
                        writer.Write(manifestText);
                    foreach (string file in files)
                        zip.CreateEntryFromFile(file, "data/" + file.Substring(source.Length + 1).Replace('\\', '/'), CompressionLevel.Optimal);
                }
                File.Move(temp, output);
            }
            finally { if (File.Exists(temp)) File.Delete(temp); }
        }
        public static string Import(Home home, string input, string name = null)
        {
            name = name ?? Path.GetFileNameWithoutExtension(input);
            SaveSlots.ValidateName(name);
            // Resolve duplicates before doing any extraction.
            if (SaveSlots.List(home).Any(p => string.Equals(SaveSlots.Name(p), name, StringComparison.OrdinalIgnoreCase)))
                throw new SetupException("A slot with that name already exists for this game.");
            string pending = null;
            try
            {
                using (var zip = ZipFile.OpenRead(input))
                {
                    if (zip.Entries.Count > MaxEntries) throw new SetupException("Too many zip entries.");
                    long bytes = 0;
                    var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                    foreach (var entry in zip.Entries)
                    {
                        string[] parts = Parts(entry.FullName);
                        string key = string.Join("/", parts);
                        if (!seen.Add(key) || ((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000 || (entry.ExternalAttributes & 0x400) != 0)
                            throw new SetupException("Duplicate or linked zip entry.");
                        bool dir = entry.FullName.EndsWith("/");
                        if (dir && entry.Length != 0) throw new SetupException("Invalid zip directory entry.");
                        if (key != "manifest.txt" && (parts[0] != "data" || (!dir && parts.Length < 2)))
                            throw new SetupException("Archive may contain only manifest.txt and data/ save files.");
                        if (entry.Length > MaxBytes - bytes) throw new SetupException("Archive exceeds 256 MiB.");
                        bytes += entry.Length;
                    }
                    var manifest = zip.GetEntry("manifest.txt");
                    if (manifest == null || manifest.Length > 4096) throw new SetupException("Missing or invalid save manifest.");
                    var fields = new Dictionary<string, string>(StringComparer.Ordinal);
                    using (var contents = new MemoryStream())
                    {
                        Extract(manifest, contents);
                        contents.Position = 0;
                        using (var reader = new StreamReader(contents, Encoding.UTF8))
                        {
                            string line;
                            while ((line = reader.ReadLine()) != null)
                            {
                                int eq = line.IndexOf('=');
                                if (eq < 1 || fields.ContainsKey(line.Substring(0, eq))) throw new SetupException("Invalid save manifest.");
                                fields.Add(line.Substring(0, eq), line.Substring(eq + 1));
                            }
                        }
                    }
                    if (!fields.TryGetValue("format", out var format) || format != "1") throw new SetupException("Unsupported save archive format.");
                    if (!fields.TryGetValue("title_id", out var title) || !string.Equals(title, Dump.Title(home.CurrentTitle), StringComparison.OrdinalIgnoreCase))
                        throw new SetupException("Archive title id does not match the selected game.");
                    if (!fields.TryGetValue("date", out var date) || !DateTimeOffset.TryParse(date, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out _) ||
                        !fields.TryGetValue("source", out var origin) || string.IsNullOrWhiteSpace(origin) ||
                        !fields.TryGetValue("launcher_version", out var version) || string.IsNullOrWhiteSpace(version))
                        throw new SetupException("Incomplete save manifest.");
                    var files = zip.Entries.Where(e => e.FullName.StartsWith("data/", StringComparison.Ordinal) && !e.FullName.EndsWith("/")).ToArray();
                    if (files.Length == 0 || files.Sum(e => e.Length) == 0) throw new SetupException("Archive has no save data.");
                    pending = SaveSlots.Pending(home);
                    foreach (var entry in files)
                    {
                        string target = Path.Combine(pending, entry.FullName.Replace('/', Path.DirectorySeparatorChar));
                        Backups.Safe(home, target);
                        Directory.CreateDirectory(Path.GetDirectoryName(target));
                        using (var output = new FileStream(target, FileMode.CreateNew, FileAccess.Write)) Extract(entry, output);
                    }
                    SaveSlots.Publish(home, pending, name);
                }
                return name;
            }
            catch (InvalidDataException e) { throw new SetupException("Invalid save zip: " + e.Message); }
            finally { if (pending != null && Directory.Exists(pending)) Directory.Delete(pending, true); }
        }
    }
}
