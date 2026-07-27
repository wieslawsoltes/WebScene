using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace WebScene.JavaScript.Interop;

/// <summary>
/// Encodes C# value tuples as JavaScript arrays. The converter also flattens the
/// nested <see cref="ValueTuple{T1,T2,T3,T4,T5,T6,T7,TRest}"/> representation
/// used by C# tuples with more than seven elements.
/// </summary>
internal sealed class JavaScriptValueTupleJsonConverterFactory
    : JsonConverterFactory
{
    public override bool CanConvert(Type typeToConvert)
    {
        if (typeToConvert == typeof(ValueTuple))
        {
            return true;
        }
        if (!typeToConvert.IsGenericType)
        {
            return false;
        }
        var definition = typeToConvert.GetGenericTypeDefinition();
        return definition == typeof(ValueTuple<>)
               || definition == typeof(ValueTuple<,>)
               || definition == typeof(ValueTuple<,,>)
               || definition == typeof(ValueTuple<,,,>)
               || definition == typeof(ValueTuple<,,,,>)
               || definition == typeof(ValueTuple<,,,,,>)
               || definition == typeof(ValueTuple<,,,,,,>)
               || definition == typeof(ValueTuple<,,,,,,,>);
    }

    public override JsonConverter CreateConverter(
        Type typeToConvert,
        JsonSerializerOptions options)
        => (JsonConverter)Activator.CreateInstance(
            typeof(JavaScriptValueTupleJsonConverter<>).MakeGenericType(
                typeToConvert))!;

    private sealed class JavaScriptValueTupleJsonConverter<TTuple>
        : JsonConverter<TTuple>
    {
        private static readonly Type[] ElementTypes =
            GetElementTypes(typeof(TTuple)).ToArray();

        public override TTuple Read(
            ref Utf8JsonReader reader,
            Type typeToConvert,
            JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartArray)
            {
                throw new JsonException("A JavaScript array was expected for a tuple.");
            }

            var values = new object?[ElementTypes.Length];
            for (var index = 0; index < ElementTypes.Length; index++)
            {
                if (!reader.Read() || reader.TokenType == JsonTokenType.EndArray)
                {
                    throw new JsonException(
                        $"The JavaScript tuple requires {ElementTypes.Length} elements.");
                }
                values[index] = JsonSerializer.Deserialize(
                    ref reader,
                    ElementTypes[index],
                    options);
            }
            if (!reader.Read() || reader.TokenType != JsonTokenType.EndArray)
            {
                throw new JsonException(
                    $"The JavaScript tuple requires exactly {ElementTypes.Length} elements.");
            }

            var valueIndex = 0;
            return (TTuple)CreateTuple(typeof(TTuple), values, ref valueIndex);
        }

        public override void Write(
            Utf8JsonWriter writer,
            TTuple value,
            JsonSerializerOptions options)
        {
            writer.WriteStartArray();
            var tuple = (ITuple)(object)value!;
            for (var index = 0; index < ElementTypes.Length; index++)
            {
                JsonSerializer.Serialize(
                    writer,
                    tuple[index],
                    ElementTypes[index],
                    options);
            }
            writer.WriteEndArray();
        }

        private static IEnumerable<Type> GetElementTypes(Type tupleType)
        {
            if (tupleType == typeof(ValueTuple))
            {
                yield break;
            }

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
                foreach (var element in GetElementTypes(arguments[7]))
                {
                    yield return element;
                }
            }
        }

        private static object CreateTuple(
            Type tupleType,
            IReadOnlyList<object?> values,
            ref int valueIndex)
        {
            if (tupleType == typeof(ValueTuple))
            {
                return default(ValueTuple);
            }

            var arguments = tupleType.GetGenericArguments();
            var hasRest = arguments.Length == 8
                          && typeof(ITuple).IsAssignableFrom(arguments[7]);
            var directCount = hasRest ? 7 : arguments.Length;
            var constructorArguments = new object?[arguments.Length];
            for (var index = 0; index < directCount; index++)
            {
                constructorArguments[index] = values[valueIndex++];
            }
            if (hasRest)
            {
                constructorArguments[7] = CreateTuple(
                    arguments[7],
                    values,
                    ref valueIndex);
            }
            return Activator.CreateInstance(tupleType, constructorArguments)!;
        }
    }
}
