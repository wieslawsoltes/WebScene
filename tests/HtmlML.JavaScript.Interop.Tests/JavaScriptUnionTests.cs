using System.Text.Json;
using HtmlML.JavaScript.Interop;
using Xunit;

namespace HtmlML.JavaScript.Interop.Tests;

public sealed class JavaScriptUnionTests
{
    [Fact]
    public void Nine_branch_union_round_trips_the_matching_shape()
    {
        JavaScriptUnion<
            Branch1,
            Branch2,
            Branch3,
            Branch4,
            Branch5,
            Branch6,
            Branch7,
            Branch8,
            Branch9> value = new Branch9 { Nine = "matched" };

        var json = JsonSerializer.Serialize(value);
        var roundTrip = JsonSerializer.Deserialize<JavaScriptUnion<
            Branch1,
            Branch2,
            Branch3,
            Branch4,
            Branch5,
            Branch6,
            Branch7,
            Branch8,
            Branch9>>(json);

        Assert.True(roundTrip.TryGet<Branch9>(out var branch));
        Assert.Equal("matched", branch!.Nine);
    }

    [Fact]
    public void Arbitrarily_wide_union_round_trips_the_matching_shape()
    {
        var value = new JavaScriptUnion<(
            Branch1,
            Branch2,
            Branch3,
            Branch4,
            Branch5,
            Branch6,
            Branch7,
            Branch8,
            Branch9,
            Branch10,
            Branch11,
            Branch12,
            Branch13,
            Branch14,
            Branch15,
            Branch16,
            Branch17)>(new Branch17 { Seventeen = "matched" });

        var json = JsonSerializer.Serialize(value);
        var roundTrip = JsonSerializer.Deserialize<JavaScriptUnion<(
            Branch1,
            Branch2,
            Branch3,
            Branch4,
            Branch5,
            Branch6,
            Branch7,
            Branch8,
            Branch9,
            Branch10,
            Branch11,
            Branch12,
            Branch13,
            Branch14,
            Branch15,
            Branch16,
            Branch17)>>(json);

        Assert.True(roundTrip.TryGet<Branch17>(out var branch));
        Assert.Equal("matched", branch!.Seventeen);
    }

    private sealed record Branch1 { public required string One { get; init; } }
    private sealed record Branch2 { public required string Two { get; init; } }
    private sealed record Branch3 { public required string Three { get; init; } }
    private sealed record Branch4 { public required string Four { get; init; } }
    private sealed record Branch5 { public required string Five { get; init; } }
    private sealed record Branch6 { public required string Six { get; init; } }
    private sealed record Branch7 { public required string Seven { get; init; } }
    private sealed record Branch8 { public required string Eight { get; init; } }
    private sealed record Branch9 { public required string Nine { get; init; } }
    private sealed record Branch10 { public required string Ten { get; init; } }
    private sealed record Branch11 { public required string Eleven { get; init; } }
    private sealed record Branch12 { public required string Twelve { get; init; } }
    private sealed record Branch13 { public required string Thirteen { get; init; } }
    private sealed record Branch14 { public required string Fourteen { get; init; } }
    private sealed record Branch15 { public required string Fifteen { get; init; } }
    private sealed record Branch16 { public required string Sixteen { get; init; } }
    private sealed record Branch17 { public required string Seventeen { get; init; } }
}
