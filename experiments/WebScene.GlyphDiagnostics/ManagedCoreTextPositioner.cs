using System.Runtime.InteropServices;

internal static class ManagedCoreTextPositioner
{
    internal static CoreTextRunMetrics[] Shape(Configuration configuration)
    {
        if (!OperatingSystem.IsMacOS())
        {
            throw new PlatformNotSupportedException("CoreText positioning is only available on macOS.");
        }

        var fontAttribute = CoreTextAttribute("kCTFontAttributeName");
        var kernAttribute = CoreTextAttribute("kCTKernAttributeName");
        var zero = 0d;
        var kernValue = CoreFoundation.CFNumberCreate(
            IntPtr.Zero,
            CoreFoundation.NumberDouble,
            ref zero);
        if (kernValue == IntPtr.Zero)
        {
            throw new InvalidOperationException("Could not create the CoreText kern value.");
        }

        try
        {
            var results = new List<CoreTextRunMetrics>(configuration.Cases.Length);
            foreach (var item in configuration.Cases)
            {
                results.Add(Shape(item, fontAttribute, kernAttribute, kernValue));
            }
            return results.ToArray();
        }
        finally
        {
            CoreFoundation.CFRelease(kernValue);
        }
    }

    private static CoreTextRunMetrics Shape(
        GlyphCase item,
        IntPtr fontAttribute,
        IntPtr kernAttribute,
        IntPtr kernValue)
    {
        var fontClass = ObjectiveC.objc_getClass("NSFont");
        var selector = ObjectiveC.sel_registerName("systemFontOfSize:weight:");
        var font = ObjectiveC.objc_msgSend_double_double(
            fontClass,
            selector,
            item.Size,
            SystemWeight(item.Weight));
        if (font == IntPtr.Zero)
        {
            throw new InvalidOperationException($"Could not create the system font for '{item.Id}'.");
        }
        font = ObjectiveC.objc_retain(font);

        var text = CoreFoundation.CFStringCreateWithCharacters(
            IntPtr.Zero,
            item.Text,
            item.Text.Length);
        var attributes = CoreFoundation.CFDictionaryCreate(
            IntPtr.Zero,
            [fontAttribute, kernAttribute],
            [font, kernValue],
            2,
            IntPtr.Zero,
            IntPtr.Zero);
        var attributed = attributes == IntPtr.Zero || text == IntPtr.Zero
            ? IntPtr.Zero
            : CoreFoundation.CFAttributedStringCreate(
                IntPtr.Zero,
                text,
                attributes);
        var line = attributed == IntPtr.Zero
            ? IntPtr.Zero
            : CoreText.CTLineCreateWithAttributedString(attributed);
        try
        {
            if (line == IntPtr.Zero)
            {
                throw new InvalidOperationException($"Could not create the CoreText line for '{item.Id}'.");
            }

            var glyphs = new List<uint>();
            var positions = new List<float[]>();
            var runs = CoreText.CTLineGetGlyphRuns(line);
            var runCount = checked((int)CoreFoundation.CFArrayGetCount(runs));
            for (var runIndex = 0; runIndex < runCount; runIndex++)
            {
                var run = CoreFoundation.CFArrayGetValueAtIndex(runs, runIndex);
                var count = checked((int)CoreText.CTRunGetGlyphCount(run));
                var runGlyphs = new ushort[count];
                var runPositions = new CGPoint[count];
                CoreText.CTRunGetGlyphs(run, default, runGlyphs);
                CoreText.CTRunGetPositions(run, default, runPositions);
                for (var index = 0; index < count; index++)
                {
                    glyphs.Add(runGlyphs[index]);
                    positions.Add([
                        checked((float)runPositions[index].X),
                        checked((float)runPositions[index].Y)
                    ]);
                }
            }
            return new CoreTextRunMetrics(item.Id, glyphs.ToArray(), positions.ToArray());
        }
        finally
        {
            if (line != IntPtr.Zero) CoreFoundation.CFRelease(line);
            if (attributed != IntPtr.Zero) CoreFoundation.CFRelease(attributed);
            if (attributes != IntPtr.Zero) CoreFoundation.CFRelease(attributes);
            if (text != IntPtr.Zero) CoreFoundation.CFRelease(text);
            ObjectiveC.objc_release(font);
        }
    }

    private static IntPtr CoreTextAttribute(string symbol)
    {
        var library = NativeLibrary.Load(CoreText.Library);
        try
        {
            return Marshal.ReadIntPtr(NativeLibrary.GetExport(library, symbol));
        }
        finally
        {
            NativeLibrary.Free(library);
        }
    }

    private static double SystemWeight(int weight)
        => weight switch
        {
            <= 149 => -0.8,
            <= 249 => -0.6,
            <= 349 => -0.4,
            <= 449 => 0,
            <= 549 => 0.23,
            <= 649 => 0.3,
            <= 749 => 0.4,
            <= 849 => 0.56,
            _ => 0.62
        };

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct CGPoint(double X, double Y);

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct CFRange(nint Location, nint Length);

    private static class ObjectiveC
    {
        private const string Library = "/usr/lib/libobjc.A.dylib";

        [DllImport(Library)]
        internal static extern IntPtr objc_getClass(string name);

        [DllImport(Library)]
        internal static extern IntPtr sel_registerName(string name);

        [DllImport(Library, EntryPoint = "objc_msgSend")]
        internal static extern IntPtr objc_msgSend_double_double(
            IntPtr receiver,
            IntPtr selector,
            double value1,
            double value2);

        [DllImport(Library)]
        internal static extern IntPtr objc_retain(IntPtr value);

        [DllImport(Library)]
        internal static extern void objc_release(IntPtr value);
    }

    private static class CoreFoundation
    {
        private const string Library = "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation";
        internal const int NumberDouble = 6;

        [DllImport(Library)]
        internal static extern void CFRelease(IntPtr value);

        [DllImport(Library)]
        internal static extern IntPtr CFStringCreateWithCharacters(
            IntPtr allocator,
            [MarshalAs(UnmanagedType.LPWStr)] string characters,
            nint length);

        [DllImport(Library)]
        internal static extern IntPtr CFNumberCreate(
            IntPtr allocator,
            int type,
            ref double value);

        [DllImport(Library)]
        internal static extern IntPtr CFDictionaryCreate(
            IntPtr allocator,
            IntPtr[] keys,
            IntPtr[] values,
            nint count,
            IntPtr keyCallbacks,
            IntPtr valueCallbacks);

        [DllImport(Library)]
        internal static extern IntPtr CFAttributedStringCreate(
            IntPtr allocator,
            IntPtr value,
            IntPtr attributes);

        [DllImport(Library)]
        internal static extern nint CFArrayGetCount(IntPtr array);

        [DllImport(Library)]
        internal static extern IntPtr CFArrayGetValueAtIndex(IntPtr array, nint index);
    }

    private static class CoreText
    {
        internal const string Library = "/System/Library/Frameworks/CoreText.framework/CoreText";

        [DllImport(Library)]
        internal static extern IntPtr CTLineCreateWithAttributedString(IntPtr attributedString);

        [DllImport(Library)]
        internal static extern IntPtr CTLineGetGlyphRuns(IntPtr line);

        [DllImport(Library)]
        internal static extern nint CTRunGetGlyphCount(IntPtr run);

        [DllImport(Library)]
        internal static extern void CTRunGetGlyphs(
            IntPtr run,
            CFRange range,
            [Out] ushort[] glyphs);

        [DllImport(Library)]
        internal static extern void CTRunGetPositions(
            IntPtr run,
            CFRange range,
            [Out] CGPoint[] positions);
    }
}
