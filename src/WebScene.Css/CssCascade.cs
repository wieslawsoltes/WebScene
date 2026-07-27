using System.Runtime.CompilerServices;

namespace WebScene.Css;

public readonly record struct CssCascadeDeclaration(string Name, string Value, bool Important);

public readonly record struct CssCascadeWinner(
    string Value,
    bool Important,
    int Specificity,
    int SourceOrder,
    int Sequence) : IComparable<CssCascadeWinner>
{
    public int CompareTo(CssCascadeWinner other)
    {
        var important = Important.CompareTo(other.Important);
        if (important != 0) return important;
        var specificity = Specificity.CompareTo(other.Specificity);
        return specificity != 0 ? specificity : SourceOrder.CompareTo(other.SourceOrder);
    }
}

public static class CssCascade
{
    /// <summary>
    /// Applies author-origin declaration precedence while preserving a property's
    /// first insertion sequence for deterministic computed-style enumeration.
    /// </summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void ApplyWinner(
        IDictionary<string, CssCascadeWinner> winners,
        string name,
        string value,
        bool important,
        int specificity,
        int sourceOrder)
    {
        if (!winners.TryGetValue(name, out var current))
        {
            winners[name] = new CssCascadeWinner(
                value,
                important,
                specificity,
                sourceOrder,
                winners.Count);
            return;
        }

        var candidate = new CssCascadeWinner(
            value,
            important,
            specificity,
            sourceOrder,
            current.Sequence);
        if (candidate.CompareTo(current) >= 0)
        {
            winners[name] = candidate;
        }
    }
}
