using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace UsumOneScreen
{
    static class OptionDialogs
    {
        static Form Dialog(string title) => new Form { Text = title, Font = SystemFonts.MessageBoxFont,
            AutoScaleMode = AutoScaleMode.Dpi, Size = new Size(630, 390), MinimumSize = new Size(500, 320),
            StartPosition = FormStartPosition.CenterParent, MinimizeBox = false, MaximizeBox = false };
        static TableLayoutPanel Root(Form form)
        {
            var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, Padding = new Padding(12) };
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            form.Controls.Add(root); return root;
        }
        static Button Button(string text, Action action)
        {
            var button = new Button { Text = text, AutoSize = true };
            button.Click += (s, e) => Guard(button.FindForm(), action);
            return button;
        }
        static void Guard(IWin32Window owner, Action action)
        {
            try { action(); }
            catch (Exception e) when (e is SetupException || e is IOException || e is UnauthorizedAccessException)
            { MessageBox.Show(owner, e.Message, "USUM One Screen", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
        }
        static string AskName(Form owner, string title, string value = "")
        {
            using (var form = Dialog(title))
            {
                form.Size = new Size(520, 210); form.MinimumSize = new Size(420, 210);
                var root = Root(form);
                root.Controls.Add(new Label { Text = "Slot name (1-64 characters; no path characters)", AutoSize = true });
                var input = new TextBox { Text = value, Dock = DockStyle.Top, MaxLength = 64 };
                root.Controls.Add(input);
                var buttons = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
                var ok = Button("OK", () => { SaveSlots.ValidateName(input.Text); form.DialogResult = DialogResult.OK; });
                var cancel = new Button { Text = "Cancel", AutoSize = true, DialogResult = DialogResult.Cancel };
                buttons.Controls.Add(ok); buttons.Controls.Add(cancel); root.Controls.Add(buttons);
                form.AcceptButton = ok; form.CancelButton = cancel;
                return form.ShowDialog(owner) == DialogResult.OK ? input.Text : null;
            }
        }
        static void ExportSave(Form form, Home home, string slot = null, string backup = null)
        {
            using (var picker = new SaveFileDialog { Filter = "Save archive (*.zip)|*.zip", DefaultExt = "zip", AddExtension = true,
                OverwritePrompt = false, Title = "Export save to a new ZIP file" })
                if (picker.ShowDialog(form) == DialogResult.OK) SaveArchive.Export(home, picker.FileName, slot, backup);
        }
        public static void ShowBackups(Form owner, Home home)
        {
            using (var form = Dialog(home.GameName + " saves"))
            {
                form.Size = new Size(760, 480); form.MinimumSize = new Size(550, 390);
                var root = Root(form);
                root.Controls.Add(new Label { Text = "Save data only; extdata is not included. Close Azahar before saving or loading. Load/restore backs up your current save first.",
                    AutoSize = true, Dock = DockStyle.Fill });
                var tabs = new TabControl { Dock = DockStyle.Fill };
                root.Controls.Add(tabs);
                var slotsPage = new TabPage("Named slots (kept until deleted)");
                var backupsPage = new TabPage("Backups (newest ten)");
                tabs.TabPages.Add(slotsPage); tabs.TabPages.Add(backupsPage);
                ListView MakeList(TabPage page, out FlowLayoutPanel buttons)
                {
                    var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, Padding = new Padding(4) };
                    panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
                    panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                    page.Controls.Add(panel);
                    var list = new ListView { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, MultiSelect = false, HideSelection = false };
                    list.Columns.Add("Name / date", 310); list.Columns.Add("Size (bytes)", 150); panel.Controls.Add(list);
                    buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = true };
                    panel.Controls.Add(buttons); return list;
                }
                var slots = MakeList(slotsPage, out var slotButtons);
                var backups = MakeList(backupsPage, out var backupButtons);
                string Selected(ListView list) => list.SelectedItems.Count == 0 ? null : (string)list.SelectedItems[0].Tag;
                void Refresh()
                {
                    slots.Items.Clear(); backups.Items.Clear();
                    foreach (string path in SaveSlots.List(home))
                    {
                        string name = SaveSlots.Name(path);
                        var item = new ListViewItem(name) { Tag = name };
                        item.SubItems.Add(Backups.Size(home, Path.Combine(path, "data")).ToString("N0")); slots.Items.Add(item);
                    }
                    foreach (string path in Backups.List(home))
                    {
                        var item = new ListViewItem(Path.GetFileName(path)) { Tag = Path.GetFileName(path) };
                        item.SubItems.Add(Backups.Size(home, path).ToString("N0")); backups.Items.Add(item);
                    }
                }
                bool ConfirmLoad(string name) => MessageBox.Show(form, "Load " + name + "? Your current save will be backed up first.",
                    "Load save", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) == DialogResult.Yes;
                void OpenFolder(string folder)
                {
                    Backups.Safe(home, folder); Directory.CreateDirectory(folder);
                    Process.Start(new ProcessStartInfo(folder) { UseShellExecute = true });
                }
                slotButtons.Controls.Add(Button("Save current to slot", () =>
                {
                    string name = AskName(form, "Save current to slot");
                    if (name != null) { SaveSlots.Save(home, name); Refresh(); }
                }));
                slotButtons.Controls.Add(Button("Load", () =>
                {
                    string name = Selected(slots);
                    if (name != null && ConfirmLoad(name)) { SaveSlots.Load(home, name); Refresh(); }
                }));
                slotButtons.Controls.Add(Button("Rename", () =>
                {
                    string name = Selected(slots); if (name == null) return;
                    string next = AskName(form, "Rename slot", name);
                    if (next != null) { SaveSlots.Rename(home, name, next); Refresh(); }
                }));
                slotButtons.Controls.Add(Button("Delete", () =>
                {
                    string name = Selected(slots);
                    if (name != null && MessageBox.Show(form, "Delete slot " + name + "?", "Delete slot", MessageBoxButtons.YesNo,
                        MessageBoxIcon.Warning) == DialogResult.Yes) { SaveSlots.Delete(home, name); Refresh(); }
                }));
                slotButtons.Controls.Add(Button("Export slot", () => { string name = Selected(slots); if (name != null) ExportSave(form, home, slot: name); }));
                slotButtons.Controls.Add(Button("Open folder", () => OpenFolder(SaveSlots.Folder(home))));
                backupButtons.Controls.Add(Button("Back up now", () => { Backups.Create(home); Refresh(); }));
                backupButtons.Controls.Add(Button("Restore", () =>
                {
                    string name = Selected(backups);
                    if (name != null && ConfirmLoad(name)) { Backups.Restore(home, name); Refresh(); }
                }));
                backupButtons.Controls.Add(Button("Export backup", () => { string name = Selected(backups); if (name != null) ExportSave(form, home, backup: name); }));
                backupButtons.Controls.Add(Button("Open folder", () => OpenFolder(Backups.Folder(home))));
                var transfer = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = true };
                transfer.Controls.Add(Button("Export current save", () => ExportSave(form, home)));
                transfer.Controls.Add(Button("Import ZIP to new slot", () =>
                {
                    using (var picker = new OpenFileDialog { Filter = "Save archive (*.zip)|*.zip", Title = "Import a save archive for " + home.GameName })
                    {
                        if (picker.ShowDialog(form) != DialogResult.OK) return;
                        string name = AskName(form, "Name imported slot", Path.GetFileNameWithoutExtension(picker.FileName));
                        if (name != null) { SaveArchive.Import(home, picker.FileName, name); Refresh(); tabs.SelectedTab = slotsPage; }
                    }
                }));
                transfer.Controls.Add(Button("Close", () => form.Close())); root.Controls.Add(transfer);
                Guard(owner, Refresh); form.ShowDialog(owner);
            }
        }
        public static void ShowCheats(Form owner, Home home)
        {
            using (var form = Dialog(home.GameName + " cheats"))
            {
                var entries = Cheats.Load(home);
                var root = Root(form);
                root.Controls.Add(new Label { Text = "Cheats change your game. Back up first (Backupsâ€¦). Changes apply the next time the game starts.", AutoSize = true, MaximumSize = new Size(560, 0) });
                var list = new CheckedListBox { Dock = DockStyle.Fill, CheckOnClick = true, IntegralHeight = false };
                foreach (var entry in entries) list.Items.Add(entry, entry.Enabled);
                bool updating = false;
                list.ItemCheck += (s, e) =>
                {
                    if (updating) return;
                    Cheats.Enable(entries, entries[e.Index], e.NewValue == CheckState.Checked);
                    updating = true;
                    for (int i = 0; i < entries.Count; i++) if (i != e.Index) list.SetItemChecked(i, entries[i].Enabled);
                    updating = false;
                };
                root.Controls.Add(list);
                var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
                buttons.Controls.Add(Button("Save", () => { Cheats.Save(home, entries); form.Close(); }));
                buttons.Controls.Add(Button("Cancel", () => form.Close())); root.Controls.Add(buttons);
                form.ShowDialog(owner);
            }
        }
    }
}
