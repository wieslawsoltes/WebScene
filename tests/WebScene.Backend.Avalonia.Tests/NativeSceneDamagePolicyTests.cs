using Avalonia;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeSceneDamagePolicyTests
{
    [Fact]
    public void CoalescedScenesInvalidateBothSeparatedChanges()
    {
        var first = new NativeSceneDamage(true, false, new Rect(10, 20, 30, 40), 1, 1200);
        var following = new NativeSceneDamage(true, false, new Rect(200, 100, 20, 10), 1, 200);
        var combined = first.Combine(following);
        Assert.True(combined.RequiresRender);
        Assert.False(combined.IsFull);
        Assert.Equal(new Rect(10, 20, 210, 90), combined.Bounds);
        Assert.Equal(2, combined.RectangleCount);
        Assert.Equal(1400, combined.SummedArea);
    }

    [Fact]
    public void UnchangedFollowingSceneCannotEraseEarlierDamage()
    {
        var changed = new NativeSceneDamage(true, false, new Rect(80, 90, 10, 20), 1, 200);
        Assert.Equal(changed, changed.Combine(NativeSceneDamage.None));
        Assert.Equal(changed, NativeSceneDamage.None.Combine(changed));
        Assert.Equal(NativeSceneDamage.None, NativeSceneDamage.None.Combine(NativeSceneDamage.None));
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void FullDamageSurvivesEitherSceneOrder(bool fullFirst)
    {
        var full = new NativeSceneDamage(true, true, default, 0, 0);
        var local = new NativeSceneDamage(true, false, new Rect(80, 90, 10, 20), 1, 200);
        var combined = fullFirst ? full.Combine(local) : local.Combine(full);
        Assert.True(combined.RequiresRender);
        Assert.True(combined.IsFull);
    }

    private const uint SceneCheckpoint = 1;
    private const uint SceneDomReplacement = 2;

    [Fact]
    public void LocalizedDomReplacementPreservesNativeDamage()
    {
        var header = Header(flags: SceneDomReplacement, damageCount: 1);
        NativeDamageRect[] damage =
        [
            new() { X = 40, Y = 30, Width = 20, Height = 10 }
        ];

        var result = NativeSceneDamagePolicy.Evaluate(
            in header,
            damage,
            damageBufferValid: true,
            viewportChanged: false,
            new Size(200, 100));

        Assert.True(result.RequiresRender);
        Assert.False(result.IsFull);
        Assert.Equal(new Rect(40, 30, 20, 10), result.Bounds);
        Assert.Equal(1, result.RectangleCount);
        Assert.Equal(200, result.SummedArea);
    }

    [Fact]
    public void DamageIsUnionedAndClampedToEffectiveSize()
    {
        var header = Header(damageCount: 2);
        NativeDamageRect[] damage =
        [
            new() { X = -5, Y = 10, Width = 15, Height = 10 },
            new() { X = 80, Y = 40, Width = 30, Height = 20 }
        ];

        var result = NativeSceneDamagePolicy.Evaluate(
            in header,
            damage,
            damageBufferValid: true,
            viewportChanged: false,
            new Size(200, 100));

        Assert.False(result.IsFull);
        Assert.Equal(new Rect(0, 10, 110, 50), result.Bounds);
        Assert.Equal(2, result.RectangleCount);
        Assert.Equal(700, result.SummedArea);
    }

    [Theory]
    [InlineData(SceneCheckpoint, 0, false)]
    [InlineData(0, 0, true)]
    [InlineData(SceneDomReplacement, 0, false)]
    public void StructuralChangesWithoutSafeDamageInvalidateFullSurface(
        uint flags,
        uint canvasLayerCount,
        bool viewportChanged)
    {
        var header = Header(
            flags: flags,
            canvasLayerCount: canvasLayerCount,
            damageCount: 0);

        var result = NativeSceneDamagePolicy.Evaluate(
            in header,
            [],
            damageBufferValid: true,
            viewportChanged,
            new Size(200, 100));

        Assert.True(result.RequiresRender);
        Assert.True(result.IsFull);
        Assert.Equal(new Rect(0, 0, 200, 100), result.Bounds);
    }

    [Fact]
    public void EmptyIncrementalDiffDoesNotRender()
    {
        var header = Header();

        var result = NativeSceneDamagePolicy.Evaluate(
            in header,
            [],
            damageBufferValid: true,
            viewportChanged: false,
            new Size(200, 100));

        Assert.False(result.RequiresRender);
    }

    [Fact]
    public void MissingOrMalformedDamageFallsBackToFullSurface()
    {
        var header = Header(damageCount: 1);

        var missing = NativeSceneDamagePolicy.Evaluate(
            in header,
            [],
            damageBufferValid: false,
            viewportChanged: false,
            new Size(200, 100));
        NativeDamageRect[] malformed =
        [
            new()
            {
                X = float.NaN,
                Y = 0,
                Width = 10,
                Height = 10
            }
        ];
        var invalid = NativeSceneDamagePolicy.Evaluate(
            in header,
            malformed,
            damageBufferValid: true,
            viewportChanged: false,
            new Size(200, 100));

        Assert.True(missing.IsFull);
        Assert.True(invalid.IsFull);
    }

    private static SceneHeader Header(
        uint flags = 0,
        uint canvasLayerCount = 0,
        uint damageCount = 0)
        => new()
        {
            Revision = 2,
            BaseRevision = 1,
            ViewportWidth = 100,
            ViewportHeight = 50,
            CanvasLayerCount = canvasLayerCount,
            DamageRectCount = damageCount,
            Flags = flags
        };
}
