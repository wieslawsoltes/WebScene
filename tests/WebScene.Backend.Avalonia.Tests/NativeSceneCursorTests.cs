using Avalonia.Input;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeSceneCursorTests
{
    [Theory]
    [InlineData(0, StandardCursorType.Arrow)]
    [InlineData(1, StandardCursorType.Hand)]
    [InlineData(8, StandardCursorType.SizeWestEast)]
    [InlineData(9, StandardCursorType.SizeNorthSouth)]
    public void NativeCursorKindMapsToAvaloniaCursor(int kind, StandardCursorType expected)
    {
        Assert.Equal(expected, NativeSceneSurface.CursorTypeForKind(kind));
    }
}
