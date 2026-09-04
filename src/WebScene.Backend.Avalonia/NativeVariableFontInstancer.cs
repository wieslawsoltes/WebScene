using System.Buffers.Binary;
using System.Runtime.InteropServices;
using SkiaSharp;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

// Uses the HarfBuzz already shipped by the presenter. No Skia native ABI changes.
internal static unsafe class NativeVariableFontInstancer
{
    private const uint Fvar = 0x66766172;
    private const uint Weight = 0x77676874;
    internal readonly record struct WeightAxis(float Minimum, float Default, float Maximum);

    internal static WeightAxis? ReadWeightAxis(SKTypeface face)
    {
        if (face.GetTableSize(Fvar) == 0) return null;
        var data = face.GetTableData(Fvar);
        if (data is null || data.Length < 16) return null;
        var offset = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(4));
        var count = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(8));
        var size = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(10));
        if (size < 20 || offset + (long)count * size > data.Length) return null;
        for (var i = 0; i < count; i++)
        {
            var axis = data.AsSpan(offset + i * size, size);
            if (BinaryPrimitives.ReadUInt32BigEndian(axis) != Weight) continue;
            var min = BinaryPrimitives.ReadInt32BigEndian(axis[4..]) / 65536f;
            var normal = BinaryPrimitives.ReadInt32BigEndian(axis[8..]) / 65536f;
            var max = BinaryPrimitives.ReadInt32BigEndian(axis[12..]) / 65536f;
            return min <= normal && normal <= max ? new(min, normal, max) : null;
        }
        return null;
    }

    internal static byte[] Instantiate(SKTypeface source, float weight)
    {
        var face = hb_face_builder_create();
        IntPtr input = IntPtr.Zero, result = IntPtr.Zero, blob = IntPtr.Zero;
        try
        {
            input = hb_subset_input_create_or_fail();
            if (face == IntPtr.Zero || input == IntPtr.Zero)
                throw new InvalidOperationException("Could not allocate font instancing input.");
            // Skia supplies decoded SFNT tables, including for WOFF/WOFF2 input.
            // A face builder also preserves the full table directory for HarfBuzz.
            foreach (var tag in source.GetTableTags())
            {
                if (source.GetTableSize(tag) == 0) continue;
                var table = source.GetTableData(tag);
                fixed (byte* tablePointer = table)
                {
                    var tableBlob = hb_blob_create((IntPtr)tablePointer, (uint)table.Length, 0, IntPtr.Zero, IntPtr.Zero);
                    try
                    {
                        if (hb_face_builder_add_table(face, tag, tableBlob) == 0)
                            throw new InvalidOperationException("Could not copy font table.");
                    }
                    finally { hb_blob_destroy(tableBlob); }
                }
            }
            hb_subset_input_keep_everything(input);
            // Keep all glyph IDs, names, features, scripts, hinting and unknown tables.
            hb_subset_input_set_flags(input, 0x2 | 0x8 | 0x20 | 0x40 | 0x80 | 0x100);
            if (hb_subset_input_pin_all_axes_to_default(input, face) == 0
                || hb_subset_input_pin_axis_location(input, face, Weight, weight) == 0)
                throw new InvalidOperationException("Could not pin variable-font axes.");
            result = hb_subset_or_fail(face, input);
            if (result == IntPtr.Zero) throw new InvalidOperationException("HarfBuzz font instantiation failed.");
            blob = hb_face_reference_blob(result);
            var pointer = hb_blob_get_data(blob, out var length);
            if (pointer == IntPtr.Zero || length == 0 || length > 64 * 1024 * 1024)
                throw new InvalidOperationException("Invalid or oversized instantiated font.");
            var bytes = new byte[checked((int)length)];
            Marshal.Copy(pointer, bytes, 0, bytes.Length);
            return bytes;
        }
        finally
        {
            if (blob != IntPtr.Zero) hb_blob_destroy(blob);
            if (result != IntPtr.Zero) hb_face_destroy(result);
            if (input != IntPtr.Zero) hb_subset_input_destroy(input);
            if (face != IntPtr.Zero) hb_face_destroy(face);
            GC.KeepAlive(source);
        }
    }

    private const string Library = "libHarfBuzzSharp";
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr hb_face_builder_create();
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern int hb_face_builder_add_table(IntPtr face, uint tag, IntPtr blob);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern void hb_face_destroy(IntPtr face);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr hb_face_reference_blob(IntPtr face);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr hb_blob_create(IntPtr data, uint length, int mode, IntPtr userData, IntPtr destroy);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr hb_blob_get_data(IntPtr blob, out uint length);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern void hb_blob_destroy(IntPtr blob);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr hb_subset_input_create_or_fail();
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern void hb_subset_input_destroy(IntPtr input);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern void hb_subset_input_keep_everything(IntPtr input);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern void hb_subset_input_set_flags(IntPtr input, uint flags);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern int hb_subset_input_pin_all_axes_to_default(IntPtr input, IntPtr face);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern int hb_subset_input_pin_axis_location(IntPtr input, IntPtr face, uint tag, float value);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr hb_subset_or_fail(IntPtr face, IntPtr input);
}
