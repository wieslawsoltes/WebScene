using System.Diagnostics.CodeAnalysis;

namespace WebScene.Css;

/// <summary>
/// Shared CSS Syntax rules that differ from .NET's Unicode whitespace and
/// case-folding defaults. Custom-property names are case-sensitive and their
/// token streams trim only CSS whitespace (TAB, LF, FF, CR, and SPACE).
/// </summary>
public static class CssCustomPropertySyntax
{
    public static bool IsValidName([NotNullWhen(true)] string? name)
    {
        if (name is null || name.Length <= 2 || !name.StartsWith("--", StringComparison.Ordinal))
        {
            return false;
        }

        foreach (var character in name)
        {
            if (character <= '\u0020' || character == '\u007f')
            {
                return false;
            }
        }

        return true;
    }

    public static string TrimWhitespace(string value)
    {
        var span = value.AsSpan();
        while (!span.IsEmpty && IsWhitespace(span[0]))
        {
            span = span[1..];
        }
        while (!span.IsEmpty && IsWhitespace(span[^1]))
        {
            span = span[..^1];
        }
        return span.Length == value.Length ? value : span.ToString();
    }

    public static bool IsWhitespace(char character)
        => character is '\u0009' or '\u000a' or '\u000c' or '\u000d' or '\u0020';
}

/// <summary>
/// Ordinary CSS property names are ASCII case-insensitive in WebScene's internal
/// projection, while custom-property names must retain ordinal identity.
/// </summary>
public sealed class CssPropertyNameComparer : IEqualityComparer<string>
{
    public static CssPropertyNameComparer Instance { get; } = new();

    private CssPropertyNameComparer()
    {
    }

    public bool Equals(string? left, string? right)
    {
        if (ReferenceEquals(left, right))
        {
            return true;
        }
        if (left is null || right is null)
        {
            return false;
        }

        var custom = left.StartsWith("--", StringComparison.Ordinal)
                     || right.StartsWith("--", StringComparison.Ordinal);
        return string.Equals(
            left,
            right,
            custom ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase);
    }

    public int GetHashCode(string value)
        => value.StartsWith("--", StringComparison.Ordinal)
            ? StringComparer.Ordinal.GetHashCode(value)
            : StringComparer.OrdinalIgnoreCase.GetHashCode(value);
}
