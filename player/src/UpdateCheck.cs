using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace UsumOneScreen
{
    // All network work is bounded and runs off the UI thread. Only UI continuations write settings.
    static class UpdateCheck
    {
        public const string Endpoint = "https://api.github.com/repos/tylergraydev/usum-onescreen/releases/latest";
        public static string Version => typeof(UpdateCheck).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>().InformationalVersion.Split('+')[0];
        public sealed class Asset { public string name { get; set; } public string browser_download_url { get; set; } public long size { get; set; } }
        public sealed class Release { public string tag_name { get; set; } public bool draft { get; set; } public bool prerelease { get; set; } public Asset[] assets { get; set; } }
        public sealed class Result { public Release Release; public string Message; }

        static string[] Parts(string version)
        {
            var m = Regex.Match(version ?? "", @"^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$");
            return m.Success ? new[] { m.Groups[1].Value, m.Groups[2].Value, m.Groups[3].Value } : null;
        }
        public static bool Newer(string candidate, string current)
        {
            var a = Parts(candidate); var b = Parts(current);
            if (a == null || b == null) return false; // prerelease tags are deliberately excluded
            for (int i = 0; i < 3; i++)
            {
                int c = a[i].Length.CompareTo(b[i].Length);
                if (c == 0) c = string.CompareOrdinal(a[i], b[i]);
                if (c != 0) return c > 0;
            }
            return false;
        }
        static string Canonical(string version) => Parts(version) is string[] p ? string.Join(".", p) : version;
        public static bool Due(string last, DateTime now) =>
            !DateTime.TryParse(last, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var then) ||
            now.ToUniversalTime() - then.ToUniversalTime() >= TimeSpan.FromDays(1);

        public static async Task<Result> Check(Home home, bool manual, DateTime now, Func<string, byte[]> fetch = null)
        {
            try
            {
                if (!manual && !Due(home.GetState("update_last_check"), now)) return new Result { Message = "Already checked today." };
                home.SetState("update_last_check", now.ToUniversalTime().ToString("o", CultureInfo.InvariantCulture));
                string skipped = home.GetState("update_skip_version");
                var release = await Task.Run(() => new JavaScriptSerializer().Deserialize<Release>(
                    Encoding.UTF8.GetString((fetch ?? Fetch)(Endpoint))));
                if (release == null || release.draft || release.prerelease || !Newer(release.tag_name, Version))
                    return new Result { Message = "No newer stable release available." };
                if (!manual && Canonical(skipped) == Canonical(release.tag_name)) return new Result { Message = "Version skipped." };
                return new Result { Release = release, Message = "Update available: " + release.tag_name };
            }
            catch (Exception) { return new Result { Message = "Couldn't check for updates." }; }
        }

        // Same WebRequest / TLS 1.2 setup as AzaharDownload, with an anonymous User-Agent.
        public static byte[] Fetch(string url)
        {
            if (!Uri.TryCreate(url, UriKind.Absolute, out var uri) || uri.Scheme != "https") throw new InvalidDataException("Updates require HTTPS.");
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            var request = (HttpWebRequest)WebRequest.Create(uri);
            request.UserAgent = "UsumOneScreen-Launcher/" + Version;
            request.Accept = "application/vnd.github+json";
            request.Timeout = request.ReadWriteTimeout = 30000;
            using (var response = request.GetResponse())
            using (var input = response.GetResponseStream())
            using (var output = new MemoryStream())
            {
                if (response.ResponseUri.Scheme != "https") throw new InvalidDataException("Updates require HTTPS.");
                var buffer = new byte[81920]; int read;
                while ((read = input.Read(buffer, 0, buffer.Length)) > 0)
                {
                    if (output.Length + read > 256L * 1024 * 1024) throw new InvalidDataException("Update asset too large.");
                    output.Write(buffer, 0, read);
                }
                return output.ToArray();
            }
        }
        static string Hash(byte[] bytes)
        {
            using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
        }
        static byte[] Download(Asset asset, Func<string, byte[]> fetch)
        {
            if (asset.size <= 0 || asset.size > 256L * 1024 * 1024) throw new InvalidDataException("Invalid release asset size.");
            var bytes = fetch(asset.browser_download_url);
            if (bytes.LongLength != asset.size) throw new InvalidDataException("Update size mismatch.");
            return bytes;
        }
        // Also requires a managed launcher with its embedded plugin, beyond the PE header check.
        public static byte[] Plugin(byte[] exe)
        {
            if (exe.Length < 64 || exe[0] != 'M' || exe[1] != 'Z') throw new InvalidDataException("Invalid launcher PE.");
            int pe = BitConverter.ToInt32(exe, 60);
            if (pe < 64 || pe > exe.Length - 24 || BitConverter.ToUInt32(exe, pe) != 0x4550) throw new InvalidDataException("Invalid launcher PE.");
            var assembly = Assembly.Load(exe);
            if (assembly.GetName().Name != "UsumOneScreen") throw new InvalidDataException("Unexpected launcher assembly.");
            using (var resource = assembly.GetManifestResourceStream("UsumOneScreen.Plugin"))
            using (var output = new MemoryStream())
            {
                if (resource == null || resource.Length == 0) throw new InvalidDataException("Release launcher has no embedded plugin.");
                resource.CopyTo(output); return output.ToArray();
            }
        }
        public static string Stage(Release release, string target, Func<string, byte[]> fetch = null)
        {
            fetch = fetch ?? Fetch;
            var assets = release.assets ?? new Asset[0];
            string tagged = "UsumOneScreen-" + release.tag_name + ".exe";
            var asset = assets.FirstOrDefault(a => a.name == Path.GetFileName(target)) ??
                assets.FirstOrDefault(a => a.name == tagged) ??
                assets.FirstOrDefault(a => a.name == "usum-onescreen-" + release.tag_name + "-windows.zip");
            if (asset == null) throw new InvalidDataException("No matching launcher release asset.");
            var bytes = Download(asset, fetch);
            var checksums = assets.Where(a => Regex.IsMatch(a.name ?? "", @"(?i)(sha256|checksums?)")).ToArray();
            if (checksums.Length > 0)
            {
                bool verified = false;
                foreach (var checksum in checksums)
                {
                    string text = Encoding.UTF8.GetString(Download(checksum, fetch));
                    foreach (string line in text.Split('\n'))
                    {
                        var m = Regex.Match(line.Trim(), @"^([a-fA-F0-9]{64})\s+\*?(.+)$");
                        string hash = m.Success && m.Groups[2].Value == asset.name ? m.Groups[1].Value :
                            checksum.name == asset.name + ".sha256" && Regex.IsMatch(text.Trim(), @"\A[a-fA-F0-9]{64}\z") ? text.Trim() : null;
                        if (hash == null) continue;
                        if (!hash.Equals(Hash(bytes), StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Update SHA-256 mismatch.");
                        verified = true;
                    }
                }
                if (!verified) throw new InvalidDataException("Published checksums do not cover the launcher asset.");
            }
            if (asset.name.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
            {
                using (var zip = new ZipArchive(new MemoryStream(bytes), ZipArchiveMode.Read))
                {
                    var entries = zip.Entries.Where(e => e.FullName.Replace('\\', '/').Split('/').Last() == "UsumOneScreen.exe").ToArray();
                    if (entries.Length != 1 || entries[0].Length > 64 * 1024 * 1024) throw new InvalidDataException("Invalid launcher ZIP.");
                    using (var input = entries[0].Open())
                    using (var output = new MemoryStream()) { input.CopyTo(output); bytes = output.ToArray(); }
                }
            }
            Plugin(bytes);
            string pending = target + ".update-new";
            // Never touch the live launcher while downloading or validating.
            try { File.WriteAllBytes(pending, bytes); }
            catch { if (File.Exists(pending)) File.Delete(pending); throw; }
            return pending;
        }

        // File.Replace is atomic per file. Keep backups and roll back the complete set on failure.
        public static void Swap(string target, Home home)
        {
            if (home.IsRunning()) throw new IOException("Close Azahar before updating the launcher and plugin.");
            string pending = target + ".update-new";
            byte[] plugin = Plugin(File.ReadAllBytes(pending));
            var paths = new List<string>();
            string adjacent = Path.Combine(Path.GetDirectoryName(target), "usum-onescreen.3gx");
            if (File.Exists(adjacent)) paths.Add(adjacent);
            foreach (string title in new[] { Dump.Title(Dump.GameTitle), Dump.Title(Dump.MoonGameTitle) })
            {
                string path = Path.Combine(home.Sdmc, "luma", "plugins", title, "usum-onescreen.3gx");
                if (File.Exists(path)) paths.Add(path);
            }
            paths = paths.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            var changed = new List<string>();
            try
            {
                foreach (string path in paths) File.WriteAllBytes(path + ".update-new", plugin);
                foreach (string path in paths.Concat(new[] { target }))
                {
                    if (File.Exists(path + ".update-old")) throw new IOException("An update backup already exists: " + path);
                    File.Replace(path + ".update-new", path, path + ".update-old", true); changed.Add(path);
                }
            }
            catch
            {
                foreach (string path in changed.AsEnumerable().Reverse()) File.Replace(path + ".update-old", path, null, true);
                throw;
            }
            finally { foreach (string path in paths) if (File.Exists(path + ".update-new")) File.Delete(path + ".update-new"); }
            foreach (string path in changed) { try { File.Delete(path + ".update-old"); } catch (IOException) { } }
        }
        static string Quote(string value) => "\"" + Regex.Replace(value, @"\\+$", m => m.Value + m.Value) + "\"";
        public static void CleanupHelper()
        {
            try { File.Delete(Application.ExecutablePath + ".update-helper.exe"); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
        public static int Helper(string[] args)
        {
            try
            {
                try
                {
                    using (var parent = Process.GetProcessById(int.Parse(args[1])))
                        if (!parent.WaitForExit(60000)) throw new IOException("The launcher did not close in time.");
                }
                catch (ArgumentException) { } // parent has already exited
                var home = new Home(args[3]);
                Swap(args[2], home);
                Process.Start(new ProcessStartInfo(args[2], "--home " + Quote(home.Root)) { UseShellExecute = false });
                return 0;
            }
            catch (Exception e) { MessageBox.Show("Couldn't finish the update. " + e.Message + " If recovery is needed, keep the .update-old backups.", "Launcher update"); return 1; }
        }
        public static void Attach(Form form, Home home)
        {
            var menu = new MenuStrip(); var entry = new ToolStripMenuItem("Check for updates");
            menu.Items.Add(entry); form.Controls.Add(menu); menu.BringToFront();
            bool checking = false;
            Func<bool, Task> check = async manual =>
            {
                if (checking) return;
                checking = true; entry.Enabled = false;
                try
                {
                    var result = await Check(home, manual, DateTime.UtcNow);
                    if (form.IsDisposed) return;
                    if (result.Release == null) { if (manual) MessageBox.Show(form, result.Message, "Launcher update"); return; }
                    using (var prompt = new Form { Text = result.Message, Width = 440, Height = 150, StartPosition = FormStartPosition.CenterParent })
                    {
                        var row = new FlowLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12) };
                        row.Controls.Add(new Label { Text = "Update launcher and plugin, then restart?", AutoSize = true });
                        foreach (var choice in new[] { "Update", "Skip this version", "Later" })
                        {
                            var button = new Button { Text = choice, AutoSize = true };
                            button.Click += (s, e) => { prompt.Tag = button.Text; prompt.Close(); }; row.Controls.Add(button);
                        }
                        prompt.Controls.Add(row); prompt.ShowDialog(form);
                        if ((string)prompt.Tag == "Skip this version") home.SetState("update_skip_version", result.Release.tag_name);
                        if ((string)prompt.Tag != "Update") return;
                    }
                    if (home.IsRunning()) throw new IOException("Close Azahar before updating.");
                    string target = Application.ExecutablePath;
                    await Task.Run(() => Stage(result.Release, target));
                    if (form.IsDisposed) return;
                    string helper = target + ".update-helper.exe";
                    File.Copy(target, helper, true);
                    Process.Start(new ProcessStartInfo(helper, "--apply-launcher-update " + Process.GetCurrentProcess().Id + " " + Quote(target) + " " + Quote(home.Root)) { UseShellExecute = false });
                    Application.Exit();
                }
                catch (Exception e) { if (!form.IsDisposed) MessageBox.Show(form, "Couldn't install the update. " + e.Message, "Launcher update"); }
                finally { checking = false; if (!form.IsDisposed) entry.Enabled = true; }
            };
            entry.Click += async (s, e) => await check(true);
            form.Shown += async (s, e) => { if (form.ShowInTaskbar) await check(false); };
        }
    }
}
