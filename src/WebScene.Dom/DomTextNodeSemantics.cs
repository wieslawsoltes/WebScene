using System.Runtime.CompilerServices;

namespace JavaScript.Avalonia;

/// <summary>
/// Framework-neutral text-node transformation semantics. Text storage stays on
/// each backend's concrete node so ordinary elements do not acquire text-only
/// fields or wrapper objects.
/// </summary>
public static class DomTextNodeSemantics
{
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static string NormalizeTextTransform(string? value)
        => value?.Trim().ToLowerInvariant() ?? "none";

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static string ApplyTextTransform(string data, string normalizedTransform)
        => normalizedTransform switch
        {
            "uppercase" => data.ToUpperInvariant(),
            "lowercase" => data.ToLowerInvariant(),
            "capitalize" => CapitalizeWords(data),
            _ => data
        };

    public static string CapitalizeWords(string value)
    {
        var result = value.ToCharArray();
        var atWordStart = true;
        for (var index = 0; index < result.Length; index++)
        {
            if (char.IsLetterOrDigit(result[index]))
            {
                if (atWordStart)
                {
                    result[index] = char.ToUpperInvariant(result[index]);
                }
                atWordStart = false;
            }
            else
            {
                atWordStart = true;
            }
        }
        return new string(result);
    }
}
