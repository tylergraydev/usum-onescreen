using System;
using System.IO;

namespace UsumOneScreen
{
    enum DumpKind { Game, Update }

    // What a ROM or CIA file says about itself, read from its headers only.
    sealed class DumpInfo
    {
        public string Path;
        public string Format;          // "NCSD (.3ds/.cci)", "NCCH (.cxi)", "CIA"
        public ulong TitleId;
        public int Version = -1;       // title version from a TMD; -1 when the format has none
        public bool Encrypted;
        public string Error;           // set when the file isn't a readable 3DS dump
    }

    // Verdict for one file the player picked.
    sealed class DumpCheck
    {
        public bool Ok;
        public string Message;
        public string Warning;         // accepted, but the player should know this
        public DumpInfo Info;
    }

    static class Dump
    {
        public const ulong GameTitle = 0x00040000001B5000;    // Pokémon Ultra Sun (USA)
        public const ulong UpdateTitle = 0x0004000E001B5000;  // its update
        public const int UpdateVersion = 0x0820;              // v1.2 as Nintendo shipped it (v2.2.0)
        // A decrypted v1.2 copy whose TMD was rewritten to 0x0800. Same code, and it
        // plays with an existing save, but a new game stops at the language screen
        // (docs/PLAYER-APP.md).
        public const int PatchedUpdateVersion = 0x0800;

        public static bool IsSupportedUpdate(int v) => v == UpdateVersion || v == PatchedUpdateVersion;

        public const string PatchedWarning =
            "This v1.2 update file reports version 0x0800, a rebuilt copy: an existing save plays, " +
            "but a new game stops at the language screen. A dump of the update that reports 0x0820 has no such problem.";

        public static string Title(ulong id) => id.ToString("X16");

        public static string VersionText(int v) => $"0x{v:X4} ({v})";

        public static DumpCheck Check(string path, DumpKind kind)
        {
            var info = Read(path);
            var check = new DumpCheck { Info = info };
            string name = System.IO.Path.GetFileName(path);
            if (info.Error != null)
            {
                check.Message = $"{name}: {info.Error}";
                return check;
            }
            ulong want = kind == DumpKind.Game ? GameTitle : UpdateTitle;
            if (info.TitleId != want)
            {
                string what = info.TitleId == GameTitle ? "the Ultra Sun game itself, not its update"
                            : info.TitleId == UpdateTitle ? "the Ultra Sun update, not the game"
                            : "not Pokémon Ultra Sun (USA)";
                check.Message = $"{name} is title {Title(info.TitleId)}: {what}. " +
                                $"Pick {(kind == DumpKind.Game ? "Pokémon Ultra Sun (USA), title " + Title(GameTitle) : "the Ultra Sun v1.2 update, title " + Title(UpdateTitle))}.";
                return check;
            }
            if (kind == DumpKind.Update && !IsSupportedUpdate(info.Version))
            {
                check.Message = $"{name} is update version {VersionText(info.Version)}. " +
                                $"USUM One Screen only supports v1.2, version {VersionText(UpdateVersion)}.";
                return check;
            }
            if (info.Encrypted)
            {
                check.Message = $"{name} is encrypted. USUM One Screen needs a decrypted dump of your own copy.";
                return check;
            }
            check.Ok = true;
            if (kind == DumpKind.Update && info.Version == PatchedUpdateVersion)
                check.Warning = PatchedWarning;
            check.Message = kind == DumpKind.Game
                ? $"{name}: Pokémon Ultra Sun (USA), title {Title(info.TitleId)}, {info.Format}."
                : $"{name}: Ultra Sun update v1.2, version {VersionText(info.Version)}.";
            return check;
        }

        public static DumpInfo Read(string path)
        {
            var info = new DumpInfo { Path = path };
            try
            {
                using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
                using (var r = new BinaryReader(f))
                {
                    if (f.Length < 0x400)
                        return Fail(info, "too small to be a 3DS dump");
                    string magic = Magic(r, 0x100);
                    if (magic == "NCSD")
                    {
                        info.Format = "NCSD (.3ds/.cci)";
                        long ncch = (long)ReadU32(r, 0x120) * 0x200;    // partition 0
                        return ReadNcch(r, ncch, info);
                    }
                    if (magic == "NCCH")
                    {
                        info.Format = "NCCH (.cxi)";
                        return ReadNcch(r, 0, info);
                    }
                    if (ReadU32(r, 0) == 0x2020)
                    {
                        info.Format = "CIA";
                        return ReadCia(r, info);
                    }
                    return Fail(info, "not a 3DS ROM (.3ds, .cci, .cxi) or CIA");
                }
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is EndOfStreamException || e is ArgumentException)
            {
                return Fail(info, "can't read it: " + e.Message);
            }
        }

        static DumpInfo ReadNcch(BinaryReader r, long at, DumpInfo info)
        {
            if (Magic(r, at + 0x100) != "NCCH")
                return Fail(info, "its first partition isn't an NCCH; the dump may be damaged or encrypted as a whole");
            info.TitleId = ReadU64(r, at + 0x118);              // program ID
            r.BaseStream.Seek(at + 0x18F, SeekOrigin.Begin);    // flags[7]
            info.Encrypted = (r.ReadByte() & 0x04) == 0;        // bit 2: NoCrypto
            return info;
        }

        // CIA: header, certificate chain, ticket, TMD, content, each 64-byte aligned.
        static DumpInfo ReadCia(BinaryReader r, DumpInfo info)
        {
            uint header = ReadU32(r, 0x00);
            uint certs = ReadU32(r, 0x08);
            uint ticket = ReadU32(r, 0x0C);
            uint tmdSize = ReadU32(r, 0x10);
            long tmd = Align(header) + Align(certs) + Align(ticket);
            var t = ReadTmd(r, tmd);
            if (t == null)
                return Fail(info, "its TMD can't be read; the CIA may be damaged");
            info.TitleId = t.TitleId;
            info.Version = t.Version;
            info.Encrypted = t.Content0Encrypted;
            if (!info.Encrypted)
            {
                // A content not flagged as encrypted must still start with a plain NCCH.
                long content = Align(tmd + tmdSize);
                if (Magic(r, content + 0x100) != "NCCH")
                    info.Encrypted = true;
                else
                {
                    r.BaseStream.Seek(content + 0x18F, SeekOrigin.Begin);
                    info.Encrypted = (r.ReadByte() & 0x04) == 0;
                }
            }
            return info;
        }

        public sealed class Tmd
        {
            public ulong TitleId;
            public int Version;
            public uint Content0Id;
            public bool Content0Encrypted;
        }

        // Reads a TMD at `at`: signature, then the header (title ID at 0x4C,
        // version at 0x9C, content count at 0x9E), 64 info records, the chunks.
        public static Tmd ReadTmd(BinaryReader r, long at)
        {
            uint sigType = ReadU32BE(r, at);
            int sig;
            switch (sigType)
            {
                case 0x10000: case 0x10003: sig = 0x200 + 0x3C; break;    // RSA-4096
                case 0x10001: case 0x10004: sig = 0x100 + 0x3C; break;    // RSA-2048
                case 0x10002: case 0x10005: sig = 0x3C + 0x40; break;     // ECDSA
                default: return null;
            }
            long h = at + 4 + sig;
            var tmd = new Tmd
            {
                TitleId = ReadU64BE(r, h + 0x4C),
                Version = ReadU16BE(r, h + 0x9C),
            };
            if (ReadU16BE(r, h + 0x9E) == 0)
                return null;
            long chunk = h + 0xC4 + 64 * 0x24;     // first content chunk record
            tmd.Content0Id = ReadU32BE(r, chunk);
            tmd.Content0Encrypted = (ReadU16BE(r, chunk + 6) & 0x0001) != 0;
            return tmd;
        }

        public static Tmd ReadTmdFile(string path)
        {
            try
            {
                using (var f = File.OpenRead(path))
                using (var r = new BinaryReader(f))
                    return ReadTmd(r, 0);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                return null;
            }
        }

        static DumpInfo Fail(DumpInfo info, string error)
        {
            info.Error = error;
            return info;
        }

        static long Align(long x) => (x + 63) & ~63L;

        static string Magic(BinaryReader r, long at)
        {
            if (at + 4 > r.BaseStream.Length)
                return "";
            r.BaseStream.Seek(at, SeekOrigin.Begin);
            var b = r.ReadBytes(4);
            return b.Length == 4 ? System.Text.Encoding.ASCII.GetString(b) : "";
        }

        static byte[] Bytes(BinaryReader r, long at, int n)
        {
            r.BaseStream.Seek(at, SeekOrigin.Begin);
            var b = r.ReadBytes(n);
            if (b.Length != n)
                throw new EndOfStreamException("file ends early");
            return b;
        }

        static uint ReadU32(BinaryReader r, long at) => BitConverter.ToUInt32(Bytes(r, at, 4), 0);
        static ulong ReadU64(BinaryReader r, long at) => BitConverter.ToUInt64(Bytes(r, at, 8), 0);

        static int ReadU16BE(BinaryReader r, long at)
        {
            var b = Bytes(r, at, 2);
            return (b[0] << 8) | b[1];
        }

        static uint ReadU32BE(BinaryReader r, long at)
        {
            var b = Bytes(r, at, 4);
            return ((uint)b[0] << 24) | ((uint)b[1] << 16) | ((uint)b[2] << 8) | b[3];
        }

        static ulong ReadU64BE(BinaryReader r, long at) => ((ulong)ReadU32BE(r, at) << 32) | ReadU32BE(r, at + 4);
    }
}
