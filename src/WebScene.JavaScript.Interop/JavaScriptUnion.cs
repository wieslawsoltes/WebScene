using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace WebScene.JavaScript.Interop;

public interface IJavaScriptUnion
{
    object? Value { get; }

    bool TryGet<T>(out T? value);
}

/// <summary>
/// Represents a union with an arbitrary number of branches. <typeparamref
/// name="TBranches"/> is a value-tuple type whose elements are the branch
/// types. Generated bindings use this representation when a union has more
/// branches than the fixed-arity convenience types below.
/// </summary>
[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<TBranches>(object? Value)
    : IJavaScriptUnion
{
    public static JavaScriptUnion<TBranches> From<T>(T value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2>(T2 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2, T3>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3>(T2 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3>(T3 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4>(T2 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4>(T3 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4>(T4 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5>(T2 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5>(T3 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5>(T4 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5>(T5 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6>(T2 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6>(T3 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6>(T4 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6>(T5 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6>(T6 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7>(T2 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7>(T3 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7>(T4 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7>(T5 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7>(T6 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7>(T7 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8>(T2 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8>(T3 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8>(T4 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8>(T5 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8>(T6 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8>(T7 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8>(T8 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9>(T2 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9>(T3 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9>(T4 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9>(T5 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9>(T6 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9>(T7 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9>(T8 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9>(T9 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(T2 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(T3 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(T4 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(T5 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(T6 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(T7 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(T8 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(T9 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(T10 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T2 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T3 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T4 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T5 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T6 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T7 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T8 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T9 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T10 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(T11 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(object? Value) : IJavaScriptUnion
{
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T1 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T2 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T3 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T4 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T5 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T6 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T7 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T8 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T9 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T10 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T11 value) => new(value);
    public static implicit operator JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(T12 value) => new(value);
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13>(object? Value) : IJavaScriptUnion
{
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14>(object? Value) : IJavaScriptUnion
{
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15>(object? Value) : IJavaScriptUnion
{
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

[JsonConverter(typeof(JavaScriptUnionJsonConverterFactory))]
public readonly record struct JavaScriptUnion<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16>(object? Value) : IJavaScriptUnion
{
    public bool TryGet<T>(out T? value) => JavaScriptUnion.TryGet(Value, out value);
}

internal static class JavaScriptUnion
{
    public static bool TryGet<T>(object? source, out T? value)
    {
        if (source is T match)
        {
            value = match;
            return true;
        }
        value = default;
        return false;
    }
}

public sealed class JavaScriptUnionJsonConverterFactory : JsonConverterFactory
{
    public override bool CanConvert(Type typeToConvert)
        => typeToConvert.IsGenericType
           && typeToConvert.GetGenericTypeDefinition().Name.StartsWith(
               "JavaScriptUnion`",
               StringComparison.Ordinal);

    public override JsonConverter CreateConverter(
        Type typeToConvert,
        JsonSerializerOptions options)
        => (JsonConverter)Activator.CreateInstance(
            typeof(JavaScriptUnionJsonConverter<>).MakeGenericType(typeToConvert))!;

    private sealed class JavaScriptUnionJsonConverter<TUnion> : JsonConverter<TUnion>
        where TUnion : struct, IJavaScriptUnion
    {
        private static readonly Type[] CandidateTypes =
            GetCandidateTypes(typeof(TUnion)).ToArray();

        public override TUnion Read(
            ref Utf8JsonReader reader,
            Type typeToConvert,
            JsonSerializerOptions options)
        {
            using var document = JsonDocument.ParseValue(ref reader);
            var raw = document.RootElement.GetRawText();
            foreach (var candidate in CandidateTypes)
            {
                try
                {
                    var value = JsonSerializer.Deserialize(raw, candidate, options);
                    if (value is not null)
                    {
                        return (TUnion)Activator.CreateInstance(typeof(TUnion), value)!;
                    }
                }
                catch (JsonException)
                {
                    // The payload belongs to another union branch.
                }
            }
            throw new JsonException($"The JSON value does not match {typeof(TUnion)}.");
        }

        public override void Write(
            Utf8JsonWriter writer,
            TUnion value,
            JsonSerializerOptions options)
            => JsonSerializer.Serialize(
                writer,
                value.Value,
                value.Value?.GetType() ?? typeof(object),
                options);

        private static IEnumerable<Type> GetCandidateTypes(Type unionType)
        {
            var arguments = unionType.GetGenericArguments();
            if (arguments.Length != 1 || !typeof(ITuple).IsAssignableFrom(arguments[0]))
            {
                return arguments;
            }
            return GetTupleElementTypes(arguments[0]);
        }

        private static IEnumerable<Type> GetTupleElementTypes(Type tupleType)
        {
            var arguments = tupleType.GetGenericArguments();
            var hasRest = arguments.Length == 8
                          && typeof(ITuple).IsAssignableFrom(arguments[7]);
            var directCount = hasRest ? 7 : arguments.Length;
            for (var index = 0; index < directCount; index++)
            {
                yield return arguments[index];
            }
            if (hasRest)
            {
                foreach (var element in GetTupleElementTypes(arguments[7]))
                {
                    yield return element;
                }
            }
        }
    }
}
