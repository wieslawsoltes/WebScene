using System.Runtime.InteropServices;
using System.Security.Cryptography;
using SkiaSharp;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

internal sealed unsafe class WindowsDirectWriteRunPositioner : INativeTextRunPositioner
{
    private const int MaximumCachedRuns = 2048;
    private const int MaximumCachedFaces = 64;
    private const int MaximumEligibleTextLength = 4096;
    private const int ENotSufficientBuffer = unchecked((int)0x8007007A);
    private static readonly Guid FactoryInterfaceId =
        new("B859EE5A-D838-4B5B-A2E8-1ADC7D93DB48");
    private static readonly Lazy<DirectWriteState> SharedState =
        new(CreateState, LazyThreadSafetyMode.ExecutionAndPublication);

    private readonly object _cacheGate = new();
    private readonly Dictionary<RunKey, NativePositionedTextRun> _runs = [];
    private readonly Queue<RunKey> _runOrder = [];
    private readonly Dictionary<FaceKey, FaceState> _faces = [];

    public bool IsEligible(in NativeTextRunPositionRequest request)
        => OperatingSystem.IsWindows()
            && request.Typeface is not null
            && request.Slant == SKFontStyleSlant.Upright
            && request.FeatureFlags == 0
            // The Windows oracle currently proves regular weight at every
            // required scale. Semibold and bold remain visible in the corpus
            // but fall back until their fractional-scale regressions are gone.
            && request.FontWeight == 400
            && request.Text.Length is > 0 and <= MaximumEligibleTextLength
            && NativeTextShaping.UsesWindowsSystemUiMetrics(
                request.FamilyList,
                request.WebTypefaces)
            && NativeTextShaping.UsesMacSystemUiPlatformAdvances(request.Text);

    public bool TryPosition(
        in NativeTextRunPositionRequest request,
        out NativePositionedTextRun run)
    {
        run = null!;
        if (!IsEligible(in request)) return false;

        var typeface = request.Typeface!;
        var key = new RunKey(
            request.Text,
            request.FontSize,
            request.FontWeight,
            request.FamilyList);
        lock (_cacheGate)
        {
            if (_runs.TryGetValue(key, out var cached))
            {
                if (!GlyphsMatch(cached.Glyphs, request.ExpectedGlyphs)) return false;
                run = cached;
                return true;
            }
        }

        NativePositionedTextRun positioned;
        try
        {
            using var face = GetFace(typeface, request.FontWeight);
            positioned = Shape(
                SharedState.Value.Analyzer,
                face.State,
                request.Text,
                request.FontSize);
        }
        catch (Exception error) when (error is not OutOfMemoryException)
        {
            return false;
        }
        if (!GlyphsMatch(positioned.Glyphs, request.ExpectedGlyphs)) return false;

        lock (_cacheGate)
        {
            if (_runs.TryGetValue(key, out var cached))
            {
                run = cached;
                return GlyphsMatch(cached.Glyphs, request.ExpectedGlyphs);
            }
            if (_runs.Count >= MaximumCachedRuns)
            {
                _runs.Remove(_runOrder.Dequeue());
            }
            _runs.Add(key, positioned);
            _runOrder.Enqueue(key);
        }
        run = positioned;
        return true;
    }

    private FaceLease GetFace(SKTypeface typeface, int requestedWeight)
    {
        var family = typeface.FamilyName
            ?? throw new InvalidOperationException("Skia did not expose a font family name.");
        var key = new FaceKey(family, requestedWeight);
        lock (_cacheGate)
        {
            if (_faces.TryGetValue(key, out var cached))
            {
                ValidateSkiaIdentity(cached, typeface);
                return new FaceLease(cached, release: false);
            }
        }

        var created = CreateFace(SharedState.Value.Collection, family, requestedWeight);
        try
        {
            ValidateSkiaIdentity(created, typeface);
            lock (_cacheGate)
            {
                if (_faces.TryGetValue(key, out var cached))
                {
                    ValidateSkiaIdentity(cached, typeface);
                    Release(created.Face);
                    return new FaceLease(cached, release: false);
                }
                if (_faces.Count < MaximumCachedFaces)
                {
                    _faces.Add(key, created);
                    return new FaceLease(created, release: false);
                }
            }
            return new FaceLease(created, release: true);
        }
        catch
        {
            Release(created.Face);
            throw;
        }
    }

    private static DirectWriteState CreateState()
    {
        var factoryId = FactoryInterfaceId;
        ThrowIfFailed(DWriteCreateFactory(0, in factoryId, out var factory));
        IntPtr collection = IntPtr.Zero;
        IntPtr analyzer = IntPtr.Zero;
        try
        {
            var factoryVtable = GetVtable(factory);
            ThrowIfFailed(((delegate* unmanaged[Stdcall]<void*, void**, int, int>)factoryVtable[3])(
                (void*)factory,
                (void**)&collection,
                0));
            ThrowIfFailed(((delegate* unmanaged[Stdcall]<void*, void**, int>)factoryVtable[21])(
                (void*)factory,
                (void**)&analyzer));
            return new DirectWriteState(factory, collection, analyzer);
        }
        catch
        {
            Release(analyzer);
            Release(collection);
            Release(factory);
            throw;
        }
    }

    private static FaceState CreateFace(
        IntPtr collection,
        string familyName,
        int requestedWeight)
    {
        IntPtr family = IntPtr.Zero;
        IntPtr font = IntPtr.Zero;
        IntPtr face = IntPtr.Zero;
        try
        {
            uint familyIndex;
            int exists;
            fixed (char* familyNamePointer = familyName)
            {
                ThrowIfFailed(((delegate* unmanaged[Stdcall]<void*, char*, uint*, int*, int>)
                    GetVtable(collection)[5])(
                    (void*)collection,
                    familyNamePointer,
                    &familyIndex,
                    &exists));
            }
            if (exists == 0)
            {
                throw new InvalidOperationException(
                    $"DirectWrite did not find the Skia family '{familyName}'.");
            }
            ThrowIfFailed(((delegate* unmanaged[Stdcall]<void*, uint, void**, int>)
                GetVtable(collection)[4])((void*)collection, familyIndex, (void**)&family));
            ThrowIfFailed(((delegate* unmanaged[Stdcall]<void*, uint, uint, uint, void**, int>)
                GetVtable(family)[7])(
                (void*)family,
                checked((uint)requestedWeight),
                5,
                0,
                (void**)&font));
            var fontVtable = GetVtable(font);
            var actualWeight = checked((int)((delegate* unmanaged[Stdcall]<void*, uint>)
                fontVtable[4])((void*)font));
            var stretch = checked((int)((delegate* unmanaged[Stdcall]<void*, uint>)
                fontVtable[5])((void*)font));
            var style = checked((int)((delegate* unmanaged[Stdcall]<void*, uint>)
                fontVtable[6])((void*)font));
            ThrowIfFailed(((delegate* unmanaged[Stdcall]<void*, void**, int>)fontVtable[13])(
                (void*)font,
                (void**)&face));
            var fingerprint = GetDirectWriteFingerprint(face);
            return new FaceState(
                face,
                new NativeTextRunFaceIdentity(
                    familyName,
                    actualWeight,
                    stretch,
                    style,
                    fingerprint));
        }
        finally
        {
            Release(font);
            Release(family);
        }
    }

    private static void ValidateSkiaIdentity(FaceState face, SKTypeface typeface)
    {
        if (!string.Equals(
                face.Identity.FamilyName,
                typeface.FamilyName,
                StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("DirectWrite and Skia selected different families.");
        }
        var skiaFingerprint = GetSkiaFingerprint(typeface);
        if (!string.Equals(
                face.Identity.FontTableFingerprint,
                skiaFingerprint,
                StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                "DirectWrite and Skia selected different physical font faces.");
        }
    }

    private static NativePositionedTextRun Shape(
        IntPtr analyzer,
        FaceState face,
        string text,
        float fontSize)
    {
        var glyphCapacity = checked(text.Length * 2 + 16);
        while (true)
        {
            var clusterMap = new ushort[text.Length];
            var textProperties = new ushort[text.Length];
            var glyphs = new ushort[glyphCapacity];
            var glyphProperties = new ushort[glyphCapacity];
            uint actualGlyphCount = 0;
            var analysis = new ScriptAnalysis(0, 0);
            int result;
            fixed (char* textPointer = text)
            fixed (ushort* clusterPointer = clusterMap)
            fixed (ushort* textPropertiesPointer = textProperties)
            fixed (ushort* glyphPointer = glyphs)
            fixed (ushort* glyphPropertiesPointer = glyphProperties)
            {
                result = ((delegate* unmanaged[Stdcall]<
                    void*, char*, uint, void*, int, int, ScriptAnalysis*, char*, void*,
                    void*, uint*, uint, uint, ushort*, ushort*, ushort*, ushort*, uint*, int>)
                    GetVtable(analyzer)[7])(
                    (void*)analyzer,
                    textPointer,
                    checked((uint)text.Length),
                    (void*)face.Face,
                    0,
                    0,
                    &analysis,
                    null,
                    null,
                    null,
                    null,
                    0,
                    checked((uint)glyphCapacity),
                    clusterPointer,
                    textPropertiesPointer,
                    glyphPointer,
                    glyphPropertiesPointer,
                    &actualGlyphCount);
            }
            if (result == ENotSufficientBuffer)
            {
                glyphCapacity = checked(glyphCapacity * 2);
                continue;
            }
            ThrowIfFailed(result);

            Array.Resize(ref glyphs, checked((int)actualGlyphCount));
            Array.Resize(ref glyphProperties, glyphs.Length);
            var advances = new float[glyphs.Length];
            var nativeOffsets = new GlyphOffset[glyphs.Length];
            fixed (char* textPointer = text)
            fixed (ushort* clusterPointer = clusterMap)
            fixed (ushort* textPropertiesPointer = textProperties)
            fixed (ushort* glyphPointer = glyphs)
            fixed (ushort* glyphPropertiesPointer = glyphProperties)
            fixed (float* advancesPointer = advances)
            fixed (GlyphOffset* offsetsPointer = nativeOffsets)
            {
                ThrowIfFailed(((delegate* unmanaged[Stdcall]<
                    void*, char*, ushort*, ushort*, uint, ushort*, ushort*, uint, void*, float,
                    int, int, ScriptAnalysis*, char*, void*, uint*, uint, float*, GlyphOffset*, int>)
                    GetVtable(analyzer)[8])(
                    (void*)analyzer,
                    textPointer,
                    clusterPointer,
                    textPropertiesPointer,
                    checked((uint)text.Length),
                    glyphPointer,
                    glyphPropertiesPointer,
                    actualGlyphCount,
                    (void*)face.Face,
                    fontSize,
                    0,
                    0,
                    &analysis,
                    null,
                    null,
                    null,
                    0,
                    advancesPointer,
                    offsetsPointer));
            }

            var positions = new SKPoint[glyphs.Length];
            var offsets = new SKPoint[glyphs.Length];
            var x = 0f;
            for (var index = 0; index < glyphs.Length; index++)
            {
                offsets[index] = new SKPoint(
                    nativeOffsets[index].AdvanceOffset,
                    -nativeOffsets[index].AscenderOffset);
                positions[index] = new SKPoint(
                    x + offsets[index].X,
                    offsets[index].Y);
                x += advances[index];
            }
            if (!float.IsFinite(x) || x < 0)
            {
                throw new InvalidOperationException("DirectWrite returned an invalid run width.");
            }
            return new NativePositionedTextRun(
                glyphs,
                positions,
                x,
                ResolveGlyphClusters(clusterMap, glyphs.Length),
                advances,
                offsets,
                face.Identity);
        }
    }

    private static uint[] ResolveGlyphClusters(ushort[] textClusters, int glyphCount)
    {
        var clusters = Enumerable.Repeat(uint.MaxValue, glyphCount).ToArray();
        for (var textIndex = 0; textIndex < textClusters.Length; textIndex++)
        {
            var glyphIndex = textClusters[textIndex];
            if (glyphIndex < clusters.Length && clusters[glyphIndex] == uint.MaxValue)
            {
                clusters[glyphIndex] = checked((uint)textIndex);
            }
        }
        uint previous = 0;
        for (var index = 0; index < clusters.Length; index++)
        {
            if (clusters[index] == uint.MaxValue) clusters[index] = previous;
            else previous = clusters[index];
        }
        return clusters;
    }

    private static string GetDirectWriteFingerprint(IntPtr face)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendDirectWriteTable(hash, face, 0x64616568); // head
        AppendDirectWriteTable(hash, face, 0x656D616E); // name
        return Convert.ToHexString(hash.GetHashAndReset());
    }

    private static void AppendDirectWriteTable(
        IncrementalHash hash,
        IntPtr face,
        uint directWriteTag)
    {
        void* data = null;
        uint size = 0;
        void* context = null;
        int exists = 0;
        ThrowIfFailed(((delegate* unmanaged[Stdcall]<
            void*, uint, void**, uint*, void**, int*, int>)GetVtable(face)[12])(
            (void*)face,
            directWriteTag,
            &data,
            &size,
            &context,
            &exists));
        try
        {
            if (exists == 0 || data == null || size == 0)
                throw new InvalidOperationException("DirectWrite did not expose a required font table.");
            hash.AppendData(new ReadOnlySpan<byte>(data, checked((int)size)));
        }
        finally
        {
            if (context != null)
            {
                ((delegate* unmanaged[Stdcall]<void*, void*, void>)GetVtable(face)[13])(
                    (void*)face,
                    context);
            }
        }
    }

    private static string GetSkiaFingerprint(SKTypeface typeface)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendSkiaTable(hash, typeface, 0x68656164); // head
        AppendSkiaTable(hash, typeface, 0x6E616D65); // name
        return Convert.ToHexString(hash.GetHashAndReset());
    }

    private static void AppendSkiaTable(
        IncrementalHash hash,
        SKTypeface typeface,
        uint skiaTag)
    {
        var data = typeface.GetTableData(skiaTag);
        if (data is null || data.Length == 0)
            throw new InvalidOperationException("Skia did not expose a required font table.");
        hash.AppendData(data);
    }

    private static bool GlyphsMatch(ushort[] actual, uint[] expected)
    {
        if (actual.Length != expected.Length) return false;
        for (var index = 0; index < actual.Length; index++)
        {
            if (actual[index] != expected[index]) return false;
        }
        return true;
    }

    private static void ThrowIfFailed(int result)
    {
        if (result < 0) Marshal.ThrowExceptionForHR(result);
    }

    private static void** GetVtable(IntPtr instance)
        => instance == IntPtr.Zero
            ? throw new InvalidOperationException("DirectWrite returned a null COM interface.")
            : *(void***)instance;

    private static void Release(IntPtr instance)
    {
        if (instance == IntPtr.Zero) return;
        ((delegate* unmanaged[Stdcall]<void*, uint>)GetVtable(instance)[2])((void*)instance);
    }

    [DllImport("dwrite.dll", ExactSpelling = true)]
    private static extern int DWriteCreateFactory(
        uint factoryType,
        in Guid interfaceId,
        out IntPtr factory);

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct ScriptAnalysis(ushort Script, ushort Shapes);

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct GlyphOffset(float AdvanceOffset, float AscenderOffset);

    private readonly record struct RunKey(
        string Text,
        float FontSize,
        int FontWeight,
        string FamilyName);

    private readonly record struct FaceKey(string FamilyName, int FontWeight);
    private sealed record DirectWriteState(IntPtr Factory, IntPtr Collection, IntPtr Analyzer);
    private sealed record FaceState(IntPtr Face, NativeTextRunFaceIdentity Identity);

    private readonly struct FaceLease(FaceState state, bool release) : IDisposable
    {
        internal FaceState State { get; } = state;

        public void Dispose()
        {
            if (release) Release(State.Face);
        }
    }
}
