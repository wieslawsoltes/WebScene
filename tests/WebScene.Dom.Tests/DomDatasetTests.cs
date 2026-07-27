using JavaScript.Avalonia;
using Xunit;

namespace WebScene.Dom.Tests;

public sealed class DomDatasetTests
{
    [Fact]
    public void NamedAccessMapsCamelCaseToDataAttributes()
    {
        var target = new RecordingTarget();
        dynamic dataset = new RecordingStringMap(target);

        dataset.chartTheme = "dark";

        Assert.Equal("dark", target.Attributes["data-chart-theme"]);
        Assert.Equal("dark", dataset.chartTheme);
    }

    [Fact]
    public void MissingNamedAccessReturnsNullAndDeleteReportsStorageResult()
    {
        var target = new RecordingTarget();
        dynamic dataset = new RecordingStringMap(target);

        Assert.Null(dataset.missingValue);
        dataset.itemId = "42";
        Assert.True(dataset.delete("itemId"));
        Assert.False(dataset.has("itemId"));
        Assert.False(dataset.delete("itemId"));
    }

    [Fact]
    public void KeysUseDatasetCasingWithoutAnIntermediateLinqProjection()
    {
        var target = new RecordingTarget();
        var dataset = new RecordingStringMap(target);
        dataset.set("chartTheme", "dark");
        dataset.set("itemId", "42");

        Assert.Equal(new[] { "chartTheme", "itemId" }, dataset.keys());
        Assert.Equal("data-chart-theme", DomStringMapCore<RecordingAdapter>.ToAttributeName("chartTheme"));
        Assert.Equal("chartTheme", DomStringMapCore<RecordingAdapter>.ToDatasetKey("data-chart-theme"));
    }

    private sealed class RecordingStringMap : DomStringMapCore<RecordingAdapter>
    {
        public RecordingStringMap(RecordingTarget target)
            : base(new RecordingAdapter(target))
        {
        }
    }

    private readonly struct RecordingAdapter : IDomDatasetAdapter
    {
        private readonly RecordingTarget _target;

        public RecordingAdapter(RecordingTarget target) => _target = target;

        public IReadOnlyDictionary<string, string?> DataAttributes => _target.DataAttributes;

        public void SetDataAttribute(string attributeName, string? value)
            => _target.SetDataAttribute(attributeName, value);

        public bool TryGetDataAttribute(string attributeName, out string? value)
            => _target.TryGetDataAttribute(attributeName, out value);

        public bool RemoveDataAttribute(string attributeName)
            => _target.RemoveDataAttribute(attributeName);
    }

    private sealed class RecordingTarget
    {
        private readonly Dictionary<string, string?> _attributes = new(StringComparer.OrdinalIgnoreCase);

        public IReadOnlyDictionary<string, string?> DataAttributes => _attributes;

        public Dictionary<string, string?> Attributes => _attributes;

        public void SetDataAttribute(string attributeName, string? value)
        {
            if (value is null)
            {
                _attributes.Remove(attributeName);
            }
            else
            {
                _attributes[attributeName] = value;
            }
        }

        public bool TryGetDataAttribute(string attributeName, out string? value)
            => _attributes.TryGetValue(attributeName, out value);

        public bool RemoveDataAttribute(string attributeName)
            => _attributes.Remove(attributeName);
    }
}
