using System.Text;

namespace WebScene.Css;

public enum CssSelectorCombinator
{
    None,
    Descendant,
    Child,
    AdjacentSibling,
    GeneralSibling
}

public sealed record CssAttributeSelectorSyntax(
    string Name,
    string? Operator,
    string? Value,
    bool CaseInsensitive);

public sealed class CssPseudoSelectorSyntax
{
    private CssSelectorSyntax[]? _argumentSelectors;

    public CssPseudoSelectorSyntax(string name, string? argument, bool isElement)
    {
        Name = name;
        Argument = argument;
        IsElement = isElement;
    }

    public string Name { get; }

    public string? Argument { get; }

    public bool IsElement { get; }

    internal IReadOnlyList<CssSelectorSyntax> GetArgumentSelectors()
    {
        if (_argumentSelectors is not null)
        {
            return _argumentSelectors;
        }

        if (string.IsNullOrWhiteSpace(Argument))
        {
            return _argumentSelectors = [];
        }

        var selectors = new List<CssSelectorSyntax>();
        foreach (var selectorText in CssSelectorSyntaxParser.SplitSelectorList(Argument))
        {
            if (CssSelectorSyntaxParser.TryParse(selectorText, out var selector))
            {
                selectors.Add(selector);
            }
        }
        return _argumentSelectors = selectors.ToArray();
    }
}

public sealed class CssSimpleSelectorSyntax
{
    public string? Tag { get; init; }

    public string? Id { get; init; }

    public List<string> Classes { get; } = [];

    public List<CssAttributeSelectorSyntax> Attributes { get; } = [];

    public List<CssPseudoSelectorSyntax> Pseudos { get; } = [];
}

public sealed record CssSelectorPartSyntax(
    CssSimpleSelectorSyntax Simple,
    CssSelectorCombinator CombinatorToPrevious);

public sealed record CssSelectorSyntax(IReadOnlyList<CssSelectorPartSyntax> Parts, int Specificity);

public static class CssSelectorSyntaxParser
{
    private static readonly HashSet<string> s_supportedDomPseudoClasses = new(StringComparer.Ordinal)
    {
        "root", "scope", "empty",
        "first-child", "last-child", "only-child",
        "first-of-type", "last-of-type", "only-of-type",
        "nth-child", "nth-last-child", "nth-of-type", "nth-last-of-type",
        "not", "is", "where", "has",
        "hover", "active", "focus", "focus-visible", "focus-within",
        "disabled", "enabled", "checked", "indeterminate", "default",
        "required", "optional", "valid", "invalid", "in-range", "out-of-range",
        "read-only", "read-write", "placeholder-shown", "autofill",
        "link", "visited", "any-link", "local-link", "target", "target-within",
        "lang", "dir", "defined", "fullscreen", "modal", "open",
        "picture-in-picture", "user-valid", "user-invalid", "blank"
    };

    private static readonly HashSet<string> s_recognizedDomPseudoElements = new(StringComparer.Ordinal)
    {
        "before", "after", "first-letter", "first-line", "selection", "marker",
        "placeholder", "backdrop", "file-selector-button", "cue", "cue-region",
        "grammar-error", "spelling-error", "target-text",
        "-webkit-scrollbar", "-webkit-scrollbar-thumb", "-webkit-scrollbar-track",
        "-webkit-scrollbar-corner"
    };

    public static IEnumerable<string> SplitSelectorList(string selectorText)
    {
        ArgumentNullException.ThrowIfNull(selectorText);
        var start = 0;
        var square = 0;
        var round = 0;
        char quote = '\0';
        for (var index = 0; index < selectorText.Length; index++)
        {
            var character = selectorText[index];
            if (character == '\\' && quote == '\0')
            {
                index = SkipEscape(selectorText, index);
                continue;
            }
            if (quote != '\0')
            {
                if (character == quote && (index == 0 || selectorText[index - 1] != '\\'))
                {
                    quote = '\0';
                }
                continue;
            }

            if (character is '\'' or '"') quote = character;
            else if (character == '[') square++;
            else if (character == ']') square--;
            else if (character == '(') round++;
            else if (character == ')') round--;
            else if (character == ',' && square == 0 && round == 0)
            {
                var part = selectorText[start..index].Trim();
                if (part.Length > 0) yield return part;
                start = index + 1;
            }
        }

        var last = selectorText[start..].Trim();
        if (last.Length > 0) yield return last;
    }

    public static bool TryParse(string text, out CssSelectorSyntax selector)
    {
        selector = null!;
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        var tokens = Tokenize(text);
        if (tokens.Count == 0)
        {
            return false;
        }

        var parts = new List<CssSelectorPartSyntax>();
        var pending = CssSelectorCombinator.None;
        var specificity = 0;
        foreach (var token in tokens)
        {
            if (token.IsCombinator)
            {
                pending = token.Combinator;
                continue;
            }

            if (!TryParseSimple(token.Text, out var simple, out var simpleSpecificity))
            {
                return false;
            }

            parts.Add(new CssSelectorPartSyntax(
                simple,
                parts.Count == 0
                    ? CssSelectorCombinator.None
                    : pending == CssSelectorCombinator.None
                        ? CssSelectorCombinator.Descendant
                        : pending));
            specificity += simpleSpecificity;
            pending = CssSelectorCombinator.None;
        }

        if (parts.Count == 0)
        {
            return false;
        }

        selector = new CssSelectorSyntax(parts, specificity);
        return true;
    }

    /// <summary>
    /// Returns whether a selector list is both syntactically valid and implemented by
    /// WebScene's DOM selector matcher. DOM query APIs must reject unsupported selector
    /// syntax with a SyntaxError instead of treating it as a non-match; stylesheet
    /// parsing remains independently forgiving.
    /// </summary>
    public static bool IsSupportedDomSelectorList(string selectorText)
    {
        if (string.IsNullOrWhiteSpace(selectorText) || !HasBalancedDelimiters(selectorText))
        {
            return false;
        }

        var selectors = SplitSelectorList(selectorText).ToArray();
        if (selectors.Length == 0 || selectors.Length != CountTopLevelSelectorItems(selectorText))
        {
            return false;
        }

        return selectors.All(static selectorText =>
            TryParse(selectorText, out var selector) && IsSupportedDomSelector(selector));
    }

    private static bool IsSupportedDomSelector(CssSelectorSyntax selector)
    {
        foreach (var part in selector.Parts)
        {
            foreach (var pseudo in part.Simple.Pseudos)
            {
                if (pseudo.IsElement)
                {
                    if (pseudo.Argument is not null || !s_recognizedDomPseudoElements.Contains(pseudo.Name))
                    {
                        return false;
                    }
                    continue;
                }

                if (!s_supportedDomPseudoClasses.Contains(pseudo.Name))
                {
                    return false;
                }

                if (pseudo.Name is "not" or "is" or "where" or "has")
                {
                    if (string.IsNullOrWhiteSpace(pseudo.Argument)
                        || !IsSupportedDomSelectorList(pseudo.Argument))
                    {
                        return false;
                    }
                    continue;
                }

                if (pseudo.Name is "nth-child" or "nth-last-child" or "nth-of-type" or "nth-last-of-type")
                {
                    if (!IsValidNthExpression(pseudo.Argument))
                    {
                        return false;
                    }
                    continue;
                }

                if (pseudo.Name is "lang" or "dir")
                {
                    if (string.IsNullOrWhiteSpace(pseudo.Argument))
                    {
                        return false;
                    }
                    continue;
                }

                if (pseudo.Argument is not null)
                {
                    return false;
                }
            }
        }
        return true;
    }

    private static bool IsValidNthExpression(string? argument)
    {
        var expression = argument?.Replace(" ", string.Empty, StringComparison.Ordinal).ToLowerInvariant();
        if (expression is "odd" or "even")
        {
            return true;
        }
        if (string.IsNullOrEmpty(expression))
        {
            return false;
        }

        var index = 0;
        if (expression[index] is '+' or '-') index++;
        var digitsBeforeN = 0;
        while (index < expression.Length && char.IsAsciiDigit(expression[index]))
        {
            digitsBeforeN++;
            index++;
        }
        if (index == expression.Length)
        {
            return digitsBeforeN > 0;
        }
        if (expression[index] != 'n')
        {
            return false;
        }
        index++;
        if (index == expression.Length)
        {
            return true;
        }
        if (expression[index] is not ('+' or '-'))
        {
            return false;
        }
        index++;
        var offsetStart = index;
        while (index < expression.Length && char.IsAsciiDigit(expression[index])) index++;
        return index == expression.Length && index > offsetStart;
    }

    private static int CountTopLevelSelectorItems(string selectorText)
    {
        var count = 1;
        var square = 0;
        var round = 0;
        char quote = '\0';
        for (var index = 0; index < selectorText.Length; index++)
        {
            var character = selectorText[index];
            if (character == '\\' && quote == '\0')
            {
                index = SkipEscape(selectorText, index);
                continue;
            }
            if (quote != '\0')
            {
                if (character == quote && !IsEscaped(selectorText, index)) quote = '\0';
                continue;
            }
            if (character is '\'' or '"') quote = character;
            else if (character == '[') square++;
            else if (character == ']') square--;
            else if (character == '(') round++;
            else if (character == ')') round--;
            else if (character == ',' && square == 0 && round == 0) count++;
        }
        return count;
    }

    private static bool HasBalancedDelimiters(string text)
    {
        var round = 0;
        var square = 0;
        char quote = '\0';
        for (var index = 0; index < text.Length; index++)
        {
            if (text[index] == '\\' && quote == '\0')
            {
                index = SkipEscape(text, index);
                continue;
            }
            if (quote != '\0')
            {
                if (text[index] == quote && !IsEscaped(text, index)) quote = '\0';
            }
            else if (text[index] is '\'' or '"') quote = text[index];
            else if (text[index] == '(') round++;
            else if (text[index] == ')' && --round < 0) return false;
            else if (text[index] == '[') square++;
            else if (text[index] == ']' && --square < 0) return false;
        }
        return quote == '\0' && round == 0 && square == 0;
    }

    private static bool IsEscaped(string source, int position)
    {
        var slashCount = 0;
        for (var index = position - 1; index >= 0 && source[index] == '\\'; index--) slashCount++;
        return (slashCount & 1) != 0;
    }

    private static List<SelectorToken> Tokenize(string text)
    {
        var result = new List<SelectorToken>();
        var current = new StringBuilder();
        var square = 0;
        var round = 0;
        char quote = '\0';
        var pendingWhitespace = false;

        void Flush()
        {
            if (current.Length == 0) return;
            result.Add(SelectorToken.Simple(current.ToString()));
            current.Clear();
        }

        for (var index = 0; index < text.Length; index++)
        {
            var character = text[index];
            if (character == '\\' && quote == '\0')
            {
                var escapeEnd = SkipEscape(text, index);
                current.Append(text, index, escapeEnd - index + 1);
                index = escapeEnd;
                continue;
            }
            if (quote != '\0')
            {
                current.Append(character);
                if (character == quote && (index == 0 || text[index - 1] != '\\')) quote = '\0';
                continue;
            }

            if (character is '\'' or '"')
            {
                quote = character;
                current.Append(character);
            }
            else if (character == '[') { square++; current.Append(character); }
            else if (character == ']') { square--; current.Append(character); }
            else if (character == '(') { round++; current.Append(character); }
            else if (character == ')') { round--; current.Append(character); }
            else if (square == 0 && round == 0 && char.IsWhiteSpace(character))
            {
                Flush();
                pendingWhitespace = result.Count > 0 && !result[^1].IsCombinator;
            }
            else if (square == 0 && round == 0 && character is '>' or '+' or '~')
            {
                Flush();
                if (result.Count > 0 && result[^1].IsCombinator) result.RemoveAt(result.Count - 1);
                result.Add(SelectorToken.ForCombinator(character switch
                {
                    '>' => CssSelectorCombinator.Child,
                    '+' => CssSelectorCombinator.AdjacentSibling,
                    _ => CssSelectorCombinator.GeneralSibling
                }));
                pendingWhitespace = false;
            }
            else
            {
                if (pendingWhitespace)
                {
                    result.Add(SelectorToken.ForCombinator(CssSelectorCombinator.Descendant));
                    pendingWhitespace = false;
                }
                current.Append(character);
            }
        }

        Flush();
        return result;
    }

    private static bool TryParseSimple(string text, out CssSimpleSelectorSyntax simple, out int specificity)
    {
        simple = new CssSimpleSelectorSyntax();
        specificity = 0;
        var index = 0;
        if (index < text.Length && (text[index] == '*' || IsIdentifierStart(text[index])))
        {
            var tag = text[index] == '*' ? "*" : ReadIdentifier(text, ref index);
            if (tag == "*") index++;
            simple = new CssSimpleSelectorSyntax { Tag = tag };
            if (tag != "*") specificity++;
        }

        while (index < text.Length)
        {
            switch (text[index])
            {
                case '#':
                    index++;
                    simple = Clone(simple, id: ReadIdentifier(text, ref index));
                    specificity += 100;
                    break;
                case '.':
                    index++;
                    simple.Classes.Add(ReadIdentifier(text, ref index));
                    specificity += 10;
                    break;
                case '[':
                    var attributeText = ReadBalanced(text, ref index, '[', ']');
                    if (!TryParseAttribute(attributeText, out var attribute)) return false;
                    simple.Attributes.Add(attribute);
                    specificity += 10;
                    break;
                case ':':
                    index++;
                    var isElement = index < text.Length && text[index] == ':';
                    if (isElement) index++;
                    var name = ReadIdentifier(text, ref index).ToLowerInvariant();
                    if (name.Length == 0) return false;
                    isElement |= name is "before" or "after";
                    string? argument = null;
                    if (index < text.Length && text[index] == '(')
                    {
                        argument = ReadBalanced(text, ref index, '(', ')');
                    }
                    if (isElement && simple.Pseudos.Any(static pseudo => pseudo.IsElement)) return false;
                    var pseudo = new CssPseudoSelectorSyntax(name, argument, isElement);
                    simple.Pseudos.Add(pseudo);
                    specificity += GetPseudoSpecificity(pseudo);
                    break;
                default:
                    return false;
            }
        }

        return true;
    }

    private static int GetPseudoSpecificity(CssPseudoSelectorSyntax pseudo)
    {
        if (pseudo.IsElement)
        {
            return 1;
        }
        if (pseudo.Name == "where")
        {
            return 0;
        }
        if (pseudo.Name is not ("is" or "not" or "has"))
        {
            return 10;
        }

        var maximum = 0;
        foreach (var argumentSelector in pseudo.GetArgumentSelectors())
        {
            maximum = Math.Max(maximum, argumentSelector.Specificity);
        }
        return maximum;
    }

    private static CssSimpleSelectorSyntax Clone(CssSimpleSelectorSyntax source, string? id)
    {
        var clone = new CssSimpleSelectorSyntax { Tag = source.Tag, Id = id ?? source.Id };
        clone.Classes.AddRange(source.Classes);
        clone.Attributes.AddRange(source.Attributes);
        clone.Pseudos.AddRange(source.Pseudos);
        return clone;
    }

    private static bool TryParseAttribute(string content, out CssAttributeSelectorSyntax attribute)
    {
        attribute = null!;
        content = content.Trim();
        var caseInsensitive = content.EndsWith(" i", StringComparison.OrdinalIgnoreCase);
        if (caseInsensitive) content = content[..^2].TrimEnd();
        string? operation = null;
        var operationIndex = -1;
        foreach (var candidate in new[] { "~=", "|=", "^=", "$=", "*=", "=" })
        {
            operationIndex = content.IndexOf(candidate, StringComparison.Ordinal);
            if (operationIndex >= 0)
            {
                operation = candidate;
                break;
            }
        }

        var name = (operationIndex >= 0 ? content[..operationIndex] : content).Trim();
        if (name.Length == 0) return false;
        var value = operationIndex >= 0
            ? content[(operationIndex + operation!.Length)..].Trim().Trim('\'', '"')
            : null;
        attribute = new CssAttributeSelectorSyntax(name, operation, value, caseInsensitive);
        return true;
    }

    private static string ReadBalanced(string text, ref int index, char open, char close)
    {
        index++;
        var start = index;
        var depth = 1;
        char quote = '\0';
        while (index < text.Length)
        {
            var character = text[index];
            if (quote != '\0')
            {
                if (character == quote && text[index - 1] != '\\') quote = '\0';
            }
            else if (character is '\'' or '"') quote = character;
            else if (character == open) depth++;
            else if (character == close && --depth == 0)
            {
                var result = text[start..index];
                index++;
                return result;
            }
            index++;
        }
        return text[start..];
    }

    private static string ReadIdentifier(string text, ref int index)
    {
        var builder = new StringBuilder();
        while (index < text.Length)
        {
            var character = text[index];
            if (character == '\\' && index + 1 < text.Length)
            {
                index++;
                if (text[index] == '\r')
                {
                    index++;
                    if (index < text.Length && text[index] == '\n') index++;
                    continue;
                }
                if (text[index] is '\n' or '\f')
                {
                    index++;
                    continue;
                }
                var hexStart = index;
                while (index < text.Length
                       && index - hexStart < 6
                       && Uri.IsHexDigit(text[index]))
                {
                    index++;
                }
                if (index > hexStart)
                {
                    var scalar = Convert.ToInt32(text[hexStart..index], 16);
                    if (index < text.Length && text[index] == '\r')
                    {
                        index++;
                        if (index < text.Length && text[index] == '\n') index++;
                    }
                    else if (index < text.Length && char.IsWhiteSpace(text[index])) index++;
                    builder.Append(scalar is 0 or > 0x10ffff || scalar is >= 0xd800 and <= 0xdfff
                        ? "\ufffd"
                        : char.ConvertFromUtf32(scalar));
                }
                else
                {
                    builder.Append(text[index]);
                    index++;
                }
            }
            else if (character == '\\')
            {
                builder.Append('\ufffd');
                index++;
            }
            else if (character == '\0')
            {
                builder.Append('\ufffd');
                index++;
            }
            else if (char.IsLetterOrDigit(character) || character is '-' or '_' || character >= 0x80)
            {
                builder.Append(character);
                index++;
            }
            else
            {
                break;
            }
        }
        return builder.ToString();
    }

    private static bool IsIdentifierStart(char character)
        => char.IsLetter(character) || character is '-' or '_' or '\\' || character >= 0x80;

    private static int SkipEscape(string text, int slashIndex)
    {
        var index = slashIndex + 1;
        if (index >= text.Length) return slashIndex;
        if (text[index] == '\r')
        {
            index++;
            if (index < text.Length && text[index] == '\n') index++;
            return index - 1;
        }
        if (text[index] is '\n' or '\f') return index;
        var hexStart = index;
        while (index < text.Length
               && index - hexStart < 6
               && Uri.IsHexDigit(text[index]))
        {
            index++;
        }
        if (index > hexStart && index < text.Length && text[index] == '\r')
        {
            index++;
            if (index < text.Length && text[index] == '\n') index++;
        }
        else if (index > hexStart && index < text.Length && char.IsWhiteSpace(text[index])) index++;
        else if (index == hexStart) index++;
        return index - 1;
    }

    private readonly record struct SelectorToken(string Text, bool IsCombinator, CssSelectorCombinator Combinator)
    {
        public static SelectorToken Simple(string text) => new(text, false, CssSelectorCombinator.None);

        public static SelectorToken ForCombinator(CssSelectorCombinator combinator) => new(string.Empty, true, combinator);
    }
}
