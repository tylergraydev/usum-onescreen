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
        public static void ShowBackups(Form owner, Home home)
        {
            using (var form = Dialog(home.GameName + " backups"))
            {
                var root = Root(form);
                root.Controls.Add(new Label { Text = "Save data only; extdata is not included. Close Azahar before backup or restore.", AutoSize = true, MaximumSize = new Size(560, 0) });
                var list = new ListView { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, MultiSelect = false, HideSelection = false };
                list.Columns.Add("Date / name", 240); list.Columns.Add("Size (bytes)", 160);
                root.Controls.Add(list);
                void Refresh()
                {
                    list.Items.Clear();
                    foreach (var path in Backups.List(home))
                    {
                        var item = new ListViewItem(Path.GetFileName(path)) { Tag = Path.GetFileName(path) };
                        item.SubItems.Add(Backups.Size(home, path).ToString("N0")); list.Items.Add(item);
                    }
                }
                var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
                buttons.Controls.Add(Button("Back up now", () => { Backups.Create(home); Refresh(); }));
                buttons.Controls.Add(Button("Restore", () =>
                {
                    if (home.IsRunning()) throw new SetupException("Close Azahar before restoring a save.");
                    if (list.SelectedItems.Count == 0) return;
                    string name = (string)list.SelectedItems[0].Tag;
                    if (MessageBox.Show(form, "Restore " + name + "? Your current save will be backed up first.", "Restore save", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
                    Backups.Restore(home, name); Refresh();
                }));
                buttons.Controls.Add(Button("Open folder", () =>
                {
                    string folder = Backups.Folder(home); Backups.Safe(home, folder);
                    Directory.CreateDirectory(folder); Process.Start(new ProcessStartInfo(folder) { UseShellExecute = true });
                }));
                root.Controls.Add(buttons);
                Guard(owner, Refresh);
                form.ShowDialog(owner);
            }
        }
        public static void ShowCheats(Form owner, Home home)
        {
            using (var form = Dialog(home.GameName + " cheats"))
            {
                var entries = Cheats.Load(home);
                var root = Root(form);
                root.Controls.Add(new Label { Text = "Cheats change your game. Back up first (Backups…). Changes apply the next time the game starts.", AutoSize = true, MaximumSize = new Size(560, 0) });
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
