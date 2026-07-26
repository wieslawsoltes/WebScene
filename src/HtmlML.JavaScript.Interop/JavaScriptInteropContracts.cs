using System.Text.Json;

namespace HtmlML.JavaScript.Interop;

/// <summary>An opaque JavaScript object stored in the runtime-side handle table.</summary>
[System.Text.Json.Serialization.JsonConverter(typeof(JavaScriptObjectReferenceJsonConverter))]
public readonly record struct JavaScriptObjectReference(long Id)
{
    public bool IsEmpty => Id <= 0;
}

public sealed class JavaScriptObjectReferenceJsonConverter
    : System.Text.Json.Serialization.JsonConverter<JavaScriptObjectReference>
{
    public override JavaScriptObjectReference Read(
        ref Utf8JsonReader reader,
        Type typeToConvert,
        JsonSerializerOptions options)
    {
        using var document = JsonDocument.ParseValue(ref reader);
        if (document.RootElement.ValueKind == JsonValueKind.Number)
        {
            return new JavaScriptObjectReference(document.RootElement.GetInt64());
        }
        if (document.RootElement.TryGetProperty("__htmlMlHandle", out var handle))
        {
            return new JavaScriptObjectReference(handle.GetInt64());
        }
        throw new JsonException("A JavaScript object reference marker was expected.");
    }

    public override void Write(
        Utf8JsonWriter writer,
        JavaScriptObjectReference value,
        JsonSerializerOptions options)
    {
        writer.WriteStartObject();
        writer.WriteNumber("__htmlMlHandle", value.Id);
        writer.WriteEndObject();
    }
}

[System.Text.Json.Serialization.JsonConverter(typeof(JavaScriptNullishJsonConverter))]
public readonly record struct JavaScriptNullish
{
    private JavaScriptNullish(bool isUndefined) => IsUndefined = isUndefined;

    public bool IsUndefined { get; }

    public static JavaScriptNullish Null { get; } = new(false);

    public static JavaScriptNullish Undefined { get; } = new(true);
}

public sealed class JavaScriptNullishJsonConverter
    : System.Text.Json.Serialization.JsonConverter<JavaScriptNullish>
{
    public override JavaScriptNullish Read(
        ref Utf8JsonReader reader,
        Type typeToConvert,
        JsonSerializerOptions options)
    {
        if (reader.TokenType == JsonTokenType.Null)
        {
            return JavaScriptNullish.Null;
        }
        using var document = JsonDocument.ParseValue(ref reader);
        if (document.RootElement.ValueKind == JsonValueKind.Object
            && document.RootElement.TryGetProperty(
                "__htmlMlUndefined",
                out var undefined)
            && undefined.ValueKind == JsonValueKind.True)
        {
            return JavaScriptNullish.Undefined;
        }
        throw new JsonException("A JavaScript null or undefined marker was expected.");
    }

    public override void Write(
        Utf8JsonWriter writer,
        JavaScriptNullish value,
        JsonSerializerOptions options)
    {
        if (!value.IsUndefined)
        {
            writer.WriteNullValue();
            return;
        }
        writer.WriteStartObject();
        writer.WriteBoolean("__htmlMlUndefined", true);
        writer.WriteEndObject();
    }
}

public interface IJavaScriptObjectReferenceProvider
{
    JavaScriptObjectReference JavaScriptReference { get; }
}

/// <summary>A pre-serialized JSON value passed across the JavaScript boundary.</summary>
public readonly record struct JavaScriptJson
{
    private JavaScriptJson(string value) => Value = value;

    public string Value { get; }

    public static JavaScriptJson Parse(string json)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(json);
        using var document = JsonDocument.Parse(json);
        return new JavaScriptJson(document.RootElement.GetRawText());
    }

    public static JavaScriptJson Serialize<T>(T value, JsonSerializerOptions? options = null)
        => new(JsonSerializer.Serialize(value, options ?? JavaScriptInteropJson.Options));
}

/// <summary>One JSON or object-reference argument in a generated JavaScript call.</summary>
public readonly record struct JavaScriptArgument
{
    private JavaScriptArgument(string json) => Json = json;

    public string Json { get; }

    public static JavaScriptArgument From(JavaScriptJson value) => new(value.Value);

    public static JavaScriptArgument From(JavaScriptObjectReference value)
        => new($"{{\"__htmlMlHandle\":{value.Id}}}");

    public static JavaScriptArgument From(JavaScriptNullish value)
        => value.IsUndefined
            ? Undefined
            : new("null");

    public static JavaScriptArgument Undefined { get; }
        = new("{\"__htmlMlUndefined\":true}");

    public static JavaScriptArgument From<T>(T value)
        => new(JsonSerializer.Serialize(value, JavaScriptInteropJson.Options));

    public static JavaScriptArgument FromObject(object? value)
        => new(JsonSerializer.Serialize(
            value,
            value?.GetType() ?? typeof(object),
            JavaScriptInteropJson.Options));

    public static JavaScriptArgument FromRest<T>(T values)
        => new("{\"__htmlMlRest\":"
               + JsonSerializer.Serialize(values, JavaScriptInteropJson.Options)
               + "}");
}

[System.Text.Json.Serialization.JsonConverter(
    typeof(JavaScriptOptionalJsonConverterFactory))]
public readonly record struct JavaScriptOptional<T>
{
    public JavaScriptOptional(T? value)
    {
        HasValue = true;
        Value = value;
    }

    public bool HasValue { get; }

    public T? Value { get; }

    public JavaScriptArgument ToArgument()
        => HasValue
            ? JavaScriptArgument.From(Value)
            : JavaScriptArgument.Undefined;

    public static implicit operator JavaScriptOptional<T>(T? value) => new(value);
}

public sealed class JavaScriptOptionalJsonConverterFactory
    : System.Text.Json.Serialization.JsonConverterFactory
{
    public override bool CanConvert(Type typeToConvert)
        => typeToConvert.IsGenericType
           && typeToConvert.GetGenericTypeDefinition()
           == typeof(JavaScriptOptional<>);

    public override System.Text.Json.Serialization.JsonConverter CreateConverter(
        Type typeToConvert,
        JsonSerializerOptions options)
        => (System.Text.Json.Serialization.JsonConverter)Activator.CreateInstance(
            typeof(JavaScriptOptionalJsonConverter<>).MakeGenericType(
                typeToConvert.GetGenericArguments()))!;

    private sealed class JavaScriptOptionalJsonConverter<T>
        : System.Text.Json.Serialization.JsonConverter<JavaScriptOptional<T>>
    {
        public override JavaScriptOptional<T> Read(
            ref Utf8JsonReader reader,
            Type typeToConvert,
            JsonSerializerOptions options)
            => new(JsonSerializer.Deserialize<T>(ref reader, options));

        public override void Write(
            Utf8JsonWriter writer,
            JavaScriptOptional<T> value,
            JsonSerializerOptions options)
        {
            if (!value.HasValue)
            {
                writer.WriteNullValue();
                return;
            }
            JsonSerializer.Serialize(writer, value.Value, options);
        }
    }
}

internal static class JavaScriptInteropJson
{
    public static JsonSerializerOptions Options { get; } = CreateOptions();

    private static JsonSerializerOptions CreateOptions()
    {
        var options = new JsonSerializerOptions(JsonSerializerDefaults.Web);
        options.Converters.Add(new JavaScriptValueTupleJsonConverterFactory());
        options.Converters.Add(new JavaScriptObjectReferenceProviderConverterFactory());
        return options;
    }

    private sealed class JavaScriptObjectReferenceProviderConverterFactory
        : System.Text.Json.Serialization.JsonConverterFactory
    {
        public override bool CanConvert(Type typeToConvert)
            => typeof(IJavaScriptObjectReferenceProvider).IsAssignableFrom(typeToConvert);

        public override System.Text.Json.Serialization.JsonConverter CreateConverter(
            Type typeToConvert,
            JsonSerializerOptions options)
            => (System.Text.Json.Serialization.JsonConverter)Activator.CreateInstance(
                typeof(ProviderConverter<>).MakeGenericType(typeToConvert))!;

        private sealed class ProviderConverter<TProvider>
            : System.Text.Json.Serialization.JsonConverter<TProvider>
            where TProvider : IJavaScriptObjectReferenceProvider
        {
            public override TProvider Read(
                ref Utf8JsonReader reader,
                Type typeToConvert,
                JsonSerializerOptions options)
                => throw new NotSupportedException(
                    "JavaScript object references cannot be deserialized as providers.");

            public override void Write(
                Utf8JsonWriter writer,
                TProvider value,
                JsonSerializerOptions options)
            {
                writer.WriteStartObject();
                writer.WriteNumber("__htmlMlHandle", value.JavaScriptReference.Id);
                writer.WriteEndObject();
            }
        }
    }
}

/// <summary>
/// Converts captured JSON fragments using the same built-in converters as the
/// generated native-engine interop layer.
/// </summary>
public static class JavaScriptInteropSerializer
{
    public static T Deserialize<T>(JsonElement value)
        => value.Deserialize<T>(JavaScriptInteropJson.Options)!;
}

/// <summary>
/// Small runtime boundary targeted by generated bindings. Implementations own engine
/// affinity; generated proxies only describe calls and retain opaque object handles.
/// </summary>
public interface IJavaScriptInvoker
{
    ValueTask<JavaScriptObjectReference> GetGlobalObjectAsync(
        string globalName,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support global object properties.");

    ValueTask<T?> GetGlobalAsync<T>(
        string globalName,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support global properties.");

    ValueTask<T?> GetGlobalPromiseAsync<T>(
        string globalName,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support promise-valued global properties.");

    ValueTask<JavaScriptObjectReference> GetGlobalPromiseObjectAsync(
        string globalName,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support object promises in global properties.");

    ValueTask<JavaScriptObjectReference> InvokeGlobalObjectAsync(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support global object-returning functions.");

    ValueTask<T?> InvokeGlobalAsync<T>(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support global functions.");

    ValueTask<T?> InvokeGlobalPromiseAsync<T>(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support promise-returning global functions.");

    ValueTask<JavaScriptObjectReference> InvokeGlobalPromiseObjectAsync(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support promise-object global functions.");

    ValueTask InvokeGlobalVoidAsync(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support global void functions.");

    ValueTask<JavaScriptObjectReference> ConstructAsync(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default);

    ValueTask<JavaScriptObjectReference> InvokeObjectAsync(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default);

    ValueTask<JavaScriptObjectReference> GetObjectPropertyAsync(
        JavaScriptObjectReference target,
        string property,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support object properties.");

    ValueTask<T?> GetPropertyAsync<T>(
        JavaScriptObjectReference target,
        string property,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support properties.");

    ValueTask<T?> GetPromisePropertyAsync<T>(
        JavaScriptObjectReference target,
        string property,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support promise-valued properties.");

    ValueTask<JavaScriptObjectReference> GetPromiseObjectPropertyAsync(
        JavaScriptObjectReference target,
        string property,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support object promises in properties.");

    ValueTask SetPropertyAsync(
        JavaScriptObjectReference target,
        string property,
        JavaScriptArgument value,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This JavaScript invoker does not support writable properties.");

    ValueTask<T?> InvokeAsync<T>(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default);

    ValueTask<T?> InvokePromiseAsync<T>(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default);

    ValueTask<JavaScriptObjectReference> InvokePromiseObjectAsync(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default);

    ValueTask InvokeVoidAsync(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default);

    ValueTask ReleaseAsync(
        JavaScriptObjectReference reference,
        CancellationToken cancellationToken = default);
}
