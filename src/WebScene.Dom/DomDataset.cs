using System.Dynamic;
using System.Text;

namespace JavaScript.Avalonia;

public interface IDomDatasetAdapter
{
    IReadOnlyDictionary<string, string?> DataAttributes { get; }

    void SetDataAttribute(string attributeName, string? value);

    bool TryGetDataAttribute(string attributeName, out string? value);

    bool RemoveDataAttribute(string attributeName);
}

/// <summary>
/// Framework-neutral DOMStringMap semantics over the element's direct attribute
/// storage. The target is the real DOM element, not a projection or copied map.
/// </summary>
public class DomStringMapCore<TAdapter> : DynamicObject
    where TAdapter : struct, IDomDatasetAdapter
{
    private readonly TAdapter _target;

    protected DomStringMapCore(TAdapter target)
    {
        _target = target;
    }

    public override bool TrySetMember(SetMemberBinder binder, object? value)
    {
        _target.SetDataAttribute(ToAttributeName(binder.Name), value?.ToString());
        return true;
    }

    public override bool TryGetMember(GetMemberBinder binder, out object? result)
    {
        _target.TryGetDataAttribute(ToAttributeName(binder.Name), out var value);
        result = value;
        return true;
    }

    public override bool TryDeleteMember(DeleteMemberBinder binder)
        => _target.RemoveDataAttribute(ToAttributeName(binder.Name));

    public void set(string name, string? value)
        => _target.SetDataAttribute(ToAttributeName(name), value);

    public string? get(string name)
        => _target.TryGetDataAttribute(ToAttributeName(name), out var value) ? value : null;

    public bool delete(string name)
        => _target.RemoveDataAttribute(ToAttributeName(name));

    public bool has(string name)
        => _target.TryGetDataAttribute(ToAttributeName(name), out _);

    public string[] keys()
    {
        var result = new string[_target.DataAttributes.Count];
        var index = 0;
        foreach (var attributeName in _target.DataAttributes.Keys)
        {
            result[index++] = ToDatasetKey(attributeName);
        }
        return result;
    }

    internal static string ToAttributeName(string key)
    {
        var builder = new StringBuilder();
        for (var index = 0; index < key.Length; index++)
        {
            var character = key[index];
            if (char.IsUpper(character))
            {
                builder.Append('-');
                builder.Append(char.ToLowerInvariant(character));
            }
            else
            {
                builder.Append(character);
            }
        }
        return "data-" + builder.ToString();
    }

    internal static string ToDatasetKey(string attributeName)
    {
        if (!attributeName.StartsWith("data-", StringComparison.OrdinalIgnoreCase))
        {
            return attributeName;
        }

        var span = attributeName.AsSpan(5);
        var builder = new StringBuilder(span.Length);
        var upper = false;
        foreach (var character in span)
        {
            if (character == '-')
            {
                upper = true;
                continue;
            }

            builder.Append(upper ? char.ToUpperInvariant(character) : character);
            upper = false;
        }
        return builder.ToString();
    }
}
