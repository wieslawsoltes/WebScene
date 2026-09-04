// WOFF2 reconstruction follows https://github.com/google/woff2 (MIT).
// Copyright (c) 2013-2017 by the WOFF2 Authors.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
using System.Buffers.Binary;
using System.IO.Compression;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

// Decode web-font containers before the platform-specific Skia font manager.
// No font interpretation, subsetting, native dependency, or render-loop work.
internal static class NativeWebFontDecoder
{
    private const int Limit = 64 * 1024 * 1024;
    private const uint Glyf = 0x676c7966, Loca = 0x6c6f6361, Head = 0x68656164,
        Hmtx = 0x686d7478, Hhea = 0x68686561, Maxp = 0x6d617870;
    private static readonly string[] KnownTags = ("cmap head hhea hmtx maxp name OS/2 post cvt_ fpgm glyf loca prep CFF_ VORG EBDT EBLC gasp hdmx kern LTSH PCLT VDMX vhea vmtx BASE GDEF GPOS GSUB EBSC JSTF MATH CBDT CBLC COLR CPAL SVG_ sbix acnt avar bdat bloc bsln cvar fdsc feat fmtx fvar gvar hsty just lcar mort morx opbd prop trak Zapf Silf Glat Gloc Feat Sill").Split(' ');
    private sealed record Table(uint Tag, int OriginalLength, bool Transformed, int Length)
    {
        public byte[] Data = [];
    }

    internal static bool IsCompressed(ReadOnlySpan<byte> data) => data.Length >= 4
        && (data[..4].SequenceEqual("wOF2"u8) || data[..4].SequenceEqual("wOFF"u8));

    internal static byte[] Decode(ReadOnlySpan<byte> data)
    {
        try { return DecodeCore(data); }
        catch (Exception ex) when (ex is OverflowException or ArgumentOutOfRangeException or IndexOutOfRangeException or EndOfStreamException)
        { throw new InvalidDataException("Invalid web-font bounds.", ex); }
    }

    private static byte[] DecodeCore(ReadOnlySpan<byte> data)
    {
        Require(data.Length is >= 44 and <= Limit);
        var input = new Reader(data.ToArray());
        var signature = input.U32();
        Require(signature is 0x774f4632 or 0x774f4646);
        var flavor = input.U32();
        // Collections require selecting a face; retain unsupported-font fallback.
        Require(flavor is 0x00010000 or 0x4f54544f or 0x74727565);
        Require(input.U32() == data.Length);
        var count = input.U16();
        Require(count is > 0 and <= 256 && input.U16() == 0);
        Require(input.U32() is > 0 and <= Limit);
        var tables = new Dictionary<uint, Table>();
        if (signature == 0x774f4646)
        {
            input.Take(24); // Version and optional metadata/private blocks.
            var entries = new List<(Table Table, int Offset, int Compressed)>();
            for (var i = 0; i < count; i++)
            {
                var tag = input.U32();
                var offset = input.Length32();
                var compressed = input.Length32();
                var length = input.Length32();
                input.U32(); // Recompute checksums for the reconstructed SFNT.
                Require(compressed <= length && offset >= 44 + count * 20);
                var table = new Table(tag, length, false, length);
                Require(tables.TryAdd(tag, table));
                entries.Add((table, offset, compressed));
            }
            Require(entries.Sum(e => (long)e.Table.Length) <= Limit);
            foreach (var (table, offset, compressed) in entries)
            {
                var source = data.Slice(offset, compressed);
                if (compressed == table.Length) table.Data = source.ToArray();
                else
                {
                    using var stream = new ZLibStream(new MemoryStream(source.ToArray()), CompressionMode.Decompress);
                    table.Data = new byte[table.Length];
                    stream.ReadExactly(table.Data);
                    Require(stream.ReadByte() == -1);
                }
            }
        }
        else
        {
            var compressedLength = input.Length32();
            input.Take(24);
            var total = 0;
            for (var i = 0; i < count; i++)
            {
                var flags = input.Byte();
                var tag = (flags & 63) == 63 ? input.U32() : Tag(KnownTags[flags & 63]);
                var version = flags >> 6;
                var glyphTable = tag is Glyf or Loca;
                Require(glyphTable ? version is 0 or 3 : version == 0 || (tag == Hmtx && version == 1));
                var transformed = glyphTable ? version == 0 : version != 0;
                var original = input.Base128();
                var length = transformed ? input.Base128() : original;
                Require(tag != Loca || !transformed || length == 0);
                Require(tables.TryAdd(tag, new Table(tag, original, transformed, length)));
                total = checked(total + length);
                Require(total <= Limit);
            }
            Require(tables.Values.Sum(t => (long)t.OriginalLength) <= Limit);
            var decoded = new byte[total];
            using var brotli = new BrotliDecoder();
            var status = brotli.Decompress(input.Take(compressedLength), decoded, out var consumed, out var written);
            Require(status == System.Buffers.OperationStatus.Done && written == total && consumed == compressedLength);
            var offset = 0;
            foreach (var table in tables.Values)
            {
                table.Data = decoded.AsSpan(offset, table.Length).ToArray();
                offset += table.Length;
            }
            if (tables.TryGetValue(Glyf, out var glyf))
            {
                Require(tables.TryGetValue(Loca, out var loca) && loca.Transformed == glyf.Transformed);
                if (glyf.Transformed) ReconstructGlyphs(tables, glyf, loca!);
            }
            else Require(!tables.ContainsKey(Loca));
            if (tables.TryGetValue(Hmtx, out var hmtx) && hmtx.Transformed) ReconstructMetrics(tables, hmtx);
        }
        return BuildSfnt(flavor, tables);
    }

    private static void ReconstructGlyphs(Dictionary<uint, Table> tables, Table glyf, Table loca)
    {
        var reader = new Reader(glyf.Data);
        Require(reader.U16() == 0);
        var options = reader.U16();
        Require((options & ~1) == 0);
        var glyphCount = reader.U16();
        var indexFormat = reader.U16();
        Require(indexFormat <= 1 && loca.OriginalLength == (glyphCount + 1) * (indexFormat == 0 ? 2 : 4));
        Require(U16(Get(tables, Maxp), 4) == glyphCount);
        var lengths = new int[7];
        for (var i = 0; i < lengths.Length; i++) lengths[i] = reader.Length32();
        var streams = lengths.Select(length => new Reader(reader.Take(length).ToArray())).ToArray();
        var contours = streams[0]; var points = streams[1]; var flags = streams[2];
        var glyphs = streams[3]; var composites = streams[4]; var boxes = streams[5]; var instructions = streams[6];
        var overlap = (options & 1) != 0 ? reader.Take((glyphCount + 7) / 8).ToArray() : [];
        reader.End();
        var boxFlags = boxes.Take(((glyphCount + 31) / 32) * 4).ToArray();
        using var output = new Writer();
        using var locations = new Writer();
        for (var g = 0; g < glyphCount; g++)
        {
            locations.U32((uint)output.Length);
            var contourCount = contours.I16();
            var hasBox = (boxFlags[g / 8] & (0x80 >> (g & 7))) != 0;
            if (contourCount == 0) { Require(!hasBox); continue; }
            Require(contourCount >= -1);
            output.U16(unchecked((ushort)contourCount));
            if (contourCount == -1)
            {
                Require(hasBox);
                output.Write(boxes.Take(8));
                var haveInstructions = false;
                ushort componentFlags;
                do
                {
                    componentFlags = composites.U16();
                    var component = composites.U16();
                    Require(component < glyphCount);
                    output.U16(componentFlags); output.U16(component);
                    var size = (componentFlags & 1) != 0 ? 4 : 2;
                    if ((componentFlags & 8) != 0) size += 2;
                    else if ((componentFlags & 64) != 0) size += 4;
                    else if ((componentFlags & 128) != 0) size += 8;
                    output.Write(composites.Take(size));
                    haveInstructions |= (componentFlags & 256) != 0;
                } while ((componentFlags & 32) != 0);
                if (haveInstructions) CopyInstructions(output, glyphs, instructions);
            }
            else
            {
                var ends = new ushort[contourCount];
                var pointCount = 0;
                for (var c = 0; c < contourCount; c++)
                {
                    var n = points.U255();
                    Require(n > 0);
                    pointCount += n;
                    Require(pointCount <= 65536);
                    ends[c] = (ushort)(pointCount - 1);
                }
                var pointFlags = flags.Take(pointCount).ToArray();
                var xs = new short[pointCount]; var ys = new short[pointCount];
                var x = 0; var y = 0;
                for (var p = 0; p < pointCount; p++)
                {
                    var f = pointFlags[p] & 127;
                    int dx, dy;
                    var b = glyphs.Byte();
                    if (f < 10) { dx = 0; dy = Sign(f, ((f & 14) << 7) + b); }
                    else if (f < 20) { dx = Sign(f, (((f - 10) & 14) << 7) + b); dy = 0; }
                    else if (f < 84)
                    {
                        var t = f - 20;
                        dx = Sign(f, 1 + (t & 48) + (b >> 4));
                        dy = Sign(f >> 1, 1 + ((t & 12) << 2) + (b & 15));
                    }
                    else if (f < 120)
                    {
                        var t = f - 84;
                        dx = Sign(f, 1 + ((t / 12) << 8) + b);
                        dy = Sign(f >> 1, 1 + (((t % 12) >> 2) << 8) + glyphs.Byte());
                    }
                    else if (f < 124)
                    {
                        var b1 = glyphs.Byte();
                        dx = Sign(f, (b << 4) + (b1 >> 4));
                        dy = Sign(f >> 1, ((b1 & 15) << 8) + glyphs.Byte());
                    }
                    else { dx = Sign(f, (b << 8) + glyphs.Byte()); dy = Sign(f >> 1, glyphs.U16()); }
                    x = checked(x + dx); y = checked(y + dy);
                    xs[p] = checked((short)x); ys[p] = checked((short)y);
                }
                if (hasBox) output.Write(boxes.Take(8));
                else { output.I16(xs.Min()); output.I16(ys.Min()); output.I16(xs.Max()); output.I16(ys.Max()); }
                foreach (var end in ends) output.U16(end);
                CopyInstructions(output, glyphs, instructions);
                // Use explicit signed deltas. Compression is irrelevant after decoding;
                // retaining point order is essential for gvar, composites and hinting.
                for (var p = 0; p < pointCount; p++)
                    output.Byte((byte)(((pointFlags[p] & 128) == 0 ? 1 : 0)
                        | (p == 0 && overlap.Length != 0 && (overlap[g / 8] & (0x80 >> (g & 7))) != 0 ? 64 : 0)));
                for (var p = 0; p < pointCount; p++) output.I16(unchecked((short)(xs[p] - (p == 0 ? 0 : xs[p - 1]))));
                for (var p = 0; p < pointCount; p++) output.I16(unchecked((short)(ys[p] - (p == 0 ? 0 : ys[p - 1]))));
            }
            output.Pad4();
        }
        locations.U32((uint)output.Length);
        foreach (var stream in streams) stream.End();
        glyf.Data = output.ToArray(); loca.Data = locations.ToArray();
        var head = Get(tables, Head);
        Require(head.Length >= 54);
        BinaryPrimitives.WriteUInt16BigEndian(head.AsSpan(50), 1); // Long offsets permit uncompressed point flags.
    }

    private static void CopyInstructions(Writer output, Reader glyphs, Reader instructions)
    {
        var length = glyphs.U255();
        output.U16((ushort)length);
        output.Write(instructions.Take(length));
    }

    private static void ReconstructMetrics(Dictionary<uint, Table> tables, Table table)
    {
        var count = U16(Get(tables, Maxp), 4);
        var metrics = U16(Get(tables, Hhea), 34);
        Require(metrics > 0 && metrics <= count && table.OriginalLength == 2 * count + 2 * metrics);
        var glyf = Get(tables, Glyf); var loca = Get(tables, Loca);
        var format = U16(Get(tables, Head), 50);
        Require(format <= 1);
        var xmin = new short[count];
        for (var g = 0; g < count; g++)
        {
            var start = format == 0 ? U16(loca, g * 2) * 2u : U32(loca, g * 4);
            var end = format == 0 ? U16(loca, (g + 1) * 2) * 2u : U32(loca, (g + 1) * 4);
            Require(start <= end && end <= glyf.Length);
            if (end > start) { Require(end - start >= 10); xmin[g] = unchecked((short)U16(glyf, (int)start + 2)); }
        }
        var input = new Reader(table.Data);
        var flags = input.Byte();
        Require(flags is >= 1 and <= 3);
        var widths = new ushort[metrics];
        for (var g = 0; g < metrics; g++) widths[g] = input.U16();
        using var output = new Writer();
        for (var g = 0; g < count; g++)
        {
            if (g < metrics) output.U16(widths[g]);
            output.I16((flags & (g < metrics ? 1 : 2)) == 0 ? input.I16() : xmin[g]);
        }
        input.End();
        table.Data = output.ToArray();
    }

    private static byte[] BuildSfnt(uint flavor, Dictionary<uint, Table> tables)
    {
        var ordered = tables.Values.OrderBy(t => t.Tag).ToArray();
        var count = ordered.Length;
        var length = 12 + count * 16 + ordered.Sum(t => (long)((t.Data.Length + 3) & ~3));
        Require(length <= Limit);
        var output = new byte[(int)length];
        BinaryPrimitives.WriteUInt32BigEndian(output, flavor);
        BinaryPrimitives.WriteUInt16BigEndian(output.AsSpan(4), (ushort)count);
        var power = 0; while ((1 << (power + 1)) <= count) power++;
        BinaryPrimitives.WriteUInt16BigEndian(output.AsSpan(6), (ushort)((1 << power) * 16));
        BinaryPrimitives.WriteUInt16BigEndian(output.AsSpan(8), (ushort)power);
        BinaryPrimitives.WriteUInt16BigEndian(output.AsSpan(10), (ushort)((count - (1 << power)) * 16));
        var offset = 12 + count * 16;
        var headOffset = -1;
        for (var i = 0; i < count; i++)
        {
            var table = ordered[i];
            if (table.Tag == Head) { Require(table.Data.Length >= 54); table.Data.AsSpan(8, 4).Clear(); headOffset = offset; }
            var entry = output.AsSpan(12 + 16 * i, 16);
            BinaryPrimitives.WriteUInt32BigEndian(entry, table.Tag);
            BinaryPrimitives.WriteUInt32BigEndian(entry[4..], Checksum(table.Data));
            BinaryPrimitives.WriteUInt32BigEndian(entry[8..], (uint)offset);
            BinaryPrimitives.WriteUInt32BigEndian(entry[12..], (uint)table.Data.Length);
            table.Data.CopyTo(output, offset);
            offset += (table.Data.Length + 3) & ~3;
        }
        if (headOffset >= 0) BinaryPrimitives.WriteUInt32BigEndian(output.AsSpan(headOffset + 8), unchecked(0xb1b0afba - Checksum(output)));
        return output;
    }

    private static uint Checksum(ReadOnlySpan<byte> bytes)
    {
        uint sum = 0;
        for (var i = 0; i < bytes.Length; i++) sum = unchecked(sum + ((uint)bytes[i] << (24 - (i & 3) * 8)));
        return sum;
    }
    private static byte[] Get(Dictionary<uint, Table> tables, uint tag) => tables.TryGetValue(tag, out var table) ? table.Data : throw new InvalidDataException("Missing web-font table.");
    private static ushort U16(byte[] data, int offset) => BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(offset, 2));
    private static uint U32(byte[] data, int offset) => BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(offset, 4));
    private static uint Tag(string name) => (uint)(name[0] << 24 | name[1] << 16 | name[2] << 8 | (name[3] == '_' ? ' ' : name[3]));
    private static int Sign(int flag, int value) => (flag & 1) != 0 ? value : -value;
    private static void Require(bool valid) { if (!valid) throw new InvalidDataException("Invalid or unsupported web-font container."); }

    private sealed class Reader(byte[] data)
    {
        private int _offset;
        public ReadOnlySpan<byte> Take(int count)
        {
            Require(count >= 0 && count <= data.Length - _offset);
            var result = data.AsSpan(_offset, count); _offset += count; return result;
        }
        public byte Byte() => Take(1)[0];
        public ushort U16() => BinaryPrimitives.ReadUInt16BigEndian(Take(2));
        public short I16() => unchecked((short)U16());
        public uint U32() => BinaryPrimitives.ReadUInt32BigEndian(Take(4));
        public int Length32() { var n = U32(); Require(n <= Limit); return (int)n; }
        public int U255() { var n = Byte(); return n == 253 ? U16() : n == 254 ? Byte() + 506 : n == 255 ? Byte() + 253 : n; }
        public int Base128()
        {
            uint n = 0;
            for (var i = 0; i < 5; i++)
            {
                var b = Byte(); Require(!(i == 0 && b == 128) && (n & 0xfe000000) == 0);
                n = (n << 7) | (uint)(b & 127);
                if ((b & 128) == 0) { Require(n <= Limit); return (int)n; }
            }
            throw new InvalidDataException("Invalid UIntBase128.");
        }
        public void End() => Require(_offset == data.Length);
    }

    private sealed class Writer : MemoryStream
    {
        public override void Write(ReadOnlySpan<byte> data) { Require(Length + data.Length <= Limit); base.Write(data); }
        public void Byte(byte value) { Require(Length < Limit); WriteByte(value); }
        public void U16(ushort value) { Span<byte> bytes = stackalloc byte[2]; BinaryPrimitives.WriteUInt16BigEndian(bytes, value); Write(bytes); }
        public void I16(short value) => U16(unchecked((ushort)value));
        public void U32(uint value) { Span<byte> bytes = stackalloc byte[4]; BinaryPrimitives.WriteUInt32BigEndian(bytes, value); Write(bytes); }
        public void Pad4() { while ((Length & 3) != 0) Byte(0); }
    }
}
