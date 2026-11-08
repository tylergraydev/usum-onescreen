#include "UI/GameAssets.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace OneScreen
{
namespace UI
{
    static u16 Get16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
    static u32 Get32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

    // --- Containers ---

    bool Lz11Decompress(const u8 *src, size_t size, std::vector<u8> &out)
    {
        out.clear();
        if (size < 4 || src[0] != 0x11)
            return false;
        u32 outSize = src[1] | (src[2] << 8) | (src[3] << 16);
        size_t p = 4;
        if (outSize == 0)
        {
            if (size < 8)
                return false;
            outSize = Get32(src + 4);
            p = 8;
        }
        if (outSize > 16u << 20)
            return false;
        out.resize(outSize);
        u32 o = 0;
        while (o < outSize)
        {
            if (p >= size)
                return false;
            const u8 flags = src[p++];
            for (u32 bit = 0; bit < 8 && o < outSize; bit++)
            {
                if (!(flags & (0x80 >> bit)))
                {
                    if (p >= size)
                        return false;
                    out[o++] = src[p++];
                    continue;
                }
                if (p + 1 >= size)
                    return false;
                const u32 b0 = src[p], kind = b0 >> 4;
                u32 length, disp;
                if (kind == 0)
                {
                    if (p + 2 >= size)
                        return false;
                    length = (((b0 & 0xF) << 4) | (src[p + 1] >> 4)) + 0x11;
                    disp = (((src[p + 1] & 0xF) << 8) | src[p + 2]) + 1;
                    p += 3;
                }
                else if (kind == 1)
                {
                    if (p + 3 >= size)
                        return false;
                    length = (((b0 & 0xF) << 12) | (src[p + 1] << 4) | (src[p + 2] >> 4)) + 0x111;
                    disp = (((src[p + 2] & 0xF) << 8) | src[p + 3]) + 1;
                    p += 4;
                }
                else
                {
                    length = kind + 1;
                    disp = (((b0 & 0xF) << 8) | src[p + 1]) + 1;
                    p += 2;
                }
                if (disp > o)
                    return false;
                for (u32 i = 0; i < length && o < outSize; i++, o++)
                    out[o] = out[o - disp];
            }
        }
        return true;
    }

    // SARC: header (0x14: magic, u16 header size, BOM, u32 file size, u32
    // data offset), SFAT (u16 node count, nodes of {hash, attributes (name
    // offset / 4 in the low 16 bits), start, end}), SFNT (names).
    static bool SarcAt(const u8 *data, size_t size, size_t at, const std::string &name, ArchiveFile &out)
    {
        const u8 *s = data + at;
        const size_t avail = size - at;
        if (avail < 0x20 || std::memcmp(s, "SARC", 4) != 0 || Get16(s + 4) != 0x14 || Get16(s + 6) != 0xFEFF)
            return false;
        const u32 dataAt = Get32(s + 0x0C);
        const u32 sfat = 0x14;
        if (std::memcmp(s + sfat, "SFAT", 4) != 0)
            return false;
        const u32 nodes = Get16(s + sfat + 6);
        const size_t sfnt = sfat + 0x0C + (size_t)nodes * 16;
        if (sfnt + 8 > avail || std::memcmp(s + sfnt, "SFNT", 4) != 0)
            return false;
        const size_t names = sfnt + 8;
        for (u32 n = 0; n < nodes; n++)
        {
            const u8 *node = s + sfat + 0x0C + n * 16;
            const u32 attr = Get32(node + 4);
            if ((attr >> 24) == 0)
                continue;
            const size_t nameAt = names + (size_t)(attr & 0xFFFF) * 4;
            if (nameAt + name.size() + 1 > avail || std::memcmp(s + nameAt, name.c_str(), name.size() + 1) != 0)
                continue;
            const u32 start = Get32(node + 8), end = Get32(node + 12);
            if (end < start || (size_t)dataAt + end > avail)
                return false;
            out.offset = (u32)(at + dataAt + start);
            out.size = end - start;
            return true;
        }
        return false;
    }

    bool SarcFind(const u8 *data, size_t size, const std::string &name, ArchiveFile &out)
    {
        // An ALYT keeps its SARC after a header of variable size.
        for (size_t at = 0; at + 0x20 <= size; at += 4)
            if (std::memcmp(data + at, "SARC", 4) == 0)
                return SarcAt(data, size, at, name, out);
        return false;
    }

    bool MiniPackEntry(const u8 *data, size_t size, u32 index, ArchiveFile &out)
    {
        if (size < 4)
            return false;
        const u32 count = Get16(data + 2);
        if (index >= count || 4 + (size_t)(count + 1) * 4 > size)
            return false;
        const u32 start = Get32(data + 4 + index * 4), end = Get32(data + 8 + index * 4);
        if (end < start || end > size)
            return false;
        out.offset = start;
        out.size = end - start;
        return true;
    }

    // --- Images ---

    static u32 Pow2(u32 v)
    {
        u32 n = 8;
        while (n < v)
            n <<= 1;
        return n;
    }

    static const s32 EtcTable[8][2] = { { 2, 8 }, { 5, 17 }, { 9, 29 }, { 13, 42 }, { 18, 60 }, { 24, 80 }, { 33, 106 }, { 47, 183 } };

    static u8 Clamp255(s32 v)
    {
        return (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
    }

    // One ETC1 4x4 block (64-bit little-endian word) into texels at (bx, by).
    static void DecodeEtcBlock(uint64_t v, uint64_t alpha, bool hasAlpha, u8 *texels, u32 texW, u32 bx, u32 by)
    {
        const bool diff = (v >> 33) & 1, flip = (v >> 32) & 1;
        const u32 table[2] = { (u32)(v >> 37) & 7, (u32)(v >> 34) & 7 };
        s32 base[2][3];
        for (int c = 0; c < 3; c++)
        {
            const u32 shift = 59 - c * 8;  // r 59, g 51, b 43
            if (diff)
            {
                const s32 b5 = (s32)((v >> shift) & 31);
                s32 d = (s32)((v >> (shift - 3)) & 7);
                if (d & 4)
                    d -= 8;
                const s32 b52 = (b5 + d) & 31;
                base[0][c] = (b5 << 3) | (b5 >> 2);
                base[1][c] = (b52 << 3) | (b52 >> 2);
            }
            else
            {
                base[0][c] = (s32)((v >> (shift + 1)) & 15) * 17;
                base[1][c] = (s32)((v >> (shift - 3)) & 15) * 17;
            }
        }
        for (u32 x = 0; x < 4; x++)
            for (u32 y = 0; y < 4; y++)
            {
                const u32 k = x * 4 + y;
                const int sub = flip ? (y >= 2) : (x >= 2);
                const u32 idx = (u32)(((v >> (16 + k)) & 1) << 1 | ((v >> k) & 1));
                const s32 m = idx == 0 ? EtcTable[table[sub]][0] : idx == 1 ? EtcTable[table[sub]][1]
                            : idx == 2 ? -EtcTable[table[sub]][0] : -EtcTable[table[sub]][1];
                u8 *t = texels + ((by + y) * texW + bx + x) * 4;
                t[0] = Clamp255(base[sub][0] + m);
                t[1] = Clamp255(base[sub][1] + m);
                t[2] = Clamp255(base[sub][2] + m);
                t[3] = hasAlpha ? (u8)(((alpha >> (k * 4)) & 15) * 17) : 255;
            }
    }

    static void TexelFromBytes(u8 fmt, const u8 *d, u32 i, u8 *t)
    {
        u16 v;
        switch (fmt)
        {
        case 0:  t[0] = t[1] = t[2] = d[i]; t[3] = 255; break;                                   // L8
        case 1:  t[0] = t[1] = t[2] = 255; t[3] = d[i]; break;                                   // A8
        case 2:  t[0] = t[1] = t[2] = (u8)((d[i] >> 4) * 17); t[3] = (u8)((d[i] & 15) * 17); break;  // LA4
        case 3:  t[0] = t[1] = t[2] = d[i * 2 + 1]; t[3] = d[i * 2]; break;                       // LA8
        case 5:  v = Get16(d + i * 2);                                                            // RGB565
                 t[0] = (u8)((v >> 11 & 31) * 255 / 31); t[1] = (u8)((v >> 5 & 63) * 255 / 63);
                 t[2] = (u8)((v & 31) * 255 / 31); t[3] = 255; break;
        case 6:  t[0] = d[i * 3 + 2]; t[1] = d[i * 3 + 1]; t[2] = d[i * 3]; t[3] = 255; break;   // RGB8
        case 7:  v = Get16(d + i * 2);                                                            // RGBA5551
                 t[0] = (u8)((v >> 11 & 31) * 255 / 31); t[1] = (u8)((v >> 6 & 31) * 255 / 31);
                 t[2] = (u8)((v >> 1 & 31) * 255 / 31); t[3] = (v & 1) ? 255 : 0; break;
        case 8:  v = Get16(d + i * 2);                                                            // RGBA4
                 t[0] = (u8)((v >> 12 & 15) * 17); t[1] = (u8)((v >> 8 & 15) * 17);
                 t[2] = (u8)((v >> 4 & 15) * 17); t[3] = (u8)((v & 15) * 17); break;
        case 9:  t[0] = d[i * 4 + 3]; t[1] = d[i * 4 + 2]; t[2] = d[i * 4 + 1]; t[3] = d[i * 4]; break;  // RGBA8
        case 12: t[0] = t[1] = t[2] = (u8)(((d[i / 2] >> ((i & 1) * 4)) & 15) * 17); t[3] = 255; break;   // L4
        case 13: t[0] = t[1] = t[2] = 255; t[3] = (u8)(((d[i / 2] >> ((i & 1) * 4)) & 15) * 17); break;  // A4
        default: break;
        }
    }

    bool DecodeFlim(const u8 *file, size_t size, Image &out, std::string &error)
    {
        out = Image();
        if (size < 0x28)
        {
            error = "too small for a BFLIM";
            return false;
        }
        const u8 *foot = file + size - 0x28;
        if (std::memcmp(foot, "FLIM", 4) != 0 || std::memcmp(foot + 0x14, "imag", 4) != 0)
        {
            error = "no FLIM footer";
            return false;
        }
        const u32 w = Get16(foot + 0x1C), h = Get16(foot + 0x1E);
        const u8 fmt = foot[0x22], flags = foot[0x23];
        static const u8 bits[14] = { 8, 8, 8, 16, 16, 16, 24, 16, 16, 32, 4, 8, 4, 4 };
        if (fmt >= 14 || fmt == 4 || w == 0 || h == 0 || w > 1024 || h > 1024)
        {
            error = "unsupported BFLIM format " + std::to_string(fmt) + " or size";
            return false;
        }
        const bool transposed = (flags & 4) != 0;
        const u32 texW = Pow2(transposed ? h : w), texH = Pow2(transposed ? w : h);
        if ((size_t)texW * texH * bits[fmt] / 8 > size - 0x28)
        {
            error = "BFLIM data shorter than its size";
            return false;
        }

        std::vector<u8> texels((size_t)texW * texH * 4, 0);
        if (fmt == 10 || fmt == 11)
        {
            const bool alpha = fmt == 11;
            size_t p = 0;
            for (u32 ty = 0; ty < texH; ty += 8)
                for (u32 tx = 0; tx < texW; tx += 8)
                    for (u32 b = 0; b < 4; b++)
                    {
                        uint64_t a = 0, v;
                        if (alpha)
                        {
                            std::memcpy(&a, file + p, 8);
                            p += 8;
                        }
                        std::memcpy(&v, file + p, 8);
                        p += 8;
                        DecodeEtcBlock(v, a, alpha, texels.data(), texW, tx + (b & 1) * 4, ty + (b >> 1) * 4);
                    }
        }
        else
        {
            u32 i = 0;
            for (u32 ty = 0; ty < texH; ty += 8)
                for (u32 tx = 0; tx < texW; tx += 8)
                    for (u32 k = 0; k < 64; k++, i++)
                    {
                        const u32 x = (k & 1) | ((k >> 1) & 2) | ((k >> 2) & 4);
                        const u32 y = ((k >> 1) & 1) | ((k >> 2) & 2) | ((k >> 3) & 4);
                        TexelFromBytes(fmt, file, i, texels.data() + ((ty + y) * texW + tx + x) * 4);
                    }
        }

        out.w = (s32)w;
        out.h = (s32)h;
        out.rgba.resize((size_t)w * h * 4);
        for (u32 y = 0; y < h; y++)
            for (u32 x = 0; x < w; x++)
            {
                const u32 tx = transposed ? texW - 1 - y : x, ty = transposed ? x : y;
                std::memcpy(&out.rgba[(y * w + x) * 4], &texels[(ty * texW + tx) * 4], 4);
            }
        return true;
    }

    void DrawImage(Canvas &canvas, const Image &image, s32 x, s32 y, const Rect *src, u8 alpha)
    {
        Rect part = src != nullptr ? Intersect(*src, Rect{ 0, 0, image.w, image.h }) : Rect{ 0, 0, image.w, image.h };
        if (part.Empty())
            return;
        for (s32 py = 0; py < part.h; py++)
            for (s32 px = 0; px < part.w; px++)
            {
                const u8 *p = &image.rgba[((size_t)(part.y + py) * image.w + part.x + px) * 4];
                if (p[3] == 0)
                    continue;
                canvas.BlendPixel(x + px, y + py, Rgb(p[0], p[1], p[2], 255), (u8)(p[3] * alpha / 255));
            }
    }

    // --- Font ---

    bool ParseFfntHeader(const u8 *head, size_t size, FfntInfo &f, std::string &error)
    {
        f = FfntInfo();
        if (size < 0x14 || std::memcmp(head, "FFNT", 4) != 0 || Get16(head + 4) != 0xFEFF)
        {
            error = "not an FFNT font";
            return false;
        }
        const u32 finf = Get16(head + 6);
        if (finf + 0x20 > size || std::memcmp(head + finf, "FINF", 4) != 0)
        {
            error = "no FINF block";
            return false;
        }
        const u8 *p = head + finf + 8;
        f.height = p[1];
        f.width = p[2];
        f.ascent = p[3];
        f.lineFeed = Get16(p + 4);
        f.altIndex = Get16(p + 6);
        f.defLeft = (std::int8_t)p[8];
        f.defGlyphW = p[9];
        f.defCharW = p[10];
        f.glyphAt = Get32(p + 12);
        f.widthAt = Get32(p + 16);
        f.mapAt = Get32(p + 20);
        if (f.glyphAt < 8 || f.glyphAt - 8 + 0x20 > size || std::memcmp(head + f.glyphAt - 8, "TGLP", 4) != 0)
        {
            error = "no TGLP block in the header";
            return false;
        }
        const u8 *t = head + f.glyphAt;
        f.cellW = t[0];
        f.cellH = t[1];
        f.sheetCount = t[2];
        f.sheetSize = Get32(t + 4);
        f.baseline = Get16(t + 8);
        f.sheetFormat = Get16(t + 10);
        f.cellsPerRow = Get16(t + 12);
        f.cellRows = Get16(t + 14);
        f.sheetW = Get16(t + 16);
        f.sheetH = Get16(t + 18);
        f.sheetData = Get32(t + 20);
        if (f.sheetFormat != SheetFormatA4)
        {
            error = "glyph sheets are format " + std::to_string(f.sheetFormat) + ", only A4 is supported";
            return false;
        }
        if (f.cellW == 0 || f.cellH == 0 || f.cellW > 64 || f.cellH > 64 || f.cellsPerRow == 0 || f.sheetW % 8 != 0
            || (f.cellW + 1) * f.cellsPerRow > f.sheetW || f.sheetSize < (u32)f.sheetW * f.sheetH / 2)
        {
            error = "bad TGLP geometry";
            return false;
        }
        return true;
    }

    bool GameFont::Load(const FileRead &read, u32 fileSize, std::string &error)
    {
        *this = GameFont();
        u8 head[0x60];
        if (fileSize < sizeof(head) || !read(0, head, sizeof(head)))
        {
            error = "can't read the font header";
            return false;
        }
        if (!ParseFfntHeader(head, sizeof(head), m_info, error))
            return false;
        const FfntInfo &f = m_info;
        const u32 limit = std::min<u32>((u32)f.cellsPerRow * std::min<u32>(f.cellRows, MaxRows),
                                        (u32)f.cellsPerRow * f.cellRows);

        // Character map: a chain of CMAP blocks (direct range, table, or
        // scan list). Only glyphs inside the rows we keep are mapped.
        std::vector<std::pair<u16, u16>> map;
        std::vector<u8> block;
        u32 at = f.mapAt, blocks = 0;
        while (at != 0 && blocks++ < 256)
        {
            u8 h[0x14];
            if (at < 8 || at - 8 + sizeof(h) > fileSize || !read(at - 8, h, sizeof(h)) || std::memcmp(h, "CMAP", 4) != 0)
            {
                error = "bad CMAP chain";
                return false;
            }
            const u32 blockSize = Get32(h + 4);
            if (blockSize < 0x14 || blockSize > 0x20000 || at - 8 + blockSize > fileSize)
            {
                error = "bad CMAP block size";
                return false;
            }
            block.resize(blockSize - 0x14);
            if (!block.empty() && !read(at - 8 + 0x14, block.data(), (u32)block.size()))
            {
                error = "can't read a CMAP block";
                return false;
            }
            const u32 begin = Get16(h + 8), end = Get16(h + 10), method = Get16(h + 12);
            const auto add = [&](u32 code, u32 index) {
                if (index < limit)
                    map.emplace_back((u16)code, (u16)index);
            };
            if (method == 0 && block.size() >= 2 && end >= begin)
            {
                const u32 first = Get16(block.data());
                for (u32 c = begin; c <= end; c++)
                    add(c, first + (c - begin));
            }
            else if (method == 1 && end >= begin && block.size() >= (end - begin + 1) * 2)
            {
                for (u32 c = begin; c <= end; c++)
                {
                    const u32 index = Get16(&block[(c - begin) * 2]);
                    if (index != 0xFFFF)
                        add(c, index);
                }
            }
            else if (method == 2 && block.size() >= 2)
            {
                const u32 n = Get16(block.data());
                for (u32 k = 0; k < n && 2 + k * 4 + 4 <= block.size(); k++)
                    add(Get16(&block[2 + k * 4]), Get16(&block[4 + k * 4]));
            }
            at = Get32(h + 0x10);
        }
        if (map.empty())
        {
            error = "no characters in the loaded rows";
            return false;
        }
        std::sort(map.begin(), map.end());
        u32 top = f.altIndex < limit ? f.altIndex : 0;
        for (const auto &m : map)
            top = std::max<u32>(top, m.second);
        const u32 rows = top / f.cellsPerRow + 1;
        m_glyphs = rows * f.cellsPerRow;

        // Widths: a chain of CWDH blocks of {s8 left, u8 glyph width, u8
        // advance} per glyph.
        m_widths.assign((size_t)m_glyphs * 3, 0);
        for (u32 i = 0; i < m_glyphs; i++)
        {
            m_widths[i * 3] = (u8)f.defLeft;
            m_widths[i * 3 + 1] = f.defGlyphW;
            m_widths[i * 3 + 2] = f.defCharW;
        }
        at = f.widthAt;
        blocks = 0;
        while (at != 0 && blocks++ < 256)
        {
            u8 h[0x10];
            if (at < 8 || at - 8 + sizeof(h) > fileSize || !read(at - 8, h, sizeof(h)) || std::memcmp(h, "CWDH", 4) != 0)
            {
                error = "bad CWDH chain";
                return false;
            }
            const u32 first = Get16(h + 8), last = Get16(h + 10);
            if (last >= first && first < m_glyphs)
            {
                const u32 n = std::min(last, m_glyphs - 1) - first + 1;
                if (at - 8 + 0x10 + n * 3 > fileSize || !read(at - 8 + 0x10, &m_widths[first * 3], n * 3))
                {
                    error = "can't read the width table";
                    return false;
                }
            }
            at = Get32(h + 0x0C);
        }

        // The rows of sheet 0 that hold those glyphs: whole 8-pixel tile rows
        // from the top, so they are one read from the start of the sheet.
        const u32 pixelRows = 1 + rows * (f.cellH + 1u);
        const u32 tileRows = (pixelRows + 7) / 8;
        const u32 bandBytes = tileRows * (f.sheetW / 8u) * 32u;
        if (bandBytes > f.sheetSize || (uint64_t)f.sheetData + bandBytes > fileSize)
        {
            error = "glyph rows outside the file";
            return false;
        }
        m_band.resize(bandBytes);
        if (!read(f.sheetData, m_band.data(), bandBytes))
        {
            error = "can't read the glyph sheet";
            return false;
        }
        m_map.swap(map);

        m_source.sheets = m_band.data();
        m_source.sheetSize = bandBytes;
        m_source.sheetCount = 1;
        m_source.sheetFormat = f.sheetFormat;
        m_source.sheetW = f.sheetW;
        m_source.sheetH = (u16)(tileRows * 8);
        m_source.cellsPerRow = f.cellsPerRow;
        m_source.cellRows = (u16)rows;
        m_source.cellW = f.cellW;
        m_source.cellH = f.cellH;
        m_source.baseline = (u8)f.baseline;
        m_source.lineFeed = (u8)f.lineFeed;
        m_source.indexOf = nullptr;
        m_source.widths = nullptr;
        m_ready = true;
        return true;
    }

    size_t GameFont::Bytes(void) const
    {
        return m_band.size() + m_widths.size() + m_map.size() * sizeof(m_map[0]);
    }

    int GameFont::IndexOf(u32 codePoint) const
    {
        const auto it = std::lower_bound(m_map.begin(), m_map.end(), std::make_pair((u16)codePoint, (u16)0));
        if (codePoint <= 0xFFFF && it != m_map.end() && it->first == codePoint)
            return it->second;
        return m_info.altIndex < m_glyphs ? m_info.altIndex : -1;
    }

    void GameFont::Widths(int index, s32 &left, s32 &glyphW, s32 &advance) const
    {
        if (index < 0 || (u32)index >= m_glyphs)
        {
            left = m_info.defLeft;
            glyphW = m_info.defGlyphW;
            advance = m_info.defCharW;
            return;
        }
        left = (std::int8_t)m_widths[index * 3];
        glyphW = m_widths[index * 3 + 1];
        advance = m_widths[index * 3 + 2];
    }

    bool GameFont::DecodeCell(int index, u8 *out) const
    {
        if (!m_ready || index < 0 || (u32)index >= m_glyphs)
            return false;
        return DecodeGlyphCell(m_source, index, out);
    }

    // --- Game text ---

    void GameTextCache::SetFont(const GameFont *font)
    {
        if (font != nullptr && !font->Ready())
            font = nullptr;
        if (font == m_font)
            return;
        m_font = font;
        for (Slot &slot : m_slots)
            slot.used = false;
        m_maskW = font != nullptr ? (s32)std::ceil(font->Info().cellW * m_scale) : 0;
        m_maskH = font != nullptr ? (s32)std::ceil(font->Info().cellH * m_scale) : 0;
        m_masks.assign((size_t)Slots * m_maskW * m_maskH, 0);
    }

    s32 GameTextCache::Scaled(s32 v) const
    {
        return (s32)std::lround(v * m_scale);
    }

    s32 GameTextCache::LineHeight(void) const
    {
        return m_font != nullptr ? Scaled(m_font->Info().lineFeed) : 0;
    }

    s32 GameTextCache::Ascent(void) const
    {
        return m_font != nullptr ? Scaled(m_font->Info().baseline) : 0;
    }

    s32 GameTextCache::Advance(u32 codePoint)
    {
        const Glyph *g = Get(codePoint);
        return g != nullptr ? g->advance : 0;
    }

    const Glyph *GameTextCache::Get(u32 codePoint)
    {
        if (m_font == nullptr)
            return nullptr;
        const u32 index = codePoint % Slots;
        Slot &slot = m_slots[index];
        u8 *mask = m_masks.data() + (size_t)index * m_maskW * m_maskH;
        if (slot.used && slot.codePoint == codePoint)
        {
            hits++;
            return &slot.glyph;
        }
        misses++;

        const FfntInfo &f = m_font->Info();
        const int glyph = m_font->IndexOf(codePoint);
        s32 left, glyphW, advance;
        m_font->Widths(glyph, left, glyphW, advance);

        // Only the OSD thread renders glyphs.
        static u8 s_cell[64 * 64];
        static u8 s_crop[64 * 64];
        Glyph &g = slot.glyph;
        g.mask = mask;
        g.left = Scaled(left);
        g.advance = Scaled(advance);
        g.w = 0;
        g.h = m_maskH;
        // The glyph is the first glyphW columns of its cell, drawn `left`
        // pixels after the pen; the pen then moves by the advance.
        const s32 inkW = std::min<s32>(f.cellW, std::max<s32>(0, glyphW));
        if (inkW > 0 && m_font->DecodeCell(glyph, s_cell))
        {
            for (s32 y = 0; y < f.cellH; y++)
                std::memcpy(s_crop + y * inkW, s_cell + y * f.cellW, (size_t)inkW);
            g.w = std::min<s32>(m_maskW, (s32)std::ceil(inkW * m_scale));
            ScaleMask(s_crop, inkW, f.cellH, m_scale, mask, g.w, g.h);
        }
        slot.codePoint = codePoint;
        slot.used = true;
        return &g;
    }

    void GameTextCache::Draw(Canvas &canvas, u32 codePoint, s32 x, s32 y, Color color)
    {
        const Glyph *g = Get(codePoint);
        if (g == nullptr || g->w <= 0) return;
        if (canvas.Scale() == 1.f)
        {
            canvas.DrawMask(x + g->left, y, g->mask, g->w, g->h, g->w, 1, color);
            return;
        }
        const FfntInfo &f = m_font->Info();
        u8 cell[64 * 64];
        if ((u32)f.cellW * f.cellH > sizeof(cell) || !m_font->DecodeCell(m_font->IndexOf(codePoint), cell)) return;
        s32 left, glyphW, advance;
        m_font->Widths(m_font->IndexOf(codePoint), left, glyphW, advance);
        const s32 inkW = std::min<s32>(f.cellW, std::max<s32>(0, glyphW));
        canvas.DrawMaskSized((float)(x + g->left), (float)y, cell, inkW, f.cellH, f.cellW,
                            inkW * m_scale, f.cellH * m_scale, color);
    }

    // --- What gets loaded ---

    Rect CategoryIconRect(u8 category)
    {
        // waza_icon_all: three 42x20 plates stacked 20 px apart (status,
        // physical, special), ink at x 5..45.
        return Rect{ 4, (s32)(category < 3 ? category : 0) * 20, 42, 20 };
    }

    MoveInfo DecodeMoveEntry(const u8 *entry, size_t size)
    {
        MoveInfo m;
        if (size < 6)
            return m;
        m.valid = true;
        m.type = entry[0];
        m.category = entry[2];
        m.power = entry[3];
        m.accuracy = entry[4];
        m.pp = entry[5];
        return m;
    }

    const Image *GameAssets::Icon(GameIcon icon) const
    {
        const Image &i = icons[(int)icon];
        return i.Empty() ? nullptr : &i;
    }

    const Image *GameAssets::Ball(u16 item) const
    {
        return item < balls.size() && !balls[item].Empty() ? &balls[item] : nullptr;
    }

    MoveInfo GameAssets::Move(u16 move) const
    {
        return move < moves.size() ? moves[move] : MoveInfo();
    }

    std::string GameAssets::MoveName(u16 move) const
    {
        return move < moveNames.size() ? moveNames[move] : std::string();
    }

    std::string GameAssets::TypeName(u8 type) const
    {
        return type < typeNames.size() ? typeNames[type] : std::string();
    }

    // Where each part lives in the RomFS (docs/GAME-ASSETS.md).
    static const char *FontGarc = "a/0/2/5";       // FFNT fonts; entry 0 is the menu font
    static const char *MoveGarc = "a/0/1/1";       // one "WD" pack, one entry per move
    static const char *ItemIconGarc = "a/0/6/1";   // item icons (LZ11 BFLIM), entry = item ID - 1 for items 1-17
    static const u32 TypeNamesFile = 112, MoveNamesFile = 118;
    static const u32 BallCount = 17;

    struct IconSource
    {
        GameIcon    icon;
        const char *garc;
        u32         entry;
        const char *name;
    };
    static const IconSource IconSources[] = {
        { GameIcon::ButtonA,    "a/1/0/4", 0,  "timg/ButtonIcon_A.bflim" },
        { GameIcon::ButtonB,    "a/1/0/4", 0,  "timg/ButtonIcon_B.bflim" },
        { GameIcon::Cursor,     "a/1/0/4", 0,  "timg/Common_Cursor_02.bflim" },
        { GameIcon::ButtonX,    "a/0/6/6", 0,  "timg/ButtonIcon_X.bflim" },
        { GameIcon::ButtonY,    "a/0/6/6", 0,  "timg/ButtonIcon_Y.bflim" },
        { GameIcon::Categories, "a/0/6/6", 0,  "timg/waza_icon_all.bflim" },
        { GameIcon::Male,       "a/1/3/9", 10, "timg/Icon_Gender_M.bflim" },
        { GameIcon::Female,     "a/1/3/9", 10, "timg/Icon_Gender_F.bflim" },
    };

    // A GARC entry's bytes, decompressed if it is LZ11.
    static bool ReadGarcEntry(const RomfsRead &read, const char *garcPath, u32 entry, std::vector<u8> &out,
                              std::string &error)
    {
        uint64_t garc, garcSize, offset;
        u32 size;
        if (!RomfsFind(read, garcPath, garc, garcSize, error) || !GarcEntry(read, garc, entry, offset, size, error))
            return false;
        if (size > 4u << 20)
        {
            error = "entry too big";
            return false;
        }
        std::vector<u8> raw(size);
        if (size != 0 && !read(offset, raw.data(), size))
        {
            error = "can't read the entry";
            return false;
        }
        if (!raw.empty() && raw[0] == 0x11)
        {
            if (!Lz11Decompress(raw.data(), raw.size(), out))
            {
                error = "bad LZ11 data";
                return false;
            }
            return true;
        }
        out.swap(raw);
        return true;
    }

    void LoadGameAssets(const RomfsRead &read, u32 lang, GameAssets &out, std::vector<std::string> &log)
    {
        std::string error;
        std::vector<std::string> status;

        // Font: GARC entry 0, read in place (only the parts it needs).
        {
            uint64_t garc, garcSize, offset;
            u32 size;
            error.clear();
            if (RomfsFind(read, FontGarc, garc, garcSize, error) && GarcEntry(read, garc, 0, offset, size, error))
            {
                const GameFont::FileRead fileRead = [&read, offset, size](u32 at, void *dst, u32 n) {
                    return (uint64_t)at + n <= size && read(offset + at, dst, n);
                };
                out.font.Load(fileRead, size, error);
            }
            if (out.font.Ready())
            {
                const FfntInfo &f = out.font.Info();
                status.push_back("font: " + std::string(FontGarc) + " entry 0, cell " + std::to_string(f.cellW) + "x"
                                 + std::to_string(f.cellH) + ", line " + std::to_string(f.lineFeed) + ", "
                                 + std::to_string(out.font.GlyphCount()) + " glyphs kept, "
                                 + std::to_string(out.font.Bytes() / 1024) + " KB");
            }
            else
                status.push_back("font: not loaded (" + error + "), system font instead");
        }

        // Icons from layout archives; each archive is read once.
        {
            std::string loadedGarc;
            u32 loadedEntry = ~0u;
            std::vector<u8> archive;
            u32 ok = 0;
            std::string failed;
            for (const IconSource &src : IconSources)
            {
                error.clear();
                if (loadedGarc != src.garc || loadedEntry != src.entry)
                {
                    archive.clear();
                    loadedGarc = src.garc;
                    loadedEntry = src.entry;
                    if (!ReadGarcEntry(read, src.garc, src.entry, archive, error))
                        archive.clear();
                }
                ArchiveFile file;
                Image &image = out.icons[(int)src.icon];
                if (!archive.empty() && SarcFind(archive.data(), archive.size(), src.name, file)
                    && DecodeFlim(archive.data() + file.offset, file.size, image, error))
                    ok++;
                else
                    failed += std::string(failed.empty() ? "" : ", ") + src.name + (error.empty() ? "" : " (" + error + ")");
            }
            status.push_back("icons: " + std::to_string(ok) + " of " + std::to_string(sizeof(IconSources) / sizeof(IconSources[0]))
                             + (failed.empty() ? "" : ", missing " + failed));
        }

        // Ball icons: items 1-16 (Master Ball .. Cherish Ball) are entries 0-15.
        {
            out.balls.assign(BallCount, Image());
            u32 ok = 0;
            std::vector<u8> entry;
            for (u32 i = 1; i < BallCount; i++)
            {
                error.clear();
                if (ReadGarcEntry(read, ItemIconGarc, i - 1, entry, error) && DecodeFlim(entry.data(), entry.size(), out.balls[i], error))
                    ok++;
            }
            status.push_back("balls: " + std::to_string(ok) + " of " + std::to_string(BallCount - 1) + " from " + ItemIconGarc
                             + (ok == BallCount - 1 ? "" : " (" + error + ")"));
        }

        // Move table.
        {
            std::vector<u8> pack;
            error.clear();
            if (ReadGarcEntry(read, MoveGarc, 0, pack, error) && pack.size() >= 4)
            {
                const u32 count = Get16(pack.data() + 2);
                out.moves.assign(count, MoveInfo());
                for (u32 m = 0; m < count; m++)
                {
                    ArchiveFile e;
                    if (MiniPackEntry(pack.data(), pack.size(), m, e))
                        out.moves[m] = DecodeMoveEntry(pack.data() + e.offset, e.size);
                }
            }
            status.push_back(out.moves.empty() ? "moves: not loaded (" + error + ")"
                                               : "moves: " + std::to_string(out.moves.size()) + " from " + MoveGarc);
        }

        // Text: type and move names in the game's language.
        if (lang <= 9)
        {
            const std::string garc = "a/0/3/" + std::to_string(lang);
            error.clear();
            if (!ReadMessageFile(read, garc, TypeNamesFile, out.typeNames, error))
                out.typeNames.clear();
            std::string moveError;
            if (!ReadMessageFile(read, garc, MoveNamesFile, out.moveNames, moveError))
                out.moveNames.clear();
            status.push_back("text: " + std::to_string(out.typeNames.size()) + " type names, "
                             + std::to_string(out.moveNames.size()) + " move names from " + garc
                             + (error.empty() && moveError.empty() ? "" : " (" + error + moveError + ")"));
        }
        else
            status.push_back("text: language byte " + std::to_string(lang) + " not 0-9");

        out.status.clear();
        for (const std::string &s : status)
        {
            out.status += (out.status.empty() ? "" : "\n") + s;
            log.push_back("assets: " + s);
        }
    }

    static const GameAssets *volatile s_published = nullptr;

    const GameAssets *GetGameAssets(void)
    {
        return s_published;
    }

    void PublishGameAssets(const GameAssets *assets)
    {
        s_published = assets;
    }
}
}
