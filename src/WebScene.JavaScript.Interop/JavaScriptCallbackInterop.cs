using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace WebScene.JavaScript.Interop;

public enum JavaScriptCallbackReturnKind
{
    Void,
    Promise,
    Synchronous
}

public readonly record struct JavaScriptCallbackMethod(
    string Name,
    JavaScriptCallbackReturnKind ReturnKind,
    JavaScriptJson SynchronousResult = default);

public interface IJavaScriptCallbackTarget
{
    ValueTask<object?> DispatchAsync(
        string method,
        JsonElement arguments,
        CancellationToken cancellationToken = default);
}

public delegate ValueTask<object?> JavaScriptCallbackHandler(
    JsonElement arguments,
    CancellationToken cancellationToken);

public interface IJavaScriptBidirectionalInvoker : IJavaScriptInvoker
{
    ValueTask<JavaScriptObjectReference> RegisterCallbackTargetAsync(
        IJavaScriptCallbackTarget target,
        IReadOnlyList<JavaScriptCallbackMethod> methods,
        CancellationToken cancellationToken = default);

    ValueTask<bool> PumpCallbackAsync(
        CancellationToken cancellationToken = default);

    ValueTask<JavaScriptFunctionReference> RegisterFunctionAsync(
        JavaScriptCallbackHandler callback,
        JavaScriptCallbackReturnKind returnKind = JavaScriptCallbackReturnKind.Void,
        CancellationToken cancellationToken = default);

    ValueTask<JavaScriptFunctionReference> RegisterSynchronousFactoryAsync(
        JavaScriptObjectReference result,
        JavaScriptCallbackHandler callback,
        CancellationToken cancellationToken = default)
        => throw new NotSupportedException(
            "This invoker does not support synchronous JavaScript factories.");

    ValueTask<T?> InvokeFunctionAsync<T>(
        JavaScriptObjectReference function,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default);

    ValueTask InvokeFunctionVoidAsync(
        JavaScriptObjectReference function,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default);
}

public sealed class JavaScriptFunctionReference
    : IJavaScriptObjectReferenceProvider, IAsyncDisposable
{
    private readonly IJavaScriptInvoker _invoker;

    public JavaScriptFunctionReference(
        IJavaScriptInvoker invoker,
        JavaScriptObjectReference reference)
    {
        _invoker = invoker;
        Reference = reference;
    }

    public JavaScriptObjectReference Reference { get; }

    public JavaScriptObjectReference JavaScriptReference => Reference;

    public static ValueTask<JavaScriptFunctionReference> CreateAsync(
        IJavaScriptBidirectionalInvoker invoker,
        JavaScriptCallbackHandler callback,
        JavaScriptCallbackReturnKind returnKind = JavaScriptCallbackReturnKind.Void,
        CancellationToken cancellationToken = default)
        => invoker.RegisterFunctionAsync(callback, returnKind, cancellationToken);

    public ValueTask<T?> InvokeAsync<T>(
        CancellationToken cancellationToken = default,
        params JavaScriptArgument[] arguments)
        => BidirectionalInvoker.InvokeFunctionAsync<T>(
            Reference,
            arguments,
            cancellationToken);

    public ValueTask InvokeVoidAsync(
        CancellationToken cancellationToken = default,
        params JavaScriptArgument[] arguments)
        => BidirectionalInvoker.InvokeFunctionVoidAsync(
            Reference,
            arguments,
            cancellationToken);

    public ValueTask DisposeAsync() => _invoker.ReleaseAsync(Reference);

    private IJavaScriptBidirectionalInvoker BidirectionalInvoker
        => _invoker as IJavaScriptBidirectionalInvoker
           ?? throw new NotSupportedException(
               "This JavaScript invoker cannot invoke function references.");
}

public static class JavaScriptCallbackArguments
{
    public static bool HasValue(JsonElement arguments, int index)
    {
        ValidateArguments(arguments);
        if ((uint)index >= (uint)arguments.GetArrayLength())
        {
            return false;
        }
        var argument = arguments[index];
        return argument.ValueKind != JsonValueKind.Object
               || !argument.TryGetProperty("__webSceneUndefined", out var undefined)
               || undefined.ValueKind != JsonValueKind.True;
    }

    public static bool IsNull(JsonElement arguments, int index)
    {
        ValidateArguments(arguments);
        return (uint)index < (uint)arguments.GetArrayLength()
               && arguments[index].ValueKind == JsonValueKind.Null;
    }

    public static JavaScriptOptional<T> GetOptional<T>(
        JsonElement arguments,
        int index,
        IJavaScriptBidirectionalInvoker invoker,
        JsonSerializerOptions? options = null)
        => HasValue(arguments, index)
            ? new JavaScriptOptional<T>(Get<T>(arguments, index, invoker, options))
            : default;

    public static JavaScriptFunctionReference GetFunctionReference(
        JsonElement arguments,
        int index,
        IJavaScriptBidirectionalInvoker invoker)
        => Get<JavaScriptFunctionReference>(arguments, index, invoker)
           ?? throw new JsonException("A JavaScript callback function was expected.");

    public static T? Get<T>(
        JsonElement arguments,
        int index,
        IJavaScriptBidirectionalInvoker invoker,
        JsonSerializerOptions? options = null)
    {
        ValidateArguments(arguments);
        if ((uint)index >= (uint)arguments.GetArrayLength())
        {
            return default;
        }
        var argument = arguments[index];
        if (typeof(T) == typeof(JavaScriptFunctionReference))
        {
            var marker = argument.Deserialize<JavaScriptReferenceMarker>(options)
                         ?? throw new JsonException("A JavaScript function handle was expected.");
            object reference = new JavaScriptFunctionReference(
                invoker,
                new JavaScriptObjectReference(marker.Handle));
            return (T)reference;
        }
        return argument.Deserialize<T>(options);
    }

    private static void ValidateArguments(JsonElement arguments)
    {
        if (arguments.ValueKind != JsonValueKind.Array)
        {
            throw new JsonException("JavaScript callback arguments must be an array.");
        }
    }

    private sealed record JavaScriptReferenceMarker(
        [property: JsonPropertyName("__webSceneHandle")] long Handle);
}

public abstract class JavaScriptActionBase : IJavaScriptObjectReferenceProvider, IAsyncDisposable
{
    private protected JavaScriptActionBase(
        IJavaScriptBidirectionalInvoker invoker,
        JavaScriptFunctionReference function)
    {
        Invoker = invoker;
        Function = function;
    }

    private protected IJavaScriptBidirectionalInvoker Invoker { get; }

    private protected JavaScriptFunctionReference Function { get; }

    public JavaScriptObjectReference JavaScriptReference => Function.Reference;

    public ValueTask DisposeAsync() => Function.DisposeAsync();

    private protected ValueTask InvokeCoreAsync(
        CancellationToken cancellationToken,
        params JavaScriptArgument[] arguments)
        => Invoker.InvokeFunctionVoidAsync(
            Function.Reference,
            arguments,
            cancellationToken);
}

public sealed class JavaScriptAction(
    IJavaScriptBidirectionalInvoker invoker,
    JavaScriptFunctionReference function)
    : JavaScriptActionBase(invoker, function)
{
    public ValueTask InvokeAsync(CancellationToken cancellationToken = default)
        => InvokeCoreAsync(cancellationToken);
}

public sealed class JavaScriptAction<T1>(
    IJavaScriptBidirectionalInvoker invoker,
    JavaScriptFunctionReference function)
    : JavaScriptActionBase(invoker, function)
{
    public ValueTask InvokeAsync(
        T1 argument1,
        CancellationToken cancellationToken = default)
        => InvokeCoreAsync(cancellationToken, JavaScriptArgument.From(argument1));
}

public sealed class JavaScriptAction<T1, T2>(
    IJavaScriptBidirectionalInvoker invoker,
    JavaScriptFunctionReference function)
    : JavaScriptActionBase(invoker, function)
{
    public ValueTask InvokeAsync(
        T1 argument1,
        T2 argument2,
        CancellationToken cancellationToken = default)
        => InvokeCoreAsync(
            cancellationToken,
            JavaScriptArgument.From(argument1),
            JavaScriptArgument.From(argument2));
}

public sealed class JavaScriptAction<T1, T2, T3>(
    IJavaScriptBidirectionalInvoker invoker,
    JavaScriptFunctionReference function)
    : JavaScriptActionBase(invoker, function)
{
    public ValueTask InvokeAsync(
        T1 argument1,
        T2 argument2,
        T3 argument3,
        CancellationToken cancellationToken = default)
        => InvokeCoreAsync(
            cancellationToken,
            JavaScriptArgument.From(argument1),
            JavaScriptArgument.From(argument2),
            JavaScriptArgument.From(argument3));
}

public sealed class JavaScriptAction<T1, T2, T3, T4>(
    IJavaScriptBidirectionalInvoker invoker,
    JavaScriptFunctionReference function)
    : JavaScriptActionBase(invoker, function)
{
    public ValueTask InvokeAsync(
        T1 argument1,
        T2 argument2,
        T3 argument3,
        T4 argument4,
        CancellationToken cancellationToken = default)
        => InvokeCoreAsync(
            cancellationToken,
            JavaScriptArgument.From(argument1),
            JavaScriptArgument.From(argument2),
            JavaScriptArgument.From(argument3),
            JavaScriptArgument.From(argument4));
}

/// <summary>
/// Strongly typed JavaScript callback wrapper for signatures with more than
/// four parameters. <typeparamref name="TArguments"/> is the generated C#
/// value-tuple containing every callback argument type.
/// </summary>
public sealed class JavaScriptTupleAction<TArguments>(
    IJavaScriptBidirectionalInvoker invoker,
    JavaScriptFunctionReference function)
    : JavaScriptActionBase(invoker, function)
    where TArguments : struct, ITuple
{
    public ValueTask InvokeAsync(
        TArguments arguments,
        CancellationToken cancellationToken = default)
    {
        var tuple = (ITuple)arguments;
        var values = new JavaScriptArgument[tuple.Length];
        for (var index = 0; index < tuple.Length; index++)
        {
            values[index] = JavaScriptArgument.FromObject(tuple[index]);
        }
        return InvokeCoreAsync(cancellationToken, values);
    }
}
