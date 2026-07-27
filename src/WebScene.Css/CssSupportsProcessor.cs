using System.Text;
using System.Text.RegularExpressions;

namespace WebScene.Css;

/// <summary>
/// Evaluates the selector() subset of CSS conditional rules before the
/// third-party parser. AngleSharp currently discards selector feature queries,
/// so leaving this to its CSSOM would make both a supported query and its
/// negation disappear.
/// </summary>
internal static class CssSupportsProcessor
{
    internal static string Process(string css)
    {
        ArgumentNullException.ThrowIfNull(css);
        var result = new StringBuilder(css.Length);
        var copyStart = 0;
        var search = 0;
        while (TryFindSupportsAtRule(css, search, out var atRuleStart))
        {
            if (!TryFindBlockStart(css, atRuleStart + "@supports".Length, out var blockStart)
                || !TryFindBlockEnd(css, blockStart, out var blockEnd))
            {
                break;
            }

            result.Append(css, copyStart, atRuleStart - copyStart);
            var condition = css[(atRuleStart + "@supports".Length)..blockStart];
            var body = css[(blockStart + 1)..blockEnd];
            if (!CssSupportsConditionEvaluator.TryEvaluateSelectorCondition(condition, out var supported))
            {
                // Keep declaration-based queries on the existing CSSOM path;
                // this pre-pass owns only selector() feature queries.
                result.Append(css, atRuleStart, blockStart - atRuleStart + 1);
                result.Append(Process(body));
                result.Append('}');
            }
            else if (supported)
            {
                result.Append(Process(body));
            }

            search = blockEnd + 1;
            copyStart = search;
        }

        result.Append(css, copyStart, css.Length - copyStart);
        return result.ToString();
    }

    private static bool TryFindSupportsAtRule(string css, int start, out int position)
    {
        var quote = '\0';
        var inComment = false;
        for (var index = start; index < css.Length; index++)
        {
            if (inComment)
            {
                if (css[index] == '*' && index + 1 < css.Length && css[index + 1] == '/')
                {
                    inComment = false;
                    index++;
                }
                continue;
            }

            if (quote != '\0')
            {
                if (css[index] == quote && !IsEscaped(css, index)) quote = '\0';
                continue;
            }

            if (css[index] == '/' && index + 1 < css.Length && css[index + 1] == '*')
            {
                inComment = true;
                index++;
            }
            else if (css[index] is '\'' or '"')
            {
                quote = css[index];
            }
            else if (css[index] == '@'
                     && css.AsSpan(index).StartsWith("@supports".AsSpan(), StringComparison.OrdinalIgnoreCase)
                     && (index + "@supports".Length == css.Length
                         || !IsIdentifierCharacter(css[index + "@supports".Length])))
            {
                position = index;
                return true;
            }
        }

        position = -1;
        return false;
    }

    private static bool TryFindBlockStart(string css, int start, out int position)
    {
        var quote = '\0';
        var inComment = false;
        var parentheses = 0;
        var brackets = 0;
        for (var index = start; index < css.Length; index++)
        {
            if (inComment)
            {
                if (css[index] == '*' && index + 1 < css.Length && css[index + 1] == '/')
                {
                    inComment = false;
                    index++;
                }
                continue;
            }

            if (quote != '\0')
            {
                if (css[index] == quote && !IsEscaped(css, index)) quote = '\0';
                continue;
            }

            switch (css[index])
            {
                case '/' when index + 1 < css.Length && css[index + 1] == '*':
                    inComment = true;
                    index++;
                    break;
                case '\'':
                case '"':
                    quote = css[index];
                    break;
                case '(':
                    parentheses++;
                    break;
                case ')':
                    parentheses--;
                    if (parentheses < 0)
                    {
                        position = -1;
                        return false;
                    }
                    break;
                case '[':
                    brackets++;
                    break;
                case ']':
                    brackets--;
                    if (brackets < 0)
                    {
                        position = -1;
                        return false;
                    }
                    break;
                case '{' when parentheses == 0 && brackets == 0:
                    position = index;
                    return true;
            }
        }

        position = -1;
        return false;
    }

    private static bool TryFindBlockEnd(string css, int blockStart, out int position)
    {
        var depth = 1;
        var quote = '\0';
        var inComment = false;
        for (var index = blockStart + 1; index < css.Length; index++)
        {
            if (inComment)
            {
                if (css[index] == '*' && index + 1 < css.Length && css[index + 1] == '/')
                {
                    inComment = false;
                    index++;
                }
                continue;
            }

            if (quote != '\0')
            {
                if (css[index] == quote && !IsEscaped(css, index)) quote = '\0';
                continue;
            }

            if (css[index] == '/' && index + 1 < css.Length && css[index + 1] == '*')
            {
                inComment = true;
                index++;
            }
            else if (css[index] is '\'' or '"')
            {
                quote = css[index];
            }
            else if (css[index] == '{')
            {
                depth++;
            }
            else if (css[index] == '}' && --depth == 0)
            {
                position = index;
                return true;
            }
        }

        position = -1;
        return false;
    }

    private static bool IsIdentifierCharacter(char character)
        => char.IsLetterOrDigit(character) || character is '-' or '_';

    private static bool IsEscaped(string source, int position)
    {
        var slashCount = 0;
        for (var index = position - 1; index >= 0 && source[index] == '\\'; index--) slashCount++;
        return (slashCount & 1) != 0;
    }
}

internal static class CssSupportsConditionEvaluator
{
    internal static bool TryEvaluateSelectorCondition(string condition, out bool supported)
    {
        var parser = new ConditionParser(condition);
        var valid = parser.TryParse(out supported);
        if (!parser.SawSelectorFeature)
        {
            supported = false;
            return false;
        }
        supported = valid && supported;
        return true;
    }

    private static bool IsSupportedSelector(string selectorText, int depth = 0)
    {
        if (depth > 32 || !HasBalancedDelimiters(selectorText)) return false;
        var selectors = CssSelectorSyntaxParser.SplitSelectorList(selectorText).ToArray();
        if (selectors.Length != 1
            || !CssSelectorSyntaxParser.TryParse(selectors[0], out var selector))
        {
            return false;
        }

        foreach (var part in selector.Parts)
        {
            if (part.Simple.Attributes.Any(attribute => attribute.Name.Contains('|', StringComparison.Ordinal)))
            {
                return false;
            }

            foreach (var pseudo in part.Simple.Pseudos)
            {
                if (pseudo.IsElement)
                {
                    if (pseudo.Argument is not null || pseudo.Name is not ("before" or "after")) return false;
                    continue;
                }

                if (pseudo.Name is "not" or "is" or "where")
                {
                    if (string.IsNullOrWhiteSpace(pseudo.Argument)) return false;
                    var arguments = CssSelectorSyntaxParser.SplitSelectorList(pseudo.Argument).ToArray();
                    if (arguments.Length == 0
                        || arguments.Any(argument => !IsSupportedSelector(argument, depth + 1)))
                    {
                        return false;
                    }
                    continue;
                }

                if (pseudo.Name is "nth-child" or "nth-last-child" or "nth-of-type" or "nth-last-of-type")
                {
                    if (!IsValidNthExpression(pseudo.Argument)) return false;
                    continue;
                }

                if (pseudo.Argument is not null
                    || pseudo.Name is not (
                        "root" or "empty" or "first-child" or "last-child" or "only-child" or
                        "first-of-type" or "last-of-type" or "only-of-type" or "hover" or "active" or
                        "focus" or "focus-visible" or "disabled" or "enabled" or "checked" or "link"))
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
        return expression is "odd" or "even"
               || (!string.IsNullOrWhiteSpace(expression)
                   && Regex.IsMatch(
                       expression,
                       @"^[+-]?(?:\d+|(?:\d*)n(?:[+-]\d+)?)$",
                       RegexOptions.CultureInvariant));
    }

    private static bool HasBalancedDelimiters(string text)
    {
        var round = 0;
        var square = 0;
        var quote = '\0';
        for (var index = 0; index < text.Length; index++)
        {
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

    private sealed class ConditionParser(string source)
    {
        private int _position;

        internal bool SawSelectorFeature { get; private set; }

        internal bool TryParse(out bool value)
        {
            if (!TryParseExpression(out value)) return false;
            SkipTrivia();
            return _position == source.Length;
        }

        private bool TryParseExpression(out bool value)
        {
            SkipTrivia();
            if (TryConsumeKeyword("not"))
            {
                if (!TryParsePrimary(out value)) return false;
                value = !value;
                return true;
            }

            if (!TryParsePrimary(out value)) return false;
            string? operation = null;
            while (true)
            {
                SkipTrivia();
                string? next = null;
                if (TryConsumeKeyword("and")) next = "and";
                else if (TryConsumeKeyword("or")) next = "or";
                if (next is null) return true;
                if (operation is not null && !string.Equals(operation, next, StringComparison.Ordinal)) return false;
                operation = next;
                if (!TryParsePrimary(out var right)) return false;
                value = next == "and" ? value && right : value || right;
            }
        }

        private bool TryParsePrimary(out bool value)
        {
            SkipTrivia();
            if (TryConsumeKeyword("not"))
            {
                if (!TryParsePrimary(out value)) return false;
                value = !value;
                return true;
            }

            if (TryConsumeKeyword("selector"))
            {
                SawSelectorFeature = true;
                SkipTrivia();
                if (!TryReadParenthesized(out var selector))
                {
                    value = false;
                    return false;
                }
                value = IsSupportedSelector(selector);
                return true;
            }

            if (_position < source.Length && source[_position] == '(')
            {
                if (!TryReadParenthesized(out var nested))
                {
                    value = false;
                    return false;
                }

                var nestedParser = new ConditionParser(nested);
                // Syntactically valid but unsupported feature queries evaluate
                // false. This also gives their `not` form the required result.
                value = nestedParser.TryParse(out var nestedValue) && nestedValue;
                SawSelectorFeature |= nestedParser.SawSelectorFeature;
                return true;
            }

            // A general-enclosed or declaration query is outside WebScene's
            // claimed supports profile. Consume it as one unsupported feature.
            var start = _position;
            while (_position < source.Length) _position++;
            value = false;
            return _position > start;
        }

        private bool TryReadParenthesized(out string content)
        {
            content = string.Empty;
            if (_position >= source.Length || source[_position] != '(') return false;
            var start = ++_position;
            var depth = 1;
            var quote = '\0';
            var inComment = false;
            while (_position < source.Length)
            {
                var character = source[_position];
                if (inComment)
                {
                    if (character == '*' && _position + 1 < source.Length && source[_position + 1] == '/')
                    {
                        inComment = false;
                        _position += 2;
                        continue;
                    }
                }
                else if (quote != '\0')
                {
                    if (character == quote && !IsEscaped(source, _position)) quote = '\0';
                }
                else if (character == '/' && _position + 1 < source.Length && source[_position + 1] == '*')
                {
                    inComment = true;
                    _position += 2;
                    continue;
                }
                else if (character is '\'' or '"') quote = character;
                else if (character == '(') depth++;
                else if (character == ')' && --depth == 0)
                {
                    content = source[start.._position];
                    _position++;
                    return true;
                }
                _position++;
            }
            return false;
        }

        private bool TryConsumeKeyword(string keyword)
        {
            SkipTrivia();
            if (!_position.Equals(source.Length)
                && source.AsSpan(_position).StartsWith(keyword.AsSpan(), StringComparison.OrdinalIgnoreCase)
                && (_position + keyword.Length == source.Length
                    || !IsIdentifierCharacter(source[_position + keyword.Length])))
            {
                _position += keyword.Length;
                return true;
            }
            return false;
        }

        private void SkipTrivia()
        {
            while (_position < source.Length)
            {
                if (char.IsWhiteSpace(source[_position]))
                {
                    _position++;
                    continue;
                }
                if (source[_position] == '/'
                    && _position + 1 < source.Length
                    && source[_position + 1] == '*')
                {
                    var end = source.IndexOf("*/", _position + 2, StringComparison.Ordinal);
                    _position = end < 0 ? source.Length : end + 2;
                    continue;
                }
                break;
            }
        }

        private static bool IsIdentifierCharacter(char character)
            => char.IsLetterOrDigit(character) || character is '-' or '_';
    }
}
