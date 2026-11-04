using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace UsumOneScreen
{
    // The app's folder: a portable Azahar copy in azahar\ (portable because
    // azahar\user\ exists; Azahar has no user-dir flag, see tools/instance.py)
    // and the app's own state file. The player's Azahar install is only read,
    // and %APPDATA%\Azahar is never used.
    sealed class Home
    {
        public const string TestedAzahar = "2126.2";
        const string PluginName = "usum-onescreen.3gx";

        public readonly string Root;
        public Home(string root) { Root = Path.GetFullPath(root); }

        public static Home Default() => new Home(AppDomain.CurrentDomain.BaseDirectory);

        public string AzaharDir => Path.Combine(Root, "azahar");
        public string Exe => Path.Combine(AzaharDir, "azahar.exe");
        public string User => Path.Combine(AzaharDir, "user");
        public string Sdmc => Path.Combine(User, "sdmc");
        public string ConfigPath => Path.Combine(User, "config", "qt-config.ini");
        // Older launcher state files only supported Sun.
        public ulong CurrentTitle => GetState("title_id") == Dump.Title(Dump.MoonGameTitle) ? Dump.MoonGameTitle : Dump.GameTitle;
        public string GameName => Dump.GameName(CurrentTitle);
        public string PluginPath => PluginPathFor(CurrentTitle);
        string PluginPathFor(ulong title) => Path.Combine(Sdmc, "luma", "plugins", Dump.Title(title), PluginName);
        public string PluginIniPath => Path.Combine(Sdmc, "usum-onescreen.ini");
        public string PluginLogPath => Path.Combine(Sdmc, "usum-onescreen.log");
        public string StatePath => Path.Combine(Root, "usum-onescreen-app.ini");

        // --- the app's own state: key=value lines ---

        public Dictionary<string, string> LoadState()
        {
            var state = new Dictionary<string, string>();
            if (File.Exists(StatePath))
                foreach (var line in File.ReadAllLines(StatePath))
                {
                    int eq = line.IndexOf('=');
                    if (eq > 0)
                        state[line.Substring(0, eq).Trim()] = line.Substring(eq + 1).Trim();
                }
            return state;
        }

        public void SaveState(Dictionary<string, string> state)
        {
            Directory.CreateDirectory(Root);
            File.WriteAllLines(StatePath, state.Select(kv => $"{kv.Key}={kv.Value}"), new UTF8Encoding(false));
        }

        public string GetState(string key, string fallback = null) =>
            LoadState().TryGetValue(key, out var v) && v != "" ? v : fallback;

        public void SetState(string key, string value)
        {
            var state = LoadState();
            state[key] = value ?? "";
            SaveState(state);
        }

        // --- Azahar ---

        public bool HasAzahar => File.Exists(Exe) && Directory.Exists(User);

        public static string BundledAzahar()
        {
            string dir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "azahar-bundled");
            return File.Exists(Path.Combine(dir, "azahar.exe")) ? dir : null;
        }

        // Azahar installs the app could copy, best first.
        public IEnumerable<string> AzaharCandidates()
        {
            var dirs = new List<string>();
            string saved = GetState("azahar_source");
            if (saved != null) dirs.Add(saved);
            dirs.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Azahar"));
            dirs.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "Azahar"));
            return dirs.Where(d => File.Exists(Path.Combine(d, "azahar.exe")) && !SamePath(d, AzaharDir)).Distinct();
        }

        // The build version Azahar stores twice in a row next to its compiler
        // name (src/common/scm_rev.cpp.in); null when it can't be found.
        public static string AzaharVersion(string dir)
        {
            try
            {
                var text = Encoding.ASCII.GetString(File.ReadAllBytes(Path.Combine(dir, "azahar.exe")));
                var m = Regex.Match(text, @"\0(\d{4}(?:\.\d+)*(?:-[0-9A-Za-z.]+)?)\0+\1\0+(MSVC|MinGW|GCC|Clang|clang)\0");
                return m.Success ? m.Groups[1].Value : null;
            }
            catch (IOException) { return null; }
        }

        public void InstallAzahar(string source, Action<string> log)
        {
            if (!File.Exists(Path.Combine(source, "azahar.exe")))
                throw new SetupException($"No azahar.exe in {source}.");
            if (SamePath(source, AzaharDir))
                throw new SetupException("That is the app's own Azahar copy; pick your Azahar install.");
            if (IsRunning())
                throw new SetupException("Azahar is running from the app's folder; close it first.");
            log($"Copying Azahar from {source} …");
            string src = Path.GetFullPath(source).TrimEnd('\\');
            foreach (var file in Directory.EnumerateFiles(src, "*", SearchOption.AllDirectories))
            {
                string rel = file.Substring(src.Length + 1);
                // A portable source's own user folder and the uninstaller stay behind.
                if (rel.StartsWith("user\\", StringComparison.OrdinalIgnoreCase) ||
                    rel.Equals("uninst.exe", StringComparison.OrdinalIgnoreCase))
                    continue;
                string dst = Path.Combine(AzaharDir, rel);
                Directory.CreateDirectory(Path.GetDirectoryName(dst));
                File.Copy(file, dst, true);
            }
            Directory.CreateDirectory(User);
            SetState("azahar_source", src);
            string version = AzaharVersion(AzaharDir);
            log(version == null ? "Azahar copied (version not recognised)."
                                : $"Azahar {version} copied.");
            if (version != TestedAzahar)
                log($"Note: USUM One Screen is tested with Azahar {TestedAzahar}.");
        }

        // Plugin loader on, top screen only, custom textures off, no update
        // check, no bars around the game, and the chosen controls.
        public void WriteConfig(ControlPreset controls)
        {
            var ini = QtIni.Load(ConfigPath);
            foreach (var (section, key, value) in new[]
            {
                ("System", "plugin_loader", "true"),
                ("System", "allow_plugin_loader", "true"),
                ("Layout", "layout_option", "1"),               // LayoutOption::SingleScreen
                ("Layout", "swap_screen", "false"),             // the single screen is the top one
                ("Utility", "custom_textures", "false"),        // an HD pack crashed boot (PLAYING.md)
                ("Utility", "preload_textures", "false"),
                ("Data%20Storage", "use_custom_storage", "false"),
                ("Miscellaneous", "check_for_update_on_start", "false"),
                ("UI", "showStatusBar", "false"),
                ("UI", "showFilterBar", "false"),
                ("UI", "confirmClose", "false"),
                ("UI", "firstStart", "false"),
            })
                ini.Set(section, key, value);
            ControlPresets.Apply(ini, controls);
            ini.Save(ConfigPath);
            SetState("controls", controls.ToString());

            // A per-game file could turn custom textures or another layout back on.
            string custom = Path.Combine(User, "config", "custom");
            if (Directory.Exists(custom))
                foreach (var game in Directory.GetFiles(custom, "*.ini"))
                {
                    var g = QtIni.Load(game);
                    g.Remove("Utility", k => k == "custom_textures" || k == "preload_textures");
                    g.Remove("Layout", k => k == "layout_option" || k == "swap_screen");
                    g.Save(game);
                }
        }

        public ControlPreset CurrentControls() =>
            Enum.TryParse(GetState("controls"), out ControlPreset p) ? p : ControlPreset.Keyboard;

        public static string BundledPlugin() =>
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, PluginName);

        public void InstallPlugin(string plugin, ulong title, Action<string> log)
        {
            if (!File.Exists(plugin))
                throw new SetupException($"The plugin {PluginName} is missing; it should be next to the app ({plugin}).");
            string destination = PluginPathFor(title);
            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            File.Copy(plugin, destination, true);
            log($"{Dump.GameName(title)} plugin installed.");
        }

        // --- titles installed in the portable SD card ---

        string ContentDir(ulong title)
        {
            string high = (title >> 32).ToString("x8"), low = (title & 0xFFFFFFFF).ToString("x8");
            string n3ds = Path.Combine(Sdmc, "Nintendo 3DS");
            if (!Directory.Exists(n3ds))
                return null;
            foreach (var id0 in Directory.GetDirectories(n3ds))
                foreach (var id1 in Directory.GetDirectories(id0))
                {
                    string dir = Path.Combine(id1, "title", high, low, "content");
                    if (Directory.Exists(dir))
                        return dir;
                }
            return null;
        }

        Dump.Tmd InstalledTmd(ulong title, out string dir)
        {
            dir = ContentDir(title);
            if (dir == null)
                return null;
            return Directory.GetFiles(dir, "*.tmd").Select(Dump.ReadTmdFile).FirstOrDefault(t => t != null);
        }

        public int InstalledUpdateVersion(ulong? title = null) => InstalledTmd(Dump.UpdateFor(title ?? CurrentTitle), out _)?.Version ?? -1;

        // The boot file of a game installed from a CIA, or null.
        public string InstalledGame(ulong title)
        {
            var tmd = InstalledTmd(title, out var dir);
            if (tmd == null)
                return null;
            string app = Path.Combine(dir, tmd.Content0Id.ToString("x8") + ".app");
            return File.Exists(app) ? app : null;
        }

        // Installs a CIA with `azahar.exe -i`. On Windows Azahar reports the
        // result in a message box titled "Success" or "Failure" and exits when
        // it closes, with 0 or InstallStatus + 2 (src/citra_qt/citra_qt.cpp).
        public void InstallCia(string cia, Action<string> log)
        {
            if (IsRunning())
                throw new SetupException("Azahar is running from the app's folder; close it first.");
            log($"Installing {Path.GetFileName(cia)} …");
            var psi = Detached(new ProcessStartInfo(Exe, "-i " + Quote(cia)));
            using (var p = Process.Start(psi))
            {
                var deadline = DateTime.UtcNow.AddMinutes(30);
                string title = null;
                while (!p.WaitForExit(250))
                {
                    if (DateTime.UtcNow > deadline)
                    {
                        p.Kill();
                        throw new SetupException("Installing took more than 30 minutes; stopped.");
                    }
                    string t = Win32.CloseWindowTitled(p.Id, "Success", "Failure");
                    if (t != null)
                        title = t;
                }
                int code = p.ExitCode;
                if (code != 0)
                {
                    string[] reasons = { "", "", "", "Azahar couldn't open the file.", "File not found.",
                                         "The install was aborted.", "Azahar says the CIA is invalid.",
                                         "Azahar says the CIA is encrypted." };
                    string why = code > 0 && code < reasons.Length ? reasons[code] : $"Azahar exited with code {code}.";
                    throw new SetupException($"Installing {Path.GetFileName(cia)} failed. {why}");
                }
                if (title == "Failure")
                    throw new SetupException($"Installing {Path.GetFileName(cia)} failed.");
            }
            log("Installed.");
        }

        // --- running ---

        public bool IsRunning() => RunningPid() != 0;

        public int RunningPid()
        {
            foreach (var p in Process.GetProcessesByName("azahar"))
                using (p)
                {
                    string path = Win32.ProcessPath(p.Id);
                    if (path != null && SamePath(path, Exe))
                        return p.Id;
                }
            return 0;
        }

        public Process Launch(string game, bool fullscreen)
        {
            if (IsRunning())
                throw new SetupException("USUM One Screen is already running.");
            // Again on every start: the player may have changed these in
            // Azahar's own windows (or a test turned the loader off).
            WriteConfig(CurrentControls());
            var log = PluginLogPath;
            if (File.Exists(log))
                File.Delete(log);
            string args = (fullscreen ? "-f " : "-w ") + Quote(game);
            return Process.Start(Detached(new ProcessStartInfo(Exe, args)));
        }

        // Starts Azahar with no game, so the player can use its own windows
        // (Emulation > Configure > Controls).
        public Process OpenAzahar() =>
            Process.Start(Detached(new ProcessStartInfo(Exe)));

        // Shell execute, so Azahar doesn't inherit our stdout/stderr: a caller
        // reading our output (a script, the test queue) would otherwise wait
        // until the game closes.
        ProcessStartInfo Detached(ProcessStartInfo psi)
        {
            psi.WorkingDirectory = Root;
            psi.UseShellExecute = true;
            return psi;
        }

        // The plugin's boot line, once it has written one: the game and
        // version it found, or why it did nothing.
        public string PluginBootLine(int timeoutMs, Func<bool> stillRunning)
        {
            var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (DateTime.UtcNow < deadline && stillRunning())
            {
                if (File.Exists(PluginLogPath))
                {
                    string text;
                    try { text = File.ReadAllText(PluginLogPath); }
                    catch (IOException) { text = ""; }
                    if (text.Contains("hooks:") || text.Contains("unsupported game"))
                        return text;
                }
                Thread.Sleep(500);
            }
            return null;
        }

        static string Quote(string s) => "\"" + s.TrimEnd('\\') + "\"";

        public static bool SamePath(string a, string b) =>
            string.Equals(Path.GetFullPath(a).TrimEnd('\\'), Path.GetFullPath(b).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase);
    }

    sealed class SetupException : Exception
    {
        public SetupException(string message) : base(message) { }
    }
}
