using System.Globalization;

namespace WebScene.Css;

/// <summary>
/// Parses the WebScene CSS length profile, retaining affine percentage/pixel values
/// until layout supplies the containing-block reference.
/// </summary>
public static class CssLengthParser
{
    public static bool TryParse(string? value, out CssLayoutLength? length)
    {
        length = null;
        if (string.IsNullOrWhiteSpace(value))
        {
            return true;
        }

        var trimmed = value.AsSpan().Trim();
        if (trimmed.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            length = CssLayoutLength.Auto;
            return true;
        }

        if (TryParseAbsoluteLength(value, out var absolutePixels))
        {
            length = CssLayoutLength.Pixels(absolutePixels);
            return true;
        }

        var unit = CssLayoutLengthUnit.Pixel;
        if (trimmed[^1] == '%')
        {
            unit = CssLayoutLengthUnit.Percent;
            trimmed = trimmed[..^1];
        }
        else if (trimmed.EndsWith("px".AsSpan(), StringComparison.OrdinalIgnoreCase))
        {
            trimmed = trimmed[..^2];
        }

        if (double.TryParse(trimmed, NumberStyles.Float, CultureInfo.InvariantCulture, out var numeric)
            && double.IsFinite(numeric))
        {
            length = unit == CssLayoutLengthUnit.Percent
                ? CssLayoutLength.Percent(numeric)
                : CssLayoutLength.Pixels(numeric);
            return true;
        }

        if (!ExpressionParser.TryEvaluate(value, out var expression))
        {
            return false;
        }

        length = expression.Unit == NumericUnit.Percent
            ? CssLayoutLength.Percent(expression.Value, expression.PixelOffset)
            : CssLayoutLength.Pixels(expression.Value);
        return true;
    }

    public static bool TryParseAbsoluteLength(string? value, out double pixels)
    {
        pixels = 0;
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var trimmed = value.AsSpan().Trim();
        var factor = 0d;
        var unitLength = 0;
        if (trimmed.EndsWith("px".AsSpan(), StringComparison.OrdinalIgnoreCase))
        {
            factor = 1;
            unitLength = 2;
        }
        else if (trimmed.EndsWith("in".AsSpan(), StringComparison.OrdinalIgnoreCase))
        {
            factor = 96;
            unitLength = 2;
        }
        else if (trimmed.EndsWith("cm".AsSpan(), StringComparison.OrdinalIgnoreCase))
        {
            factor = 96d / 2.54d;
            unitLength = 2;
        }
        else if (trimmed.EndsWith("mm".AsSpan(), StringComparison.OrdinalIgnoreCase))
        {
            factor = 96d / 25.4d;
            unitLength = 2;
        }
        else if (trimmed.EndsWith("pt".AsSpan(), StringComparison.OrdinalIgnoreCase))
        {
            factor = 96d / 72d;
            unitLength = 2;
        }
        else if (trimmed.EndsWith("pc".AsSpan(), StringComparison.OrdinalIgnoreCase))
        {
            factor = 16;
            unitLength = 2;
        }
        else if (trimmed.EndsWith("q".AsSpan(), StringComparison.OrdinalIgnoreCase))
        {
            factor = 96d / 101.6d;
            unitLength = 1;
        }
        else
        {
            return false;
        }

        return double.TryParse(trimmed[..^unitLength], NumberStyles.Float, CultureInfo.InvariantCulture, out var numeric)
               && double.IsFinite(numeric)
               && double.IsFinite(pixels = numeric * factor);
    }

    private enum NumericUnit
    {
        Number,
        Pixel,
        Percent
    }

    private readonly record struct NumericValue(double Value, NumericUnit Unit, double PixelOffset = 0);

    private ref struct ExpressionParser
    {
        private readonly ReadOnlySpan<char> _source;
        private int _position;

        private ExpressionParser(ReadOnlySpan<char> source)
        {
            _source = source;
            _position = 0;
        }

        internal static bool TryEvaluate(string source, out NumericValue value)
        {
            var parser = new ExpressionParser(source.AsSpan());
            if (!parser.TryParseExpression(out value))
            {
                return false;
            }

            parser.SkipWhitespace();
            return parser._position == parser._source.Length && double.IsFinite(value.Value);
        }

        private bool TryParseExpression(out NumericValue value)
        {
            if (!TryParseTerm(out value))
            {
                return false;
            }

            while (true)
            {
                SkipWhitespace();
                if (!TryConsume('+') && !TryConsume('-'))
                {
                    return true;
                }

                var operation = _source[_position - 1];
                if (!TryParseTerm(out var right)
                    || !TryAdd(value, right, operation == '-' ? -1 : 1, out value))
                {
                    return false;
                }
            }
        }

        private bool TryParseTerm(out NumericValue value)
        {
            if (!TryParseUnary(out value))
            {
                return false;
            }

            while (true)
            {
                SkipWhitespace();
                if (!TryConsume('*') && !TryConsume('/'))
                {
                    return true;
                }

                var operation = _source[_position - 1];
                if (!TryParseUnary(out var right) || !TryMultiply(value, right, operation, out value))
                {
                    return false;
                }
            }
        }

        private bool TryParseUnary(out NumericValue value)
        {
            SkipWhitespace();
            if (TryConsume('+'))
            {
                return TryParseUnary(out value);
            }

            if (TryConsume('-'))
            {
                if (!TryParseUnary(out value))
                {
                    return false;
                }

                value = value with { Value = -value.Value, PixelOffset = -value.PixelOffset };
                return true;
            }

            return TryParsePrimary(out value);
        }

        private bool TryParsePrimary(out NumericValue value)
        {
            value = default;
            SkipWhitespace();
            if (TryConsume('('))
            {
                return TryParseExpression(out value) && ConsumeClosingParenthesis();
            }

            if (_position < _source.Length && char.IsLetter(_source[_position]))
            {
                var start = _position;
                while (_position < _source.Length && char.IsLetter(_source[_position]))
                {
                    _position++;
                }

                var name = _source[start.._position];
                SkipWhitespace();
                if (!TryConsume('('))
                {
                    return false;
                }

                if (name.Equals("calc", StringComparison.OrdinalIgnoreCase))
                {
                    return TryParseExpression(out value) && ConsumeClosingParenthesis();
                }

                if (!name.Equals("min", StringComparison.OrdinalIgnoreCase)
                    && !name.Equals("max", StringComparison.OrdinalIgnoreCase))
                {
                    return false;
                }

                if (!TryParseExpression(out value))
                {
                    return false;
                }

                while (true)
                {
                    SkipWhitespace();
                    if (!TryConsume(','))
                    {
                        break;
                    }

                    if (!TryParseExpression(out var candidate)
                        || !TryComparable(value, candidate, out var left, out var right))
                    {
                        return false;
                    }

                    value = name.Equals("max", StringComparison.OrdinalIgnoreCase)
                        ? left.Value >= right.Value ? left : right
                        : left.Value <= right.Value ? left : right;
                }

                return ConsumeClosingParenthesis();
            }

            var numberStart = _position;
            var hasDigit = false;
            while (_position < _source.Length && char.IsDigit(_source[_position]))
            {
                hasDigit = true;
                _position++;
            }

            if (_position < _source.Length && _source[_position] == '.')
            {
                _position++;
                while (_position < _source.Length && char.IsDigit(_source[_position]))
                {
                    hasDigit = true;
                    _position++;
                }
            }

            if (!hasDigit
                || !double.TryParse(_source[numberStart.._position], NumberStyles.Float, CultureInfo.InvariantCulture, out var numeric))
            {
                return false;
            }

            var unit = NumericUnit.Number;
            if (TryConsume('%'))
            {
                unit = NumericUnit.Percent;
            }
            else if (_position + 2 <= _source.Length
                     && _source.Slice(_position, 2).Equals("px", StringComparison.OrdinalIgnoreCase))
            {
                _position += 2;
                unit = NumericUnit.Pixel;
            }

            value = new NumericValue(numeric, unit);
            return true;
        }

        private static bool TryAdd(NumericValue left, NumericValue right, int sign, out NumericValue result)
        {
            if (TryComparable(left, right, out left, out right))
            {
                result = new NumericValue(
                    left.Value + sign * right.Value,
                    left.Unit,
                    left.PixelOffset + sign * right.PixelOffset);
                return true;
            }

            if ((left.Unit is NumericUnit.Percent or NumericUnit.Pixel)
                && (right.Unit is NumericUnit.Percent or NumericUnit.Pixel))
            {
                var percent = (left.Unit == NumericUnit.Percent ? left.Value : 0)
                              + sign * (right.Unit == NumericUnit.Percent ? right.Value : 0);
                var pixels = (left.Unit == NumericUnit.Pixel ? left.Value : left.PixelOffset)
                             + sign * (right.Unit == NumericUnit.Pixel ? right.Value : right.PixelOffset);
                result = percent == 0
                    ? new NumericValue(pixels, NumericUnit.Pixel)
                    : new NumericValue(percent, NumericUnit.Percent, pixels);
                return true;
            }

            result = default;
            return false;
        }

        private static bool TryComparable(
            NumericValue left,
            NumericValue right,
            out NumericValue normalizedLeft,
            out NumericValue normalizedRight)
        {
            normalizedLeft = left;
            normalizedRight = right;
            if (left.Unit == right.Unit)
            {
                return left.Unit != NumericUnit.Percent || left.PixelOffset == right.PixelOffset;
            }

            if (left.Unit == NumericUnit.Number && left.Value == 0)
            {
                normalizedLeft = left with { Unit = right.Unit };
                return true;
            }

            if (right.Unit == NumericUnit.Number && right.Value == 0)
            {
                normalizedRight = right with { Unit = left.Unit };
                return true;
            }

            return false;
        }

        private static bool TryMultiply(NumericValue left, NumericValue right, char operation, out NumericValue result)
        {
            result = default;
            if (operation == '/')
            {
                if (right.Unit != NumericUnit.Number || right.Value == 0)
                {
                    return false;
                }

                result = left with
                {
                    Value = left.Value / right.Value,
                    PixelOffset = left.PixelOffset / right.Value
                };
                return true;
            }

            if (left.Unit == NumericUnit.Number)
            {
                result = new NumericValue(left.Value * right.Value, right.Unit, left.Value * right.PixelOffset);
                return true;
            }

            if (right.Unit == NumericUnit.Number)
            {
                result = new NumericValue(left.Value * right.Value, left.Unit, right.Value * left.PixelOffset);
                return true;
            }

            return false;
        }

        private bool ConsumeClosingParenthesis()
        {
            SkipWhitespace();
            return TryConsume(')');
        }

        private void SkipWhitespace()
        {
            while (_position < _source.Length && char.IsWhiteSpace(_source[_position]))
            {
                _position++;
            }
        }

        private bool TryConsume(char expected)
        {
            if (_position >= _source.Length || _source[_position] != expected)
            {
                return false;
            }

            _position++;
            return true;
        }
    }
}
