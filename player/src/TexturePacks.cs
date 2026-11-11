using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.RegularExpressions;

namespace UsumOneScreen
{
    // Verified against azahar-emu/azahar tag 2126.2: custom_tex_manager.cpp,
    // common_paths.h and citra_qt/configuration/config.cpp. See PLAYER-APP.md.
    static class TexturePacks
    {
        public const long MaxBytes = 4L * 1024 * 1024 * 1024; // compressed AND expanded
        public const int MaxEntries = 100000;
        // Azahar 2126.2 template. Live dumping may not create it before the dump directory exists.
        const string NewHashMetadata = @"{
  ""author"": ""citra"",
  ""version"": ""1.0.0"",
  ""description"": ""A graphics pack"",
  ""options"": {
    ""skip_mipmap"": false,
    ""flip_png_files"": true,
    ""use_new_hash"": true
  },
  ""textures"": {}
}";
        static string Title(Home h) => Dump.Title(h.CurrentTitle);
        public static string Library(Home h) => Path.Combine(h.Root, "texture-packs", Title(h));
        public static string LoadFolder(Home h) => Path.Combine(h.User, "load", "textures", Title(h));
        public static string DumpFolder(Home h) => Path.Combine(h.User, "dump", "textures", Title(h));
        static string GameConfig(Home h) => Path.Combine(h.User, "config", "custom", Title(h) + ".ini");
        static string ActiveKey(Home h) => "texture_pack_" + Title(h);
        public static string Active(Home h) => h.GetState(ActiveKey(h));
        public static string Name(string path) => File.ReadAllText(Path.Combine(path, "name.txt"));
        static void Idle(Home h)
        {
            if (h.IsRunning()) throw new SetupException("Close Azahar before changing texture packs or options.");
        }
        public static string[] List(Home h)
        {
            string root = Library(h); Backups.Safe(h, root);
            if (!Directory.Exists(root)) return new string[0];
            var packs = Directory.GetDirectories(root).Where(p => Regex.IsMatch(Path.GetFileName(p), "^[a-f0-9]{32}$")).ToArray();
            foreach (string p in packs) Backups.CheckTree(h, p);
            return packs.OrderBy(Name, StringComparer.OrdinalIgnoreCase).ToArray();
        }
        static string Find(Home h, string name)
        {
            var pack = List(h).SingleOrDefault(p => Name(p).Equals(name, StringComparison.OrdinalIgnoreCase));
            if (pack == null) throw new SetupException("Texture pack not found; use texture-pack list.");
            return pack;
        }
        static bool ReadValue(QtIni ini, string key)
        {
            bool fallback = key == "async_custom_loading";
            if (ini.Get("Utility", key + "\\default") == "true") return fallback;
            string value = ini.Get("Utility", key);
            return value == null ? fallback : value == "true" || value == "1";
        }
        public static bool Get(Home h, string key)
        {
            var game = QtIni.Load(GameConfig(h));
            // Azahar copies per-game values into the global file at boot. A missing
            // overridden key still means its default, never that overwritten global value.
            return ReadValue(game.Get("Utility", key + "\\use_global") == "false"
                ? game : QtIni.Load(h.ConfigPath), key);
        }
        // Per-title overrides prevent Sun/Moon selections from leaking into each other.
        // Also update the global value for Azahar's general settings window.
        public static void Set(Home h, string key, bool value)
        {
            Idle(h);
            if (!new[] { "custom_textures", "preload_textures", "async_custom_loading", "dump_textures" }.Contains(key))
                throw new SetupException("Unknown texture option.");
            string otherTitle = Dump.Title(h.CurrentTitle == Dump.GameTitle ? Dump.MoonGameTitle : Dump.GameTitle);
            string otherConfig = Path.Combine(h.User, "config", "custom", otherTitle + ".ini");
            string[] paths = { otherConfig, h.ConfigPath, GameConfig(h) };
            foreach (string path in paths) Backups.Safe(h, path);
            var original = paths.Select(p => File.Exists(p) ? File.ReadAllBytes(p) : null).ToArray();
            try
            {
                // Preserve the other game's effective value before changing the global default.
                var other = QtIni.Load(otherConfig);
                if (other.Get("Utility", key + "\\use_global") != "false")
                {
                    var global = QtIni.Load(h.ConfigPath);
                    bool previous = ReadValue(global, key);
                    other.Set("Utility", key, previous ? "true" : "false");
                    other.SetPlain("Utility", key + "\\use_global", "false"); other.Save(otherConfig);
                }
                foreach (string path in paths.Skip(1))
                {
                    var ini = QtIni.Load(path);
                    ini.Set("Utility", key, value ? "true" : "false");
                    if (path == GameConfig(h)) ini.SetPlain("Utility", key + "\\use_global", "false");
                    ini.Save(path);
                }
            }
            catch
            {
                for (int i = 0; i < paths.Length; i++)
                    if (original[i] == null) { if (File.Exists(paths[i])) File.Delete(paths[i]); } else File.WriteAllBytes(paths[i], original[i]);
                throw;
            }
        }
        public static bool ParseBool(string value)
        {
            if (value.Equals("on", StringComparison.OrdinalIgnoreCase)) return true;
            if (value.Equals("off", StringComparison.OrdinalIgnoreCase)) return false;
            throw new SetupException("Texture options use on or off.");
        }
        static string ZipPath(string path)
        {
            string normalized = path.Replace('\\', '/');
            string[] parts = normalized.TrimEnd('/').Split('/');
            if (normalized.StartsWith("/") || parts.Any(p => string.IsNullOrEmpty(p) || p == "." || p == ".." ||
                p.EndsWith(".") || p.EndsWith(" ") || p.Any(c => char.IsControl(c) || "<>:\"|?*".Contains(c)) ||
                Regex.IsMatch(p, @"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)", RegexOptions.IgnoreCase)))
                throw new SetupException("Unsafe texture ZIP path.");
            return string.Join("/", parts);
        }
        static bool ImageFile(string path) => new[] { ".png", ".dds", ".ktx" }.Contains(Path.GetExtension(path));
        static void CheckImage(string path)
        {
            byte[] header = new byte[12];
            using (var s = File.OpenRead(path)) if (s.Read(header, 0, header.Length) < 12) throw new SetupException("Invalid texture image header.");
            byte[] signature = Path.GetExtension(path) == ".png" ? new byte[] {137,80,78,71,13,10,26,10} :
                Path.GetExtension(path) == ".dds" ? new byte[] {68,68,83,32} : new byte[] {171,75,84,88,32,49,49,187,13,10,26,10};
            if (!header.Take(signature.Length).SequenceEqual(signature)) throw new SetupException("Invalid texture image header.");
        }
        static void EnsureMetadata(Home h, string data)
        {
            string path = Path.Combine(data, "pack.json"); Backups.Safe(h, path);
            if (!File.Exists(path)) File.WriteAllText(path, NewHashMetadata, new System.Text.UTF8Encoding(false));
        }
        public static string Warning(Home h, string name)
        {
            string path = Path.Combine(Find(h, name), "data", "pack.json");
            if (!File.Exists(path)) return null; // Use supplies new-hash metadata for older library entries.
            try
            {
                // Use the JSON parser shipped with .NET Framework, without a new
                // package, sidecar DLL, or a project-file dependency outside this feature.
                var type = Type.GetType("System.Web.Script.Serialization.JavaScriptSerializer, System.Web.Extensions, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35", true);
                var serializer = Activator.CreateInstance(type);
                type.GetProperty("MaxJsonLength").SetValue(serializer, int.MaxValue, null);
                var root = type.GetMethod("DeserializeObject").Invoke(serializer, new object[] { File.ReadAllText(path) }) as Dictionary<string, object>;
                object options, hash;
                var settings = root != null && root.TryGetValue("options", out options) ? options as Dictionary<string, object> : null;
                if (settings != null && settings.TryGetValue("use_new_hash", out hash) && hash is bool && (bool)hash) return null;
                return "This pack is made from legacy-hash textures (options.use_new_hash is false or missing).";
            }
            catch (System.Reflection.TargetInvocationException)
            {
                return "Cannot read pack.json; check the pack's metadata before use.";
            }
        }
        // Create a library entry without modifying the dump or enabling the pack.
        public static string FromDump(Home h, string name)
        {
            Idle(h); SaveSlots.ValidateName(name);
            if (List(h).Any(p => Name(p).Equals(name, StringComparison.OrdinalIgnoreCase))) throw new SetupException("Texture pack already exists.");
            string source = DumpFolder(h); Backups.Safe(h, source);
            if (!Directory.Exists(source)) throw new SetupException("No dumped textures for this game yet.");
            Backups.CheckTree(h, source);
            string[] files = Directory.GetFiles(source, "*", SearchOption.AllDirectories).Where(ImageFile).ToArray();
            if (files.Length == 0) throw new SetupException("No dumped textures for this game yet.");
            if (files.Length >= MaxEntries) throw new SetupException("Too many dumped texture files.");
            long total = System.Text.Encoding.UTF8.GetByteCount(NewHashMetadata);
            foreach (string file in files)
            {
                ZipPath(file.Substring(source.Length + 1));
                long length = new FileInfo(file).Length;
                if (length > MaxBytes - total) throw new SetupException("Dumped textures exceed the 4 GiB size limit.");
                total += length; CheckImage(file);
            }
            string pending = Path.Combine(Library(h), "pending-" + Guid.NewGuid().ToString("N")); Backups.Safe(h, pending);
            try
            {
                foreach (string file in files)
                {
                    string dest = Path.Combine(pending, "data", file.Substring(source.Length + 1));
                    Backups.Safe(h, file); Backups.Safe(h, dest);
                    Directory.CreateDirectory(Path.GetDirectoryName(dest)); File.Copy(file, dest);
                }
                // Always use dump-compatible options, even if an old pack.json sits in the dump.
                EnsureMetadata(h, Path.Combine(pending, "data"));
                File.WriteAllText(Path.Combine(pending, "name.txt"), name);
                Directory.Move(pending, Path.Combine(Library(h), Guid.NewGuid().ToString("N")));
                return name;
            }
            finally { if (Directory.Exists(pending)) { Backups.CheckTree(h, pending); Directory.Delete(pending, true); } }
        }
        // No textures are bundled. Retain pack.json unchanged (Azahar hash/mipmap options).
        public static string Install(Home h, string zip, string game = null)
        {
            Idle(h);
            if (new FileInfo(zip).Length > MaxBytes) throw new SetupException("Texture ZIP exceeds the 4 GiB size limit.");
            string name = Path.GetFileNameWithoutExtension(zip); SaveSlots.ValidateName(name);
            if (List(h).Any(p => Name(p).Equals(name, StringComparison.OrdinalIgnoreCase))) throw new SetupException("Texture pack already exists.");
            string pending = Path.Combine(Library(h), "pending-" + Guid.NewGuid().ToString("N"));
            Backups.Safe(h, pending);
            try
            {
                using (var archive = ZipFile.OpenRead(zip))
                {
                    if (archive.Entries.Count > MaxEntries) throw new SetupException("Too many texture ZIP entries.");
                    long total = 0;
                    var files = new List<Tuple<ZipArchiveEntry, string>>();
                    var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                    var titles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                    foreach (var entry in archive.Entries)
                    {
                        string path = ZipPath(entry.FullName);
                        if ((entry.ExternalAttributes & 0x400) != 0 || ((entry.ExternalAttributes >> 16) & 0xf000) == 0xa000)
                            throw new SetupException("Texture ZIP refuses linked files.");
                        foreach (string part in path.Split('/').Take(path.Split('/').Length - (entry.Name == "" ? 0 : 1)))
                            if (Regex.IsMatch(part, "^[0-9a-fA-F]{16}$")) titles.Add(part.ToUpperInvariant());
                        if (entry.Length > MaxBytes - total) throw new SetupException("Texture ZIP exceeds the 4 GiB expanded size limit.");
                        total += entry.Length;
                        if (!paths.Add(path)) throw new SetupException("Duplicate texture ZIP path.");
                        if (entry.FullName.EndsWith("/") || entry.FullName.EndsWith("\\")) continue;
                        string leaf = Path.GetFileName(path).ToLowerInvariant();
                        if (!ImageFile(path) && leaf != "pack.json" && leaf != "readme" && leaf != "readme.txt" && leaf != "readme.md")
                            throw new SetupException("Texture ZIP allows only PNG, DDS, KTX, pack.json and readme files.");
                        files.Add(Tuple.Create(entry, path));
                    }
                    if (titles.Count > 1 || (titles.Count == 1 && !titles.Contains(Title(h))))
                        throw new SetupException("Texture ZIP title does not match the selected game (or contains multiple titles).");
                    if (game != null && !game.Equals(Title(h), StringComparison.OrdinalIgnoreCase))
                        throw new SetupException("--game must match the selected game's title ID.");
                    if (titles.Count == 0 && game == null) throw new SetupException("No title ID folder; specify --game " + Title(h) + " after confirming the pack's game.");
                    // Strip wrapper folders through TITLEID. Flat archives retain their relative layout.
                    var images = files.Where(f => ImageFile(f.Item2)).ToArray();
                    if (images.Length == 0) throw new SetupException("Texture ZIP has no images.");
                    if (titles.Count != 0 && images.Any(f => !f.Item2.Split('/').Contains(Title(h), StringComparer.OrdinalIgnoreCase)))
                        throw new SetupException("Texture images must be inside the title ID folder.");
                    string[] firstParts = images[0].Item2.Split('/');
                    int titleIndex = Array.FindIndex(firstParts, part => part.Equals(Title(h), StringComparison.OrdinalIgnoreCase));
                    string prefix = titles.Count == 0 ? "" : string.Join("/", firstParts.Take(titleIndex + 1)) + "/";
                    if (titles.Count != 0 && images.Any(f => !f.Item2.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)))
                        throw new SetupException("Texture images must be inside the title ID folder.");
                    Directory.CreateDirectory(Path.Combine(pending, "data"));
                    foreach (var file in files)
                    {
                        if (!file.Item2.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) continue; // outer readme only
                        string relative = file.Item2.Substring(prefix.Length);
                        if (relative.Length == 0) throw new SetupException("Invalid texture ZIP layout.");
                        string dest = Path.Combine(pending, "data", relative.Replace('/', Path.DirectorySeparatorChar));
                        Backups.Safe(h, dest); Directory.CreateDirectory(Path.GetDirectoryName(dest));
                        using (var input = file.Item1.Open())
                        using (var output = new FileStream(dest, FileMode.CreateNew))
                        {
                            byte[] buffer = new byte[81920]; long copied = 0; int read;
                            while ((read = input.Read(buffer, 0, buffer.Length)) > 0)
                            {
                                if (read > file.Item1.Length - copied) throw new SetupException("Texture ZIP expanded size mismatch.");
                                output.Write(buffer, 0, read); copied += read;
                            }
                            if (copied != file.Item1.Length) throw new SetupException("Texture ZIP expanded size mismatch.");
                        }
                        if (ImageFile(dest)) CheckImage(dest);
                    }
                }
                EnsureMetadata(h, Path.Combine(pending, "data"));
                File.WriteAllText(Path.Combine(pending, "name.txt"), name);
                Directory.Move(pending, Path.Combine(Library(h), Guid.NewGuid().ToString("N")));
                return name;
            }
            catch (InvalidDataException e) { throw new SetupException("Invalid texture ZIP: " + e.Message); }
            finally { if (Directory.Exists(pending)) { Backups.CheckTree(h, pending); Directory.Delete(pending, true); } }
        }
        public static void Use(Home h, string name)
        {
            Idle(h); string pack = Find(h, name), target = LoadFolder(h);
            Backups.Safe(h, target);
            if (Directory.Exists(target))
            {
                Backups.CheckTree(h, target);
                if (Active(h) == null && Directory.EnumerateFileSystemEntries(target).Any())
                    throw new SetupException("Unmanaged texture folder exists; move it aside before enabling a pack.");
            }
            string pending = target + ".pending-" + Guid.NewGuid().ToString("N"), old = target + ".previous-" + Guid.NewGuid().ToString("N");
            Backups.Safe(h, pending); Backups.Safe(h, old);
            // Copies work without admin rights and avoid junction/link traversal.
            bool moved = false, published = false;
            string[] configs = { h.ConfigPath, GameConfig(h), h.StatePath, Path.Combine(h.User, "config", "custom", Dump.Title(h.CurrentTitle == Dump.GameTitle ? Dump.MoonGameTitle : Dump.GameTitle) + ".ini") };
            foreach (string p in configs) Backups.Safe(h, p);
            var original = configs.Select(p => File.Exists(p) ? File.ReadAllBytes(p) : null).ToArray();
            try
            {
                EnsureMetadata(h, Path.Combine(pack, "data"));
                Backups.Copy(h, Path.Combine(pack, "data"), pending);
                if (Directory.Exists(target)) { Directory.Move(target, old); moved = true; }
                Directory.Move(pending, target); published = true;
                Set(h, "custom_textures", true);
                h.SetState(ActiveKey(h), Path.GetFileName(pack));
            }
            catch
            {
                if (published) { Backups.CheckTree(h, target); Directory.Delete(target, true); }
                if (moved) Directory.Move(old, target);
                for (int i = 0; i < configs.Length; i++)
                    if (original[i] == null) { if (File.Exists(configs[i])) File.Delete(configs[i]); } else File.WriteAllBytes(configs[i], original[i]);
                throw;
            }
            finally { if (Directory.Exists(pending)) { Backups.CheckTree(h, pending); Directory.Delete(pending, true); } }
            if (moved) { Backups.CheckTree(h, old); Directory.Delete(old, true); }
        }
        public static void Off(Home h) => Set(h, "custom_textures", false); // retain last materialised pack for makers/hash compatibility
        public static void Remove(Home h, string name)
        {
            Idle(h); string pack = Find(h, name);
            if (Path.GetFileName(pack) == Active(h))
            {
                string target = LoadFolder(h); Backups.Safe(h, target);
                if (Directory.Exists(target)) Backups.CheckTree(h, target);
                Off(h);
                if (Directory.Exists(target)) Directory.Delete(target, true);
                h.SetState(ActiveKey(h), null);
            }
            Backups.CheckTree(h, pack); Directory.Delete(pack, true);
        }
    }
}
