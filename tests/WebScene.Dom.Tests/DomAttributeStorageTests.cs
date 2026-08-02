using WebScene.Dom;
using Xunit;

namespace WebScene.Dom.Tests;

public sealed class DomAttributeStorageTests
{
    [Fact]
    public void GenericAttributeStorageNormalizesNamesAndPreservesValues()
    {
        var element = new RecordingElement();

        element.Set("ARIA-LABEL", "Open chart");

        Assert.True(element.TryGet("aria-label", out var value));
        Assert.Equal("Open chart", value);
        Assert.True(element.TryGet("ArIa-LaBeL", out value));
        Assert.Equal("Open chart", value);
    }

    [Fact]
    public void NullGenericAttributeValueRemovesStorage()
    {
        var element = new RecordingElement();
        element.Set("title", "Chart");

        element.Set("TITLE", null);

        Assert.False(element.TryGet("title", out _));
    }

    [Fact]
    public void AttributePresenceIsPortableAndCaseInsensitive()
    {
        var element = new RecordingElement();

        element.SetPresence("DATA-THEME", present: true);
        Assert.True(element.HasPresence("data-theme"));

        element.SetPresence("Data-Theme", present: false);
        Assert.False(element.HasPresence("DATA-THEME"));
    }

    private sealed class RecordingElement : DomElementCore
    {
        public RecordingElement()
            : base(default)
        {
        }

        public void Set(string name, string? value) => SetGenericAttribute(name, value);

        public bool TryGet(string name, out string? value)
            => TryGetGenericAttribute(name, out value);

        public void SetPresence(string name, bool present)
            => SetAttributePresence(name, present);

        public bool HasPresence(string name) => HasAttributePresence(name);
    }
}
