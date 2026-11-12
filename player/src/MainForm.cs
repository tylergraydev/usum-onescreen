using System;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UsumOneScreen
{
    sealed class MainForm : Form
    {
        readonly Home home;

        readonly TextBox romBox = new TextBox(), updateBox = new TextBox();
        readonly Label romCheck = new Label(), updateCheck = new Label(), azaharLabel = new Label();
        readonly ComboBox controls = Combo(Enum.GetValues(typeof(ControlPreset)).Cast<ControlPreset>().Select(ControlPresets.Name).ToArray(), false);
        readonly ComboBox panelKey = Combo(PluginSettings.KeyChoices, true), touchKey = Combo(PluginSettings.KeyChoices, true),
                          mapKey = Combo(PluginSettings.KeyChoices, true), cursorMove = Combo(PluginSettings.CursorMoves, false),
                          size = Combo(PluginSettings.Sizes, false), corner = Combo(PluginSettings.Corners, false),
                          autoPanel = Combo(PluginSettings.OnOff, false), widescreen = Combo(PluginSettings.AspectChoices, false);
        readonly ComboBox graphics = Combo(new[] { "Keep Azahar's setting", "Native (1x)", "High (3x)", "Ultra (5x)" }, false),
                          battle = Combo(new[] { "Classic", "Bar", "Rail" }, false),
                          speed = Combo(new[] { "Normal", "Fast", "Instant" }, false),
                          glyphs = Combo(new[] { "Auto", "Xbox", "PlayStation", "Nintendo", "3DS" }, false);
        bool customSpeed;
        readonly ComboBox visualQuality = Combo(VisualQuality.Labels, false);
        readonly Label visualQualityHint = Caption("");
        string customAspect;
        readonly NumericUpDown cursorSpeed = new NumericUpDown { Minimum = 1, Maximum = 20, Width = 60 };
        readonly CheckBox fullscreen = new CheckBox { Text = "Fullscreen (F11 or Esc leaves it)", AutoSize = true };
        readonly TextBox log = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill };
        readonly Button setupButton = new Button { Text = "Set up", AutoSize = true, Padding = new Padding(12, 4, 12, 4) };
        readonly Button playButton = new Button { Text = "Play", AutoSize = true, Padding = new Padding(24, 4, 24, 4) };
        readonly Button mapButtons = new Button { Text = "Map buttons in Azahar…", AutoSize = true };
        string azaharSource;   // set when the player picks another Azahar
        bool busy;

        public MainForm(Home home)
        {
            this.home = home;
            Text = "USUM One Screen - Ultra Sun / Ultra Moon";
            Font = SystemFonts.MessageBoxFont;
            AutoScaleMode = AutoScaleMode.Dpi;
            MinimumSize = new Size(760, 680);
            Size = new Size(800, 860);
            StartPosition = FormStartPosition.CenterScreen;

            var scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true };
            Controls.Add(scroll);
            var root = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 1, Padding = new Padding(12) };
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            scroll.Controls.Add(root);

            root.Controls.Add(GameGroup());
            root.Controls.Add(EmulatorGroup());
            root.Controls.Add(PluginGroup());
            root.Controls.Add(OptionsGroup());
            log.MinimumSize = new Size(0, 70);
            root.Controls.Add(log);
            var buttons = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, AutoSize = true };
            buttons.Controls.Add(playButton);
            buttons.Controls.Add(setupButton);
            buttons.Controls.Add(fullscreen);
            root.Controls.Add(buttons);

            setupButton.Click += async (s, e) => await RunSetup();
            playButton.Click += async (s, e) => await PlayGame();
            mapButtons.Click += (s, e) => OpenAzaharForMapping();
            controls.SelectedIndexChanged += (s, e) => mapButtons.Enabled = SelectedPreset() == ControlPreset.Custom;
            AcceptButton = playButton;

            EnableDrops(this);
            LoadValues();
            UpdateCheck.Attach(this, home);
        }

        // --- layout ---

        static ComboBox Combo(string[] items, bool editable)
        {
            var c = new ComboBox { DropDownStyle = editable ? ComboBoxStyle.DropDown : ComboBoxStyle.DropDownList, Width = 150 };
            c.Items.AddRange(items);
            return c;
        }

        static TableLayoutPanel Grid(int columns)
        {
            var g = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, ColumnCount = columns };
            return g;
        }

        static Label Caption(string text) =>
            new Label { Text = text, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 7, 3, 3) };

        static GroupBox Group(string text, Control content)
        {
            var g = new GroupBox { Text = text, Dock = DockStyle.Fill, AutoSize = true, Padding = new Padding(8) };
            g.Controls.Add(content);
            return g;
        }

        Control GameGroup()
        {
            var g = Grid(3);
            g.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            g.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            g.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            AddFileRow(g, "Sun / Moon ROM", romBox, romCheck, DumpKind.Game,
                       "Pokémon Ultra Sun / Ultra Moon (*.3ds;*.cci;*.cxi;*.cia;*.zip)|*.3ds;*.cci;*.cxi;*.cia;*.zip|All files|*.*");
            AddFileRow(g, "v1.2 update", updateBox, updateCheck, DumpKind.Update,
                       "Ultra Sun / Ultra Moon update (*.cia;*.zip)|*.cia;*.zip|All files|*.*");
            var note = new Label
            {
                Text = "Drag your game files here, or use Browse\u2026\nUse your own dumps of Pokémon Ultra Sun or Ultra Moon (USA) and its matching v1.2 update. This app doesn't provide them.",
                MaximumSize = new Size(600, 0),
                AutoSize = true, ForeColor = SystemColors.GrayText,
            };
            g.Controls.Add(note, 0, 4);
            g.SetColumnSpan(note, 3);
            return Group("Your game", g);
        }

        void AddFileRow(TableLayoutPanel g, string caption, TextBox box, Label check, DumpKind kind, string filter)
        {
            int row = g.RowCount;
            g.RowCount += 2;
            box.Dock = DockStyle.Fill;
            box.Leave += (s, e) => ShowCheck(box, check, kind);
            var browse = new Button { Text = "Browse…", AutoSize = true };
            browse.Click += async (s, e) =>
            {
                using (var d = new OpenFileDialog { Filter = filter, Title = caption })
                {
                    if (File.Exists(box.Text))
                        d.InitialDirectory = Path.GetDirectoryName(box.Text);
                    if (d.ShowDialog(this) == DialogResult.OK)
                    {
                        await AcceptFiles(new[] { d.FileName });
                    }
                }
            };
            g.Controls.Add(Caption(caption), 0, row);
            g.Controls.Add(box, 1, row);
            g.Controls.Add(browse, 2, row);
            check.AutoSize = true;
            check.MaximumSize = new Size(560, 0);
            g.Controls.Add(check, 1, row + 1);
            g.SetColumnSpan(check, 2);
        }

        Control EmulatorGroup()
        {
            var g = Grid(3);
            g.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            g.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            g.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            azaharLabel.AutoSize = true;
            azaharLabel.Anchor = AnchorStyles.Left;
            var change = new Button { Text = "Use another Azahar…", AutoSize = true };
            change.Enabled = Home.BundledAzahar() == null;
            change.Click += (s, e) => PickAzahar();
            g.Controls.Add(Caption("Azahar"), 0, 0);
            g.Controls.Add(azaharLabel, 1, 0);
            g.Controls.Add(change, 2, 0);
            controls.Width = 300;
            g.Controls.Add(Caption("Controls"), 0, 1);
            g.Controls.Add(controls, 1, 1);
            g.Controls.Add(mapButtons, 2, 1);
            return Group("Emulator", g);
        }

        Control PluginGroup()
        {
            var g = Grid(4);
            void Row(int row, string a, Control ca, string b, Control cb)
            {
                g.Controls.Add(Caption(a), 0, row);
                g.Controls.Add(ca, 1, row);
                g.Controls.Add(Caption(b), 2, row);
                g.Controls.Add(cb, 3, row);
            }
            Row(0, "Bottom screen view", panelKey, "Touch", touchKey);
            Row(1, "Map", mapKey, "Cursor moves with", cursorMove);
            Row(2, "Cursor speed", cursorSpeed, "Panel size", size);
            Row(3, "Panel corner", corner, "Automatic view", autoPanel);

            return Group("Plugin (usum-onescreen.ini)", g);
        }

        Control OptionsGroup()
        {
            var g = Grid(4);
            graphics.Width = 185;
            visualQuality.Width = 185;
            visualQuality.DropDownWidth = 230;
            glyphs.Width = 185;
            void Row(int row, string a, Control ca, string b, Control cb)
            {
                g.Controls.Add(Caption(a), 0, row); g.Controls.Add(ca, 1, row);
                g.Controls.Add(Caption(b), 2, row); g.Controls.Add(cb, 3, row);
            }
            Row(0, "Widescreen", widescreen, "Graphics", graphics);
            Row(1, "Battle menu", battle, "Text and battle speed", speed);
            Row(2, "Button icons", glyphs, "Visual quality", visualQuality);
            visualQualityHint.MaximumSize = new Size(650, 0);
            g.Controls.Add(visualQualityHint, 0, 3); g.SetColumnSpan(visualQualityHint, 4);
            var hint = Caption("21:9 widescreen looks best on High or Ultra.");
            g.Controls.Add(hint, 0, 4); g.SetColumnSpan(hint, 4);
            var buttons = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
            var backups = new Button { Text = "Saves\u2026", AutoSize = true };
            var cheats = new Button { Text = "Cheats\u2026", AutoSize = true };
            backups.Click += (s, e) => { if (!busy) OptionDialogs.ShowBackups(this, home); };
            cheats.Click += (s, e) => { if (!busy) OptionDialogs.ShowCheats(this, home); };
            var textures = new Button { Text = "Texture packs...", AutoSize = true };
            textures.Click += (s, e) => { if (!busy) OptionDialogs.ShowTextures(this, home); };
            buttons.Controls.Add(backups); buttons.Controls.Add(cheats); buttons.Controls.Add(textures);
            g.Controls.Add(buttons, 0, 5); g.SetColumnSpan(buttons, 4);
            return Group("Game options", g);
        }

        // --- values ---

        void LoadValues()
        {
            romBox.Text = home.GetState("rom", "");
            if (romBox.Text != "")
                ShowCheck(romBox, romCheck, DumpKind.Game);
            ShowInstalledUpdate(home.CurrentTitle);
            controls.SelectedIndex = (int)home.CurrentControls();
            mapButtons.Enabled = SelectedPreset() == ControlPreset.Custom;
            fullscreen.Checked = home.GetState("fullscreen") == "on";

            var ini = PluginIni.Load(home.PluginIniPath);
            panelKey.Text = ini.Get("panel_key");
            touchKey.Text = ini.Get("touch_key");
            mapKey.Text = ini.Get("map_key");
            Select(cursorMove, ini.Get("cursor_move"));
            cursorSpeed.Value = int.TryParse(ini.Get("cursor_speed"), out int n) ? Math.Max(1, Math.Min(20, n)) : 6;
            Select(size, ini.Get("size") == "half" ? "1/2" : ini.Get("size"));
            Select(corner, ini.Get("corner"));
            Select(autoPanel, new[] { "on", "true", "yes", "1" }.Contains(ini.Get("auto")?.ToLowerInvariant()) ? "on" : "off");

            string aspect = string.Equals(home.GetState("aspect"), "auto", StringComparison.OrdinalIgnoreCase)
                ? "auto" : PluginSettings.NormalizeAspect(ini.Get("aspect"));
            int aspectIndex = Array.IndexOf(PluginSettings.AspectValues, aspect);
            if (aspectIndex < 0)
            {
                customAspect = aspect;
                widescreen.Items.Add($"Custom ({aspect})");
                aspectIndex = widescreen.Items.Count - 1;
            }
            widescreen.SelectedIndex = aspectIndex;

            graphics.SelectedIndex = Array.IndexOf(GameOptions.Graphics, GameOptions.CurrentGraphics(home));
            visualQuality.SelectedIndex = Array.IndexOf(VisualQuality.Values, VisualQuality.Current(home));
            Select(battle, ini.Get("layout"));
            string messages = ini.Get("battle_messages"), bars = ini.Get("battle_bars");
            string text = ini.Get("instant_text");
            customSpeed = messages != bars || (messages == "normal" ? text != "off" : text != "on") ||
                          !new[] { "normal", "fast", "instant" }.Contains(messages);
            if (customSpeed) { speed.Items.Add("Custom (keep)"); speed.SelectedIndex = 3; }
            else Select(speed, messages);
            glyphs.SelectedIndex = Math.Max(0, Array.IndexOf(GameOptions.Glyphs, GameOptions.CurrentGlyphs(home)));
            UpdateGlyphLabel();
            controls.SelectedIndexChanged += (s, e) => UpdateGlyphLabel();
            ShowAzahar();
            UpdateButtons();
            string notReady = Setup.NotReady(home);
            Log(notReady == null ? "Ready. Press Play." : "Pick your ROM and update, then press Play.");
        }

        void UpdateGlyphLabel()
        {
            string detected = Win32.DetectGlyphs(SelectedPreset());
            string label = detected == "3ds" ? "3DS" : detected == "playstation" ? "PlayStation" : detected == "nintendo" ? "Nintendo" : "Xbox";
            int selected = glyphs.SelectedIndex;
            glyphs.Items[0] = "Auto (detected: " + label + ")";
            glyphs.DropDownWidth = 240;
            glyphs.SelectedIndex = selected;
        }

        static void Select(ComboBox c, string value)
        {
            int i = c.Items.Cast<string>().ToList().FindIndex(s => string.Equals(s, value, StringComparison.OrdinalIgnoreCase));
            c.SelectedIndex = i >= 0 ? i : 0;
        }

        ControlPreset SelectedPreset() => (ControlPreset)Math.Max(0, controls.SelectedIndex);

        void ShowAzahar()
        {
            string dir = home.HasAzahar && azaharSource == null ? home.AzaharDir
                         : Home.BundledAzahar() ?? azaharSource;
            if (dir == null)
            {
                azaharLabel.Text = "Downloads tested Azahar 2126.2 on first setup.";
                azaharLabel.ForeColor = SystemColors.ControlText;
                return;
            }
            string version = Home.AzaharVersion(dir) ?? "version unknown";
            string where = azaharSource == null && home.HasAzahar ? "the app's copy" : dir;
            azaharLabel.Text = $"{version}, {where}" + (version == Home.TestedAzahar ? "" : $" (tested with {Home.TestedAzahar})");
            azaharLabel.ForeColor = version == Home.TestedAzahar ? SystemColors.ControlText : Color.DarkOrange;
        }

        void ShowCheck(TextBox box, Label check, DumpKind kind)
        {
            if (box.Text.Trim() == "")
            {
                check.Text = "";
                return;
            }
            ulong? title = null;
            if (kind == DumpKind.Update && !string.IsNullOrWhiteSpace(romBox.Text))
            {
                var rom = Dump.Check(romBox.Text.Trim(), DumpKind.Game);
                if (rom.Ok) title = rom.Info.TitleId;
            }
            var c = Dump.Check(box.Text.Trim(), kind, title);
            if (kind == DumpKind.Game && c.Ok)
            {
                Text = $"USUM One Screen - {Dump.GameName(c.Info.TitleId)}";
                if (string.IsNullOrWhiteSpace(updateBox.Text)) ShowInstalledUpdate(c.Info.TitleId);
                else ShowCheck(updateBox, updateCheck, DumpKind.Update);
            }
            check.Text = (c.Ok ? "✓ " : "✗ ") + c.Message + (c.Warning != null ? " " + c.Warning : "");
            check.ForeColor = !c.Ok ? Color.DarkRed : c.Warning != null ? Color.DarkOrange : Color.DarkGreen;
        }

        void EnableDrops(Control control)
        {
            control.AllowDrop = true;
            control.DragEnter += (s, e) =>
                e.Effect = !busy && e.Data.GetDataPresent(DataFormats.FileDrop) ? DragDropEffects.Copy : DragDropEffects.None;
            control.DragDrop += async (s, e) =>
            {
                if (!busy && e.Data.GetDataPresent(DataFormats.FileDrop))
                    await AcceptFiles((string[])e.Data.GetData(DataFormats.FileDrop));
            };
            foreach (Control child in control.Controls) EnableDrops(child);
        }

        async Task AcceptFiles(string[] paths)
        {
            if (busy) return;
            busy = true;
            UpdateButtons();
            try
            {
                var files = await Task.Run(() => paths.SelectMany(p => GameFiles.Expand(home, p, Log)).ToList());
                var errors = new System.Collections.Generic.List<string>();
                foreach (var file in files)
                {
                    var check = GameFiles.Check(file);
                    if (!check.Ok) { errors.Add(check.Message); continue; }
                    if (Dump.IsGame(check.Info.TitleId)) romBox.Text = file;
                    else updateBox.Text = file;
                }
                ShowCheck(romBox, romCheck, DumpKind.Game);
                ShowCheck(updateBox, updateCheck, DumpKind.Update);
                if (errors.Count > 0) { romCheck.Text = string.Join(Environment.NewLine, errors); romCheck.ForeColor = Color.DarkRed; }
            }
            catch (Exception e) when (e is SetupException || e is IOException || e is UnauthorizedAccessException)
            {
                romCheck.Text = e.Message;
                romCheck.ForeColor = Color.DarkRed;
                Log(e.Message);
            }
            finally { busy = false; UpdateButtons(); }
        }

        void ShowInstalledUpdate(ulong title)
        {
            int v = home.InstalledUpdateVersion(title);
            bool ok = Dump.IsSupportedUpdate(v, title);
            updateCheck.Text = ok
                ? $"{Dump.GameName(title)} update v1.2 is installed; you don't need to pick it again." +
                  (v == Dump.PatchedUpdateVersion ? " " + Dump.PatchedWarning : "")
                : $"Pick the {Dump.GameName(title)} v1.2 update.";
            updateCheck.ForeColor = !ok ? SystemColors.ControlText : v == Dump.PatchedUpdateVersion ? Color.DarkOrange : Color.DarkGreen;
        }

        void PickAzahar()
        {
            using (var d = new OpenFileDialog { Filter = "azahar.exe|azahar.exe", Title = "Pick azahar.exe in your Azahar folder" })
            {
                if (d.ShowDialog(this) != DialogResult.OK)
                    return;
                azaharSource = Path.GetDirectoryName(d.FileName);
                ShowAzahar();
                Log("Azahar will be copied from there when you press Set up or Play.");
            }
        }

        // Writes the plugin settings; returns false (and says why) if one is bad.
        bool SavePluginSettings()
        {
            string aspect = widescreen.SelectedIndex < PluginSettings.AspectValues.Length
                ? PluginSettings.AspectValues[widescreen.SelectedIndex] : customAspect;
            var values = new System.Collections.Generic.List<(string key, string value)>
            {
                ("panel_key", panelKey.Text.Trim()), ("touch_key", touchKey.Text.Trim()), ("map_key", mapKey.Text.Trim()),
                ("cursor_move", cursorMove.Text), ("cursor_speed", ((int)cursorSpeed.Value).ToString()),
                ("size", size.Text), ("corner", corner.Text), ("auto", autoPanel.Text), ("aspect", aspect),
            };
            values.Add(("layout", battle.Text.ToLowerInvariant()));
            if (!(customSpeed && speed.SelectedIndex == 3))
            {
                string pace = speed.Text.ToLowerInvariant();
                values.Add(("instant_text", pace == "normal" ? "off" : "on"));
                values.Add(("battle_messages", pace)); values.Add(("battle_bars", pace));
            }
            string glyphChoice = GameOptions.Glyphs[Math.Max(0, glyphs.SelectedIndex)];
            values.Add(("glyphs", glyphChoice == "auto" ? Win32.DetectGlyphs(SelectedPreset()) : glyphChoice));
            foreach (var (key, value) in values)
            {
                string error = PluginSettings.Validate(key, value);
                if (error != null)
                {
                    Log(error);
                    return false;
                }
            }
            var ini = PluginIni.Load(home.PluginIniPath);
            bool changed = false;
            foreach (var (key, value) in values)
                if (!(key == "aspect" && value == "auto") && !string.Equals(ini.Get(key), value, StringComparison.OrdinalIgnoreCase))
                {
                    ini.Set(key, value);
                    changed = true;
                }
            if (changed)
                ini.Save(home.PluginIniPath);
            home.SetState("glyphs", glyphChoice);
            if (visualQuality.Enabled)
            {
                string choice = VisualQuality.Values[Math.Max(0, visualQuality.SelectedIndex)];
                // Re-check after external Azahar edits before saving the selection.
                if (choice != "keep" && choice != "off" && VisualQuality.UnavailableReason(home) is string reason)
                {
                    Log(reason);
                    UpdateVisualQuality();
                    return false;
                }
                VisualQuality.Set(home, choice);
            }
            GameOptions.SetGraphics(home, GameOptions.Graphics[Math.Max(0, graphics.SelectedIndex)]);
            home.SetState("aspect", aspect);
            if (home.HasAzahar)
                home.SyncWidescreen();
            return true;
        }

        // --- actions ---

        void Log(string line)
        {
            if (InvokeRequired)
            {
                BeginInvoke(new Action<string>(Log), line);
                return;
            }
            log.AppendText(line + Environment.NewLine);
        }

        void UpdateButtons()
        {
            bool running = home.IsRunning();
            setupButton.Enabled = !busy && !running;
            playButton.Enabled = !busy && !running;
            playButton.Text = running ? "Playing" : "Play";
            UpdateVisualQuality();
        }

        void UpdateVisualQuality()
        {
            string reason = VisualQuality.UnavailableReason(home);
            visualQuality.Enabled = !busy && !home.IsRunning() && reason == null;
            visualQualityHint.Text = reason ?? "Visual quality: OpenGL only; applies next start. AA may soften text.";
        }

        Setup.Options CurrentOptions() => new Setup.Options
        {
            Rom = romBox.Text.Trim(),
            Update = updateBox.Text.Trim() == "" ? null : updateBox.Text.Trim(),
            AzaharSource = azaharSource,
            Controls = SelectedPreset(),
        };

        async Task<bool> RunSetup()
        {
            if (!SavePluginSettings())
                return false;
            busy = true;
            UpdateButtons();
            var o = CurrentOptions();
            try
            {
                await Task.Run(() => Setup.Run(home, o, Log));
                azaharSource = null;
                romBox.Text = o.Rom;
                updateBox.Clear();
                home.SetState("fullscreen", fullscreen.Checked ? "on" : "off");
                ShowAzahar();
                Text = $"USUM One Screen - {home.GameName}";
                ShowInstalledUpdate(home.CurrentTitle);
                return true;
            }
            catch (Exception e) when (e is SetupException || e is IOException || e is UnauthorizedAccessException)
            {
                Log("Stopped: " + e.Message);
                MessageBox.Show(this, e.Message, "USUM One Screen", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            finally
            {
                busy = false;
                UpdateButtons();
            }
        }

        async Task PlayGame()
        {
            // Settings changed or never set up: run setup first (it is quick
            // once Azahar is copied and the update is installed).
            bool same = Setup.NotReady(home) == null && azaharSource == null && string.IsNullOrWhiteSpace(updateBox.Text) &&
                        string.Equals(romBox.Text.Trim(), home.GetState("rom"), StringComparison.OrdinalIgnoreCase) &&
                        SelectedPreset() == home.CurrentControls();
            if (same ? !SavePluginSettings() : !await RunSetup())
                return;
            home.SetState("fullscreen", fullscreen.Checked ? "on" : "off");
            try
            {
                var p = home.Launch(home.GetState("game"), fullscreen.Checked);
                Log($"Starting {home.GameName} …");
                UpdateButtons();
                p.EnableRaisingEvents = true;
                p.Exited += (s, e) => BeginInvoke(new Action(() => { Log("Azahar closed."); UpdateButtons(); }));
                string boot = await Task.Run(() => home.PluginBootLine(120000, () => !p.HasExited));
                if (boot != null)
                {
                    string first = boot.Split('\n').FirstOrDefault(l => l.Contains("USUM One Screen") || l.Contains("unsupported")) ?? boot.Split('\n')[0];
                    Log("Plugin: " + first.Trim());
                    if (!boot.Contains("hooks: all ok"))
                        Log("The plugin's startup check didn't pass; see " + home.PluginLogPath);
                }
                else if (!p.HasExited)
                    Log("The plugin hasn't written its log yet; check that the notice appears in the game.");
            }
            catch (Exception e) when (e is SetupException || e is IOException || e is System.ComponentModel.Win32Exception)
            {
                Log("Couldn't start: " + e.Message);
            }
        }

        void OpenAzaharForMapping()
        {
            if (!home.HasAzahar)
            {
                Log("Press Set up first, so the app has its Azahar copy.");
                return;
            }
            if (home.IsRunning())
            {
                Log("Azahar is already open.");
                return;
            }
            home.WriteConfig(ControlPreset.Custom);
            home.OpenAzahar();
            Log("In Azahar: Emulation > Configure > Controls. Map your buttons, press OK and close Azahar. Those mappings are used from then on.");
        }
    }
}
