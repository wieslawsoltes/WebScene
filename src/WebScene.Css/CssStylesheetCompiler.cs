using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;
using AngleSharp.Css.Dom;
using AngleSharp.Css.Parser;

namespace WebScene.Css;

public sealed record CssCompiledStyleRule(
    CssSelectorSyntax Selector,
    IReadOnlyList<CssCascadeDeclaration> Declarations,
    IReadOnlyList<string> MediaQueries);

/// <summary>
/// A portable projection of the descriptors that participate in CSS font-face
/// matching. Resource fetching and platform font registration remain backend
/// responsibilities.
/// </summary>
public sealed record CssCompiledFontFace(
    string Family,
    string Source,
    string Style,
    string Weight,
    string Stretch,
    IReadOnlyList<string> MediaQueries);

public sealed record CssStylesheetCompilation(
    IReadOnlyList<CssCompiledStyleRule> Rules,
    long NormalizationTicks,
    long NormalizationAllocatedBytes,
    long ParserTicks,
    long ParserAllocatedBytes,
    long RuleCompilationTicks,
    long RuleCompilationAllocatedBytes)
{
    public IReadOnlyList<CssCompiledFontFace> FontFaces { get; init; } = Array.Empty<CssCompiledFontFace>();
}

/// <summary>
/// Portable stylesheet normalization and compilation. The third-party syntax parser
/// is an implementation detail; backends receive only WebScene selector and cascade models.
/// </summary>
public static class CssStylesheetCompiler
{
    public const string ProtectedVariableShorthandPrefix = "--webscene-var-shorthand-";
    private const string ProtectedCssWideValuePrefix = "-webscene-css-wide-";
    private const string ProtectedCustomPropertyNamePrefix = "--webscene-custom-name-";
    private const string ProtectedScrollbarPseudoClassPrefix = ".webscene-protected-scrollbar-pseudo-";

    public static CssStylesheetCompilation Compile(
        string css,
        bool disableNormalizationGuards = false,
        bool collectPerformanceMetrics = false)
    {
        ArgumentNullException.ThrowIfNull(css);
        var started = collectPerformanceMetrics ? Stopwatch.GetTimestamp() : 0;
        var allocationStarted = collectPerformanceMetrics ? GC.GetAllocatedBytesForCurrentThread() : 0;
        var normalized = Normalize(CssSupportsProcessor.Process(css), disableNormalizationGuards);
        var parserInput = ProtectScrollbarPseudoElementsForParser(
            ProtectCustomPropertyNamesForParser(normalized));
        var normalizationTicks = collectPerformanceMetrics ? Stopwatch.GetTimestamp() - started : 0;
        var normalizationAllocated = collectPerformanceMetrics
            ? GC.GetAllocatedBytesForCurrentThread() - allocationStarted
            : 0;

        started = collectPerformanceMetrics ? Stopwatch.GetTimestamp() : 0;
        allocationStarted = collectPerformanceMetrics ? GC.GetAllocatedBytesForCurrentThread() : 0;
        var sheet = new CssParser().ParseStyleSheet(parserInput);
        var parserTicks = collectPerformanceMetrics ? Stopwatch.GetTimestamp() - started : 0;
        var parserAllocated = collectPerformanceMetrics
            ? GC.GetAllocatedBytesForCurrentThread() - allocationStarted
            : 0;

        started = collectPerformanceMetrics ? Stopwatch.GetTimestamp() : 0;
        allocationStarted = collectPerformanceMetrics ? GC.GetAllocatedBytesForCurrentThread() : 0;
        var rules = new List<CssCompiledStyleRule>();
        var fontFaces = new List<CssCompiledFontFace>();
        if (sheet is not null)
        {
            CollectRules(sheet.Rules, rules, fontFaces);
        }
        return new CssStylesheetCompilation(
            rules,
            normalizationTicks,
            normalizationAllocated,
            collectPerformanceMetrics ? parserTicks : 0,
            collectPerformanceMetrics ? parserAllocated : 0,
            collectPerformanceMetrics ? Stopwatch.GetTimestamp() - started : 0,
            collectPerformanceMetrics ? GC.GetAllocatedBytesForCurrentThread() - allocationStarted : 0)
        {
            FontFaces = fontFaces
        };
    }

    public static string Normalize(string css, bool disableGuards = false)
    {
        ArgumentNullException.ThrowIfNull(css);
        // This projection currently lays out left-to-right. Map logical side
        // border shorthands before protecting var()-backed shorthands so the
        // physical declaration survives the third-party parser intact.
        foreach (var (logical, physical) in s_ltrLogicalBorderShorthands)
        {
            css = Regex.Replace(
                css,
                $@"(?<prefix>^|[;{{])(?<space>\s*){Regex.Escape(logical)}(?=\s*:)",
                match => match.Groups["prefix"].Value + match.Groups["space"].Value + physical,
                RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);
        }

        // AngleSharp expands an earlier flex shorthand into longhands, then can
        // discard a later keyword shorthand such as `flex: none`. Preserve the
        // authored shorthand as a custom property so CSSOM declaration order
        // and importance select the winner before CollectRules restores `flex`.
        css = Regex.Replace(
            css,
            @"(?<prefix>^|[;{])(?<space>\s*)flex\s*(?=:)",
            match => match.Groups["prefix"].Value + match.Groups["space"].Value
                     + ProtectedVariableShorthandPrefix + "flex",
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);

        // AngleSharp's current declaration grammar drops the transition
        // shorthand. Carry its authored value through parsing as a custom
        // property, then CollectRules restores the real property name using
        // the same protected-declaration seam as var()-backed shorthands.
        css = Regex.Replace(
            css,
            @"(?<prefix>^|[;{])(?<space>\s*)transition\s*(?=:)",
            match => match.Groups["prefix"].Value + match.Groups["space"].Value
                     + ProtectedVariableShorthandPrefix + "transition",
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);

        // AngleSharp currently drops outline-offset even though it is an
        // independent longhand. Preserve it as an ordinary declaration so
        // computed style and the presentation backend can consume it.
        css = Regex.Replace(
            css,
            @"(?<prefix>^|[;{])(?<space>\s*)outline-offset\s*(?=:)",
            match => match.Groups["prefix"].Value + match.Groups["space"].Value
                     + ProtectedVariableShorthandPrefix + "outline-offset",
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);

        // Preserve list-style as one cascade declaration. Expanding it in the
        // third-party parser loses authored shorthand/longhand source order and
        // can let a later non-important longhand beat an important shorthand.
        // WebScene validates and expands the supported marker subset after var()
        // substitution at the cascade boundary.
        css = Regex.Replace(
            css,
            @"(?<prefix>^|[;{])(?<space>\s*)list-style\s*:\s*(?<value>[^;{}]+?)(?=\s*[;}])",
            match =>
            {
                var value = match.Groups["value"].Value.Trim();
                var important = Regex.Match(
                    value,
                    @"\s*!important\s*$",
                    RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
                var suffix = important.Success ? " !important" : string.Empty;
                if (important.Success) value = value[..important.Index].TrimEnd();
                if (value.ToLowerInvariant() is "initial" or "inherit" or "unset")
                {
                    value = ProtectedCssWideValuePrefix + value.ToLowerInvariant();
                }
                return match.Groups["prefix"].Value + match.Groups["space"].Value
                       + ProtectedVariableShorthandPrefix + "list-style:" + value + suffix;
            },
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);

        // AngleSharp exposes `all` as a shorthand but does not enumerate the
        // authored declaration in every CSS-wide-keyword case. Preserve it as
        // an opaque custom declaration so WebScene can expand it at the
        // per-property cascade boundary with its original source order.
        css = Regex.Replace(
            css,
            @"(?<prefix>^|[;{])(?<space>\s*)all\s*:\s*(?<value>[^;{}]+?)(?=\s*[;}])",
            match => match.Groups["prefix"].Value + match.Groups["space"].Value
                     + ProtectedVariableShorthandPrefix + "all:"
                     + match.Groups["value"].Value.Trim(),
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);

        // AngleSharp's background shorthand grammar currently accepts the
        // declaration but drops a currentColor color component while retaining
        // the shorthand's resets. Preserve the authored shorthand so WebScene can
        // expand it at the cascade boundary with the declaration's original
        // specificity, source order, and !important state.
        css = Regex.Replace(
            css,
            @"(?<prefix>^|[;{])(?<space>\s*)background\s*:\s*(?<value>[^;{}]*\bcurrentcolor\b[^;{}]*)(?=\s*[;}])",
            match => match.Groups["prefix"].Value + match.Groups["space"].Value
                     + ProtectedVariableShorthandPrefix + "background:"
                     + match.Groups["value"].Value,
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);
        css = Regex.Replace(
            css,
            @"(?<prefix>^|[;{])(?<space>\s*)(?<name>margin|padding|border-radius|border(?:-(?:top|right|bottom|left|width|style|color))?|outline|inset|overflow|gap|flex-flow|flex|background)\s*:\s*(?<value>[^;{}]*var\([^;{}]+\)[^;{}]*)(?=\s*[;}])",
            match => match.Groups["prefix"].Value + match.Groups["space"].Value
                     + ProtectedVariableShorthandPrefix + match.Groups["name"].Value.ToLowerInvariant() + ":"
                     + match.Groups["value"].Value,
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);

        if (disableGuards
            || (css.IndexOf("background", StringComparison.OrdinalIgnoreCase) >= 0
                && css.IndexOf("var(", StringComparison.OrdinalIgnoreCase) >= 0))
        {
            css = Regex.Replace(
                css,
                @"(?<prefix>^|[;{])(?<space>\s*)background\s*:\s*(?<value>var\([^;{}]+\))(?=\s*[;}])",
                match => match.Groups["prefix"].Value + match.Groups["space"].Value
                         + "background-color:" + match.Groups["value"].Value,
                RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);
        }

        foreach (var (logical, firstPhysical, secondPhysical) in s_ltrLogicalAxisShorthands)
        {
            if (!disableGuards && css.IndexOf(logical, StringComparison.OrdinalIgnoreCase) < 0)
            {
                continue;
            }
            css = ExpandLogicalAxisShorthand(css, logical, firstPhysical, secondPhysical);
        }

        foreach (var (logical, physical) in s_ltrLogicalProperties)
        {
            if (!disableGuards && css.IndexOf(logical, StringComparison.OrdinalIgnoreCase) < 0)
            {
                continue;
            }
            css = Regex.Replace(
                css,
                $@"(?<prefix>^|[;{{])(?<space>\s*){Regex.Escape(logical)}(?=\s*:)",
                match => match.Groups["prefix"].Value + match.Groups["space"].Value + physical,
                RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);
        }

        // CSS Grid's original gap spellings are aliases of the modern gap
        // properties. Expand before the third-party syntax parser so legacy
        // declarations participate in the same longhand cascade even when the
        // parser exposes only part of the shorthand.
        css = ExpandTwoAxisShorthand(css, "grid-gap", "row-gap", "column-gap");
        css = ExpandTwoAxisShorthand(css, "gap", "row-gap", "column-gap");
        css = Regex.Replace(
            css,
            @"(?<prefix>^|[;{])(?<space>\s*)grid-row-gap(?=\s*:)",
            match => match.Groups["prefix"].Value + match.Groups["space"].Value + "row-gap",
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);
        css = Regex.Replace(
            css,
            @"(?<prefix>^|[;{])(?<space>\s*)grid-column-gap(?=\s*:)",
            match => match.Groups["prefix"].Value + match.Groups["space"].Value + "column-gap",
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);

        // Browsers accept a unitless zero in transform rotation functions. The
        // third-party syntax parser currently rejects transform:rotate(0),
        // dropping the declaration (and sometimes its selector dependency)
        // before it reaches WebScene. Canonicalize only zero-valued rotate
        // functions; non-zero unitless angles remain invalid CSS.
        css = Regex.Replace(
            css,
            @"(?<prefix>(?:^|[;{])\s*(?:-webkit-)?transform\s*:\s*)(?<value>[^;{}]+)(?=\s*[;}])",
            match => match.Groups["prefix"].Value + Regex.Replace(
                match.Groups["value"].Value,
                @"(?<function>rotate(?:x|y|z)?)\(\s*[+-]?(?:0+(?:\.0*)?|\.0+)\s*\)",
                "${function}(0deg)",
                RegexOptions.IgnoreCase | RegexOptions.CultureInvariant),
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);
        return css;
    }

    private static string ProtectCustomPropertyNamesForParser(string css)
    {
        // AngleSharp's declaration map compares custom-property names without
        // the case sensitivity required by CSS Variables. Protect declaration
        // names and var() references with an ASCII-lowercase encoding before
        // parsing, then restore the exact code units in CollectRules.
        css = Regex.Replace(
            css,
            @"(?<prefix>^|[;{])(?<space>\s*)(?<name>--[-_a-zA-Z0-9\u0080-\uFFFF]+)(?=\s*:)",
            match => match.Groups["prefix"].Value + match.Groups["space"].Value
                     + ProtectCustomPropertyDeclarationName(match.Groups["name"].Value),
            RegexOptions.Multiline | RegexOptions.CultureInvariant);
        return Regex.Replace(
            css,
            @"(?<prefix>\bvar\(\s*)(?<name>--[-_a-zA-Z0-9\u0080-\uFFFF]+)",
            match => match.Groups["prefix"].Value
                     + ProtectCustomPropertyName(match.Groups["name"].Value),
            RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    }

    private static void CollectRules(
        ICssRuleList rules,
        ICollection<CssCompiledStyleRule> result,
        ICollection<CssCompiledFontFace> fontFaces,
        IReadOnlyList<string>? mediaQueries = null)
    {
        foreach (var rule in rules)
        {
            if (rule is ICssFontFaceRule fontFaceRule)
            {
                if (!string.IsNullOrWhiteSpace(fontFaceRule.Family)
                    && !string.IsNullOrWhiteSpace(fontFaceRule.Source))
                {
                    fontFaces.Add(new CssCompiledFontFace(
                        fontFaceRule.Family.Trim(),
                        fontFaceRule.Source.Trim(),
                        string.IsNullOrWhiteSpace(fontFaceRule.Style) ? "normal" : fontFaceRule.Style.Trim(),
                        string.IsNullOrWhiteSpace(fontFaceRule.Weight) ? "normal" : fontFaceRule.Weight.Trim(),
                        string.IsNullOrWhiteSpace(fontFaceRule.Stretch) ? "normal" : fontFaceRule.Stretch.Trim(),
                        mediaQueries ?? Array.Empty<string>()));
                }
            }
            else if (rule is ICssStyleRule styleRule)
            {
                if (string.IsNullOrWhiteSpace(styleRule.SelectorText)) continue;
                var restoredSelectorText = RestoreScrollbarPseudoElements(styleRule.SelectorText);
                foreach (var selectorText in CssSelectorSyntaxParser.SplitSelectorList(restoredSelectorText))
                {
                    if (!CssSelectorSyntaxParser.TryParse(selectorText, out var selector)) continue;
                    var declarations = new List<CssCascadeDeclaration>();
                    foreach (var property in styleRule.Style)
                    {
                        var name = NormalizePropertyName(
                            RestoreCustomPropertyNames(property.Name));
                        if (name.StartsWith(ProtectedVariableShorthandPrefix, StringComparison.Ordinal))
                        {
                            name = name[ProtectedVariableShorthandPrefix.Length..];
                        }
                        var rawValue = styleRule.Style.GetPropertyValue(property.Name);
                        if (rawValue is not null)
                        {
                            rawValue = RestoreCustomPropertyNames(rawValue);
                        }
                        var value = rawValue is null
                            ? null
                            : name.StartsWith("--", StringComparison.Ordinal)
                                ? CssCustomPropertySyntax.TrimWhitespace(rawValue)
                                : rawValue.Trim();
                        if (name == "list-style"
                            && value?.StartsWith(ProtectedCssWideValuePrefix, StringComparison.Ordinal) == true)
                        {
                            value = value[ProtectedCssWideValuePrefix.Length..];
                        }
                        if (!string.IsNullOrWhiteSpace(name) && !string.IsNullOrWhiteSpace(value))
                        {
                            declarations.Add(new CssCascadeDeclaration(name, value, property.IsImportant));
                        }
                    }
                    if (declarations.Count > 0)
                    {
                        result.Add(new CssCompiledStyleRule(
                            selector,
                            declarations,
                            mediaQueries ?? Array.Empty<string>()));
                    }
                }
            }
            else if (rule is ICssGroupingRule groupingRule)
            {
                if (rule is ICssMediaRule mediaRule)
                {
                    var media = mediaRule.Media.MediaText?.Trim();
                    var nested = string.IsNullOrWhiteSpace(media)
                        ? mediaQueries
                        : mediaQueries is null || mediaQueries.Count == 0
                            ? new[] { media }
                            : mediaQueries.Concat(new[] { media }).ToArray();
                    CollectRules(groupingRule.Rules, result, fontFaces, nested);
                }
                else
                {
                    CollectRules(groupingRule.Rules, result, fontFaces, mediaQueries);
                }
            }
        }
    }

    private static string ProtectScrollbarPseudoElementsForParser(string css)
    {
        return Regex.Replace(
            css,
            @"::-(webkit-scrollbar(?:-(?:thumb|track|corner))?)",
            match => ProtectedScrollbarPseudoClassPrefix
                     + match.Groups[1].Value.Replace('-', '_')
                     + "::before",
            RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    }

    private static string RestoreScrollbarPseudoElements(string selectorText)
    {
        return Regex.Replace(
            selectorText,
            Regex.Escape(ProtectedScrollbarPseudoClassPrefix)
            + @"(?<name>webkit_scrollbar(?:_(?:thumb|track|corner))?)::before",
            match => "::-"
                     + match.Groups["name"].Value.Replace('_', '-'),
            RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    }

    private static string ExpandLogicalAxisShorthand(
        string css,
        string logical,
        string firstPhysical,
        string secondPhysical)
        => Regex.Replace(
            css,
            $@"(?<prefix>^|[;{{])(?<space>\s*){Regex.Escape(logical)}\s*:\s*(?<value>[^;{{}}]+?)(?=\s*[;}}])",
            match =>
            {
                var value = match.Groups["value"].Value.Trim();
                var important = Regex.Match(value, @"\s*!important\s*$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
                var suffix = important.Success ? " !important" : string.Empty;
                if (important.Success) value = value[..important.Index].TrimEnd();
                var tokens = SplitTokens(value);
                if (tokens.Count is < 1 or > 2) return match.Value;
                var second = tokens.Count == 2 ? tokens[1] : tokens[0];
                return match.Groups["prefix"].Value + match.Groups["space"].Value
                       + firstPhysical + ":" + tokens[0] + suffix + ";"
                       + secondPhysical + ":" + second + suffix;
            },
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);

    private static string ExpandTwoAxisShorthand(
        string css,
        string shorthand,
        string firstLonghand,
        string secondLonghand)
        => Regex.Replace(
            css,
            $@"(?<prefix>^|[;{{])(?<space>\s*){Regex.Escape(shorthand)}\s*:\s*(?<value>[^;{{}}]+?)(?=\s*[;}}])",
            match =>
            {
                var value = match.Groups["value"].Value.Trim();
                var important = Regex.Match(value, @"\s*!important\s*$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
                var suffix = important.Success ? " !important" : string.Empty;
                if (important.Success) value = value[..important.Index].TrimEnd();
                var tokens = SplitTokens(value);
                if (tokens.Count is < 1 or > 2) return match.Value;
                var second = tokens.Count == 2 ? tokens[1] : tokens[0];
                return match.Groups["prefix"].Value + match.Groups["space"].Value
                       + firstLonghand + ":" + tokens[0] + suffix + ";"
                       + secondLonghand + ":" + second + suffix;
            },
            RegexOptions.IgnoreCase | RegexOptions.Multiline | RegexOptions.CultureInvariant);

    private static List<string> SplitTokens(string value)
    {
        var result = new List<string>();
        var current = new StringBuilder();
        var depth = 0;
        foreach (var ch in value)
        {
            if (ch == '(') depth++;
            else if (ch == ')') depth--;
            if (char.IsWhiteSpace(ch) && depth == 0)
            {
                if (current.Length > 0)
                {
                    result.Add(current.ToString());
                    current.Clear();
                }
            }
            else current.Append(ch);
        }
        if (current.Length > 0) result.Add(current.ToString());
        return result;
    }

    private static string ProtectCustomPropertyName(string name)
    {
        var builder = new StringBuilder(
            ProtectedCustomPropertyNamePrefix.Length + name.Length * 4);
        builder.Append(ProtectedCustomPropertyNamePrefix);
        foreach (var character in name)
        {
            builder.Append(((int)character).ToString("x4", System.Globalization.CultureInfo.InvariantCulture));
        }
        return builder.ToString();
    }

    private static string ProtectCustomPropertyDeclarationName(string name)
        // These names are WebScene's parser-transport declarations rather than
        // authored custom properties. They must remain recognizable so
        // CollectRules can restore the corresponding ordinary CSS property.
        => name.StartsWith(ProtectedVariableShorthandPrefix, StringComparison.Ordinal)
            ? name
            : ProtectCustomPropertyName(name);

    private static string RestoreCustomPropertyNames(string value)
        => Regex.Replace(
            value,
            Regex.Escape(ProtectedCustomPropertyNamePrefix) + @"(?<hex>(?:[0-9a-f]{4})+)",
            match =>
            {
                var hex = match.Groups["hex"].Value;
                var builder = new StringBuilder(hex.Length / 4);
                for (var index = 0; index < hex.Length; index += 4)
                {
                    builder.Append((char)int.Parse(
                        hex.AsSpan(index, 4),
                        System.Globalization.NumberStyles.HexNumber,
                        System.Globalization.CultureInfo.InvariantCulture));
                }
                return builder.ToString();
            },
            RegexOptions.CultureInvariant);

    private static string NormalizePropertyName(string name)
    {
        if (string.IsNullOrWhiteSpace(name)) return string.Empty;
        name = name.Trim();
        if (name.StartsWith("--", StringComparison.Ordinal))
        {
            return CssCustomPropertySyntax.IsValidName(name) ? name : string.Empty;
        }
        return name.ToLowerInvariant() switch
        {
            "grid-gap" => "gap",
            "grid-row-gap" => "row-gap",
            "grid-column-gap" => "column-gap",
            var normalized => normalized
        };
    }

    private static readonly (string Logical, string FirstPhysical, string SecondPhysical)[] s_ltrLogicalAxisShorthands =
    [
        ("padding-block", "padding-top", "padding-bottom"),
        ("padding-inline", "padding-left", "padding-right"),
        ("margin-block", "margin-top", "margin-bottom"),
        ("margin-inline", "margin-left", "margin-right"),
        ("inset-block", "top", "bottom"),
        ("inset-inline", "left", "right")
    ];

    private static readonly (string Logical, string Physical)[] s_ltrLogicalBorderShorthands =
    [
        ("border-inline-start", "border-left"),
        ("border-inline-end", "border-right")
    ];

    private static readonly (string Logical, string Physical)[] s_ltrLogicalProperties =
    [
        ("inset-inline-start", "left"), ("inset-inline-end", "right"),
        ("margin-inline-start", "margin-left"), ("margin-inline-end", "margin-right"),
        ("padding-inline-start", "padding-left"), ("padding-inline-end", "padding-right"),
        ("border-start-start-radius", "border-top-left-radius"),
        ("border-start-end-radius", "border-top-right-radius"),
        ("border-end-start-radius", "border-bottom-left-radius"),
        ("border-end-end-radius", "border-bottom-right-radius"),
        ("border-inline-start-width", "border-left-width"), ("border-inline-end-width", "border-right-width"),
        ("border-inline-start-color", "border-left-color"), ("border-inline-end-color", "border-right-color")
    ];
}
