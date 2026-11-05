using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;

namespace UsumOneScreen
{
    static class GameFiles
    {
        public static bool IsZip(string path) => string.Equals(Path.GetExtension(path), ".zip", StringComparison.OrdinalIgnoreCase);
        public static DumpCheck Check(string path) => Dump.Check(path, Dump.IsUpdate(Dump.Read(path).TitleId) ? DumpKind.Update : DumpKind.Game);
        public static string Pick(IEnumerable<string> files, DumpKind kind, bool required = true)
        {
            string result = files.FirstOrDefault(f => Dump.Check(f, kind).Ok);
            if (result == null && required) throw new SetupException($"The zip contains no supported decrypted {kind.ToString().ToLowerInvariant()}.");
            return result;
        }

        public static List<string> Expand(Home home, string path, Action<string> log)
        {
            if (!IsZip(path)) return new List<string> { path };
            try
            {
                using (var zip = ZipFile.OpenRead(path))
                {
                    var entries = zip.Entries.Where(e => new[] { ".3ds", ".cci", ".cia" }
                        .Contains(Path.GetExtension(e.FullName), StringComparer.OrdinalIgnoreCase)).ToList();
                    if (entries.Count == 0) throw new SetupException("The zip contains no .3ds, .cci or .cia files.");
                    string games = Path.Combine(home.Root, "games");
                    var planned = new List<Tuple<ZipArchiveEntry, string, bool>>();
                    var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                    long needed = 0;
                    foreach (var entry in entries)
                    {
                        string name = Path.GetFileName(entry.FullName.Replace('\\', '/'));
                        if (name.IndexOf(':') >= 0 || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
                            throw new SetupException("The zip contains an invalid game file name.");
                        string destination = Path.Combine(games, name);
                        int suffix = 0;
                        bool reuse;
                        while (true)
                        {
                            reuse = File.Exists(destination) && new FileInfo(destination).Length == entry.Length && SameHeader(entry, destination);
                            if (!used.Contains(destination) && (!File.Exists(destination) || reuse)) break;
                            destination = Path.Combine(games, Path.GetFileNameWithoutExtension(name) + "-" + (++suffix) + Path.GetExtension(name));
                        }
                        used.Add(destination);
                        planned.Add(Tuple.Create(entry, destination, reuse));
                        if (!reuse) needed = checked(needed + entry.Length);
                    }
                    var drive = new DriveInfo(Path.GetPathRoot(Path.GetFullPath(games)));
                    if (drive.AvailableFreeSpace < needed) throw new SetupException("Not enough free space to extract the game zip.");
                    Directory.CreateDirectory(games);
                    foreach (var item in planned)
                    {
                        if (item.Item3) { log("Using existing " + item.Item2); continue; }
                        string temp = Path.Combine(games, Guid.NewGuid().ToString("N") + ".tmp");
                        try
                        {
                            log("Extracting " + Path.GetFileName(item.Item2) + " …");
                            item.Item1.ExtractToFile(temp);
                            File.Move(temp, item.Item2);
                        }
                        finally { if (File.Exists(temp)) File.Delete(temp); }
                    }
                    return planned.Select(i => i.Item2).ToList();
                }
            }
            catch (Exception e) when (e is IOException || e is InvalidDataException || e is UnauthorizedAccessException || e is OverflowException || e is ArgumentException)
            { throw new SetupException("Couldn't extract game zip: " + e.Message); }
        }

        static bool SameHeader(ZipArchiveEntry entry, string path)
        {
            DumpInfo incoming;
            using (var stream = new EntryReader(entry)) incoming = Dump.Read(stream, path);
            var existing = Check(path);
            var a = existing.Info;
            return existing.Ok && incoming.Error == null && a.TitleId == incoming.TitleId &&
                a.Format == incoming.Format && a.Version == incoming.Version && a.Encrypted == incoming.Encrypted;
        }

        // Header parsing seeks. Reopen a compressed entry for backward seeks and
        // skip forward with bounded memory; never unpack a second full game to compare it.
        sealed class EntryReader : Stream
        {
            readonly ZipArchiveEntry entry;
            Stream input;
            long position;
            public EntryReader(ZipArchiveEntry entry) { this.entry = entry; input = entry.Open(); }
            public override bool CanRead => true;
            public override bool CanSeek => true;
            public override bool CanWrite => false;
            public override long Length => entry.Length;
            public override long Position { get => position; set => Seek(value, SeekOrigin.Begin); }
            public override int Read(byte[] buffer, int offset, int count)
            { int n = input.Read(buffer, offset, count); position += n; return n; }
            public override long Seek(long offset, SeekOrigin origin)
            {
                long target = origin == SeekOrigin.Begin ? offset : origin == SeekOrigin.Current ? position + offset : Length + offset;
                if (target < 0 || target > Length) throw new IOException("Header offset outside zip entry.");
                if (target < position) { input.Dispose(); input = entry.Open(); position = 0; }
                var buffer = new byte[81920];
                while (position < target)
                    if (Read(buffer, 0, (int)Math.Min(buffer.Length, target - position)) == 0) throw new EndOfStreamException();
                return position;
            }
            protected override void Dispose(bool disposing) { if (disposing) input.Dispose(); base.Dispose(disposing); }
            public override void Flush() { }
            public override void SetLength(long value) => throw new NotSupportedException();
            public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        }
    }
}
