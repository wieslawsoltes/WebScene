using System.Text.Json;
using System.Text.RegularExpressions;

namespace WebScene.JavaScript.Interop;

/// <summary>
/// Handle-table invoker built for the native engine's asynchronous
/// <c>webscene_engine_evaluate_json</c> boundary.
/// </summary>
public sealed partial class NativeJavaScriptInvoker : IJavaScriptBidirectionalInvoker
{
    private const string Bootstrap = """
        (() => {
          if (globalThis.__webSceneDotNetInterop) return true;
          let nextHandle = 1;
          let nextPromise = 1;
          let nextCallbackCall = 1;
          const values = new Map();
          const promises = new Map();
          const callbackCalls = [];
          const callbackPromises = new Map();
          const keep = value => {
            if ((typeof value !== "object" && typeof value !== "function") || value === null) {
              throw new TypeError("WebScene interop expected a JavaScript object result.");
            }
            const handle = nextHandle++;
            values.set(handle, value);
            return handle;
          };
          const get = handle => {
            const value = values.get(handle);
            if (value === undefined) throw new Error(`Unknown WebScene interop handle ${handle}.`);
            return value;
          };
          const decodeArgument = value => {
            if (!value || typeof value !== "object") return value;
            if (value.__webSceneUndefined === true) return undefined;
            if (Number.isInteger(value.__webSceneHandle)) return get(value.__webSceneHandle);
            if (Array.isArray(value)) return value.map(decodeArgument);
            const result = {};
            for (const [key, item] of Object.entries(value)) {
              result[key] = decodeArgument(item);
            }
            return result;
          };
          const args = json => JSON.parse(json).flatMap(value =>
            value && typeof value === "object" && Array.isArray(value.__webSceneRest)
              ? value.__webSceneRest.map(decodeArgument)
              : [decodeArgument(value)]);
          const globalMember = path => {
            const parts = path.split(".");
            const name = parts.pop();
            const receiver = parts.reduce((value, key) => value[key], globalThis);
            const fn = receiver[name];
            if (typeof fn !== "function") throw new TypeError(`${path} is not a function.`);
            return [receiver, fn];
          };
          const globalValue = path =>
            path.split(".").reduce((value, key) => value[key], globalThis);
          const member = (handle, name) => {
            const receiver = get(handle);
            const fn = receiver[name];
            if (typeof fn !== "function") throw new TypeError(`${name} is not a function.`);
            return [receiver, fn];
          };
          const isRetainedObject = value => {
            if (typeof value === "function") return true;
            if (typeof value !== "object" || value === null
                || Array.isArray(value) || value instanceof Date) {
              return false;
            }
            const prototype = Object.getPrototypeOf(value);
            return prototype !== Object.prototype && prototype !== null;
          };
          const encodeCallbackValue = (value, seen = new WeakSet()) => {
            if (value === null || typeof value === "string"
                || typeof value === "number" || typeof value === "boolean") {
              return value;
            }
            if (value === undefined) return { __webSceneUndefined: true };
            if (typeof value === "function") {
              return { __webSceneHandle: keep(value), __webSceneKind: "function" };
            }
            if (typeof value !== "object") return String(value);
            if (value instanceof Date) return value.toISOString();
            if (seen.has(value)) {
              return { __webSceneHandle: keep(value), __webSceneKind: "object" };
            }
            seen.add(value);
            if (Array.isArray(value)) {
              return value.map(item => encodeCallbackValue(item, seen));
            }
            const prototype = Object.getPrototypeOf(value);
            if (prototype === Object.prototype || prototype === null) {
              const result = {};
              for (const key of Object.keys(value)) {
                result[key] = encodeCallbackValue(value[key], seen);
              }
              return result;
            }
            return { __webSceneHandle: keep(value), __webSceneKind: "object" };
          };
          const trackPromise = value => {
            const promise = nextPromise++;
            promises.set(promise, { status: "pending" });
            Promise.resolve(value).then(
              result => {
                if (isRetainedObject(result)) {
                  promises.set(promise, {
                    status: "fulfilled",
                    objectHandle: keep(result)
                  });
                } else {
                  promises.set(promise, {
                    status: "fulfilled",
                    value: encodeCallbackValue(result)
                  });
                }
              },
              error => promises.set(promise, {
                status: "rejected",
                error: String(error?.stack ?? error)
              }));
            return promise;
          };
          globalThis.__webSceneDotNetInterop = Object.freeze({
            getGlobalObject(path) {
              return keep(globalValue(path));
            },
            getGlobalValue(path) {
              return encodeCallbackValue(globalValue(path));
            },
            getGlobalPromise(path) {
              return trackPromise(globalValue(path));
            },
            invokeGlobalObject(path, json) {
              const [receiver, fn] = globalMember(path);
              return keep(Reflect.apply(fn, receiver, args(json)));
            },
            invokeGlobalValue(path, json) {
              const [receiver, fn] = globalMember(path);
              const value = Reflect.apply(fn, receiver, args(json));
              if (value && typeof value.then === "function") {
                throw new TypeError("Promise function used through the value invocation path.");
              }
              return encodeCallbackValue(value);
            },
            invokeGlobalVoid(path, json) {
              const [receiver, fn] = globalMember(path);
              Reflect.apply(fn, receiver, args(json));
              return true;
            },
            invokeGlobalPromise(path, json) {
              const [receiver, fn] = globalMember(path);
              const promise = nextPromise++;
              promises.set(promise, { status: "pending" });
              Promise.resolve(Reflect.apply(fn, receiver, args(json))).then(
                value => {
                  if (isRetainedObject(value)) {
                    promises.set(promise, {
                      status: "fulfilled",
                      objectHandle: keep(value)
                    });
                  } else {
                    promises.set(promise, {
                      status: "fulfilled",
                      value: encodeCallbackValue(value)
                    });
                  }
                },
                error => promises.set(promise, {
                  status: "rejected",
                  error: String(error?.stack ?? error)
                }));
              return promise;
            },
            construct(path, json) {
              const ctor = path.split(".").reduce((value, key) => value[key], globalThis);
              if (typeof ctor !== "function") throw new TypeError(`${path} is not a constructor.`);
              return keep(Reflect.construct(ctor, args(json)));
            },
            invokeObject(handle, name, json) {
              const [receiver, fn] = member(handle, name);
              return keep(Reflect.apply(fn, receiver, args(json)));
            },
            getObjectProperty(handle, name) {
              return keep(get(handle)[name]);
            },
            getProperty(handle, name) {
              const value = get(handle)[name];
              return encodeCallbackValue(value);
            },
            getPromiseProperty(handle, name) {
              return trackPromise(get(handle)[name]);
            },
            setProperty(handle, name, valueJson) {
              const valuesToSet = args(`[${valueJson}]`);
              get(handle)[name] = valuesToSet[0];
              return true;
            },
            invokeValue(handle, name, json) {
              const [receiver, fn] = member(handle, name);
              const value = Reflect.apply(fn, receiver, args(json));
              if (value && typeof value.then === "function") {
                throw new TypeError("Promise method used through the value invocation path.");
              }
              return encodeCallbackValue(value);
            },
            invokeVoid(handle, name, json) {
              const [receiver, fn] = member(handle, name);
              Reflect.apply(fn, receiver, args(json));
              return true;
            },
            invokePromise(handle, name, json) {
              const [receiver, fn] = member(handle, name);
              const promise = nextPromise++;
              promises.set(promise, { status: "pending" });
              Promise.resolve(Reflect.apply(fn, receiver, args(json))).then(
                value => {
                  if (isRetainedObject(value)) {
                    promises.set(promise, {
                      status: "fulfilled",
                      objectHandle: keep(value)
                    });
                  } else {
                    promises.set(promise, {
                      status: "fulfilled",
                      value: encodeCallbackValue(value)
                    });
                  }
                },
                error => promises.set(promise, {
                  status: "rejected",
                  error: String(error?.stack ?? error)
                }));
              return promise;
            },
            takePromise(promise) {
              const state = promises.get(promise);
              if (!state) throw new Error(`Unknown WebScene promise ${promise}.`);
              if (state.status !== "pending") promises.delete(promise);
              return state;
            },
            createCallbackTarget(target, methodsJson) {
              const methods = JSON.parse(methodsJson);
              const proxy = {};
              for (const method of methods) {
                Object.defineProperty(proxy, method.name, {
                  enumerable: true,
                  configurable: false,
                  value(...actualArguments) {
                    if (method.returnKind === "Synchronous"
                        && typeof method.synchronousResult === "string") {
                      return decodeArgument(JSON.parse(method.synchronousResult));
                    }
                    const call = nextCallbackCall++;
                    const request = {
                      call,
                      target,
                      method: method.name,
                      arguments: encodeCallbackValue(actualArguments),
                      returnKind: method.returnKind
                    };
                    if (method.returnKind === "Promise") {
                      const promise = new Promise((resolve, reject) => {
                        callbackPromises.set(call, { resolve, reject });
                      });
                      callbackCalls.push(request);
                      return promise;
                    }
                    callbackCalls.push(request);
                    if (method.returnKind === "Synchronous") {
                      throw new Error(
                        `Synchronous reverse interop for ${method.name} requires a native host callback.`);
                    }
                    return undefined;
                  }
                });
              }
              return keep(proxy);
            },
            createCallbackFunction(target, returnKind) {
              return keep(function (...actualArguments) {
                const call = nextCallbackCall++;
                const request = {
                  call,
                  target,
                  method: "invoke",
                  arguments: encodeCallbackValue(actualArguments),
                  returnKind
                };
                if (returnKind === "Promise") {
                  const promise = new Promise((resolve, reject) => {
                    callbackPromises.set(call, { resolve, reject });
                  });
                  callbackCalls.push(request);
                  return promise;
                }
                callbackCalls.push(request);
                if (returnKind === "Synchronous") {
                  throw new Error(
                    "Synchronous reverse function interop requires a native host callback.");
                }
                return undefined;
              });
            },
            createSynchronousFactory(target, resultHandle) {
              return keep(function (...actualArguments) {
                callbackCalls.push({
                  call: nextCallbackCall++,
                  target,
                  method: "invoke",
                  arguments: encodeCallbackValue(actualArguments),
                  returnKind: "Void"
                });
                return get(resultHandle);
              });
            },
            takeCallback() {
              return callbackCalls.shift() ?? null;
            },
            completeCallback(call, succeeded, resultJson) {
              const pending = callbackPromises.get(call);
              if (!pending) return true;
              callbackPromises.delete(call);
              if (succeeded) pending.resolve(decodeArgument(JSON.parse(resultJson)));
              else pending.reject(new Error(JSON.parse(resultJson)));
              return true;
            },
            invokeFunction(handle, json) {
              const fn = get(handle);
              if (typeof fn !== "function") throw new TypeError(`${handle} is not a function.`);
              const value = Reflect.apply(fn, undefined, args(json));
              return value === undefined ? null : value;
            },
            invokeFunctionVoid(handle, json) {
              const fn = get(handle);
              if (typeof fn !== "function") throw new TypeError(`${handle} is not a function.`);
              Reflect.apply(fn, undefined, args(json));
              return true;
            },
            release(handle) {
              values.delete(handle);
              return true;
            }
          });
          return true;
        })()
        """;

    private readonly Func<string, string, CancellationToken, Task<string>> _evaluateJsonAsync;
    private readonly JsonSerializerOptions _jsonOptions;
    private readonly SemaphoreSlim _initializationGate = new(1, 1);
    private readonly TimeSpan _promisePollInterval;
    private readonly TimeSpan _promiseTimeout;
    private readonly Dictionary<long, IJavaScriptCallbackTarget> _callbackTargets = [];
    private readonly Dictionary<long, long> _callbackTargetHandles = [];
    private long _nextCallbackTarget;
    private volatile bool _initialized;

    public NativeJavaScriptInvoker(
        Func<string, string, CancellationToken, Task<string>> evaluateJsonAsync,
        JsonSerializerOptions? jsonOptions = null,
        TimeSpan? promisePollInterval = null,
        TimeSpan? promiseTimeout = null)
    {
        _evaluateJsonAsync = evaluateJsonAsync
            ?? throw new ArgumentNullException(nameof(evaluateJsonAsync));
        _jsonOptions = CreateJsonOptions(jsonOptions);
        _promisePollInterval = promisePollInterval ?? TimeSpan.FromMilliseconds(8);
        _promiseTimeout = promiseTimeout ?? TimeSpan.FromSeconds(30);
    }

    private static JsonSerializerOptions CreateJsonOptions(
        JsonSerializerOptions? options)
    {
        if (options is null)
        {
            return JavaScriptInteropJson.Options;
        }
        var result = new JsonSerializerOptions(options);
        if (!result.Converters.Any(
                static converter =>
                    converter is JavaScriptValueTupleJsonConverterFactory))
        {
            result.Converters.Add(new JavaScriptValueTupleJsonConverterFactory());
        }
        return result;
    }

    public async ValueTask<JavaScriptObjectReference> GetGlobalObjectAsync(
        string globalName,
        CancellationToken cancellationToken = default)
    {
        ValidateGlobalName(globalName);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.getGlobalObject({JsonSerializer.Serialize(globalName)})",
            "webscene-interop-get-global-object.js",
            cancellationToken).ConfigureAwait(false);
        return new JavaScriptObjectReference(DeserializeRequired<long>(json));
    }

    public async ValueTask<T?> GetGlobalAsync<T>(
        string globalName,
        CancellationToken cancellationToken = default)
    {
        ValidateGlobalName(globalName);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.getGlobalValue({JsonSerializer.Serialize(globalName)})",
            "webscene-interop-get-global-value.js",
            cancellationToken).ConfigureAwait(false);
        return Deserialize<T>(json);
    }

    public async ValueTask<T?> GetGlobalPromiseAsync<T>(
        string globalName,
        CancellationToken cancellationToken = default)
    {
        ValidateGlobalName(globalName);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var operationJson = await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.getGlobalPromise({JsonSerializer.Serialize(globalName)})",
            "webscene-interop-get-global-promise.js",
            cancellationToken).ConfigureAwait(false);
        var operation = DeserializeRequired<long>(operationJson);
        var state = await WaitForPromiseAsync(operation, cancellationToken)
            .ConfigureAwait(false);
        if (state.ObjectHandle is not null)
        {
            if (typeof(T) == typeof(JavaScriptObjectReference)
                || Nullable.GetUnderlyingType(typeof(T))
                    == typeof(JavaScriptObjectReference))
            {
                object reference = new JavaScriptObjectReference(
                    state.ObjectHandle.Value);
                return (T)reference;
            }
            throw new InvalidOperationException(
                $"JavaScript promise {operation} resolved to an object; use the object invocation path.");
        }
        return state.Value.Deserialize<T>(_jsonOptions);
    }

    public async ValueTask<JavaScriptObjectReference> GetGlobalPromiseObjectAsync(
        string globalName,
        CancellationToken cancellationToken = default)
    {
        ValidateGlobalName(globalName);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var operationJson = await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.getGlobalPromise({JsonSerializer.Serialize(globalName)})",
            "webscene-interop-get-global-promise.js",
            cancellationToken).ConfigureAwait(false);
        var operation = DeserializeRequired<long>(operationJson);
        var state = await WaitForPromiseAsync(operation, cancellationToken)
            .ConfigureAwait(false);
        if (state.ObjectHandle is null or <= 0)
        {
            throw new InvalidOperationException(
                $"JavaScript promise {operation} did not resolve to an object.");
        }
        return new JavaScriptObjectReference(state.ObjectHandle.Value);
    }

    public async ValueTask<JavaScriptObjectReference> InvokeGlobalObjectAsync(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateGlobalName(globalName);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            GlobalInvocation("invokeGlobalObject", globalName, arguments),
            "webscene-interop-global-object.js",
            cancellationToken).ConfigureAwait(false);
        return new JavaScriptObjectReference(DeserializeRequired<long>(json));
    }

    public async ValueTask<T?> InvokeGlobalAsync<T>(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateGlobalName(globalName);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            GlobalInvocation("invokeGlobalValue", globalName, arguments),
            "webscene-interop-global-value.js",
            cancellationToken).ConfigureAwait(false);
        return Deserialize<T>(json);
    }

    public async ValueTask<T?> InvokeGlobalPromiseAsync<T>(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateGlobalName(globalName);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var operationJson = await EvaluateAsync(
            GlobalInvocation("invokeGlobalPromise", globalName, arguments),
            "webscene-interop-global-promise.js",
            cancellationToken).ConfigureAwait(false);
        var operation = DeserializeRequired<long>(operationJson);
        var state = await WaitForPromiseAsync(operation, cancellationToken)
            .ConfigureAwait(false);
        if (state.ObjectHandle is not null)
        {
            if (typeof(T) == typeof(JavaScriptObjectReference)
                || Nullable.GetUnderlyingType(typeof(T))
                    == typeof(JavaScriptObjectReference))
            {
                object reference = new JavaScriptObjectReference(
                    state.ObjectHandle.Value);
                return (T)reference;
            }
            throw new InvalidOperationException(
                $"JavaScript promise {operation} resolved to an object; use the object invocation path.");
        }
        return state.Value.Deserialize<T>(_jsonOptions);
    }

    public async ValueTask<JavaScriptObjectReference> InvokeGlobalPromiseObjectAsync(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateGlobalName(globalName);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var operationJson = await EvaluateAsync(
            GlobalInvocation("invokeGlobalPromise", globalName, arguments),
            "webscene-interop-global-promise.js",
            cancellationToken).ConfigureAwait(false);
        var operation = DeserializeRequired<long>(operationJson);
        var state = await WaitForPromiseAsync(operation, cancellationToken)
            .ConfigureAwait(false);
        if (state.ObjectHandle is null or <= 0)
        {
            throw new InvalidOperationException(
                $"JavaScript promise {operation} did not resolve to an object.");
        }
        return new JavaScriptObjectReference(state.ObjectHandle.Value);
    }

    public async ValueTask InvokeGlobalVoidAsync(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateGlobalName(globalName);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        await EvaluateAsync(
            GlobalInvocation("invokeGlobalVoid", globalName, arguments),
            "webscene-interop-global-void.js",
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<JavaScriptObjectReference> ConstructAsync(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(globalName);
        if (!GlobalNamePattern().IsMatch(globalName))
        {
            throw new ArgumentException(
                "The constructor must be a dotted JavaScript identifier.",
                nameof(globalName));
        }

        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.construct({JsonSerializer.Serialize(globalName)}, {SerializeArguments(arguments)})",
            "webscene-interop-construct.js",
            cancellationToken).ConfigureAwait(false);
        return new JavaScriptObjectReference(DeserializeRequired<long>(json));
    }

    public async ValueTask<JavaScriptObjectReference> InvokeObjectAsync(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateInvocation(target, method);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            Invocation("invokeObject", target, method, arguments),
            "webscene-interop-object.js",
            cancellationToken).ConfigureAwait(false);
        return new JavaScriptObjectReference(DeserializeRequired<long>(json));
    }

    public async ValueTask<JavaScriptObjectReference> GetObjectPropertyAsync(
        JavaScriptObjectReference target,
        string property,
        CancellationToken cancellationToken = default)
    {
        ValidateInvocation(target, property);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.getObjectProperty({target.Id}, {JsonSerializer.Serialize(property)})",
            "webscene-interop-get-object-property.js",
            cancellationToken).ConfigureAwait(false);
        return new JavaScriptObjectReference(DeserializeRequired<long>(json));
    }

    public async ValueTask<T?> GetPropertyAsync<T>(
        JavaScriptObjectReference target,
        string property,
        CancellationToken cancellationToken = default)
    {
        ValidateInvocation(target, property);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.getProperty({target.Id}, {JsonSerializer.Serialize(property)})",
            "webscene-interop-get-property.js",
            cancellationToken).ConfigureAwait(false);
        return Deserialize<T>(json);
    }

    public async ValueTask<T?> GetPromisePropertyAsync<T>(
        JavaScriptObjectReference target,
        string property,
        CancellationToken cancellationToken = default)
    {
        ValidateInvocation(target, property);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var operationJson = await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.getPromiseProperty({target.Id}, {JsonSerializer.Serialize(property)})",
            "webscene-interop-get-promise-property.js",
            cancellationToken).ConfigureAwait(false);
        var operation = DeserializeRequired<long>(operationJson);
        var state = await WaitForPromiseAsync(operation, cancellationToken)
            .ConfigureAwait(false);
        if (state.ObjectHandle is not null)
        {
            if (typeof(T) == typeof(JavaScriptObjectReference)
                || Nullable.GetUnderlyingType(typeof(T))
                    == typeof(JavaScriptObjectReference))
            {
                object reference = new JavaScriptObjectReference(
                    state.ObjectHandle.Value);
                return (T)reference;
            }
            throw new InvalidOperationException(
                $"JavaScript promise {operation} resolved to an object; use the object invocation path.");
        }
        return state.Value.Deserialize<T>(_jsonOptions);
    }

    public async ValueTask<JavaScriptObjectReference> GetPromiseObjectPropertyAsync(
        JavaScriptObjectReference target,
        string property,
        CancellationToken cancellationToken = default)
    {
        ValidateInvocation(target, property);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var operationJson = await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.getPromiseProperty({target.Id}, {JsonSerializer.Serialize(property)})",
            "webscene-interop-get-promise-property.js",
            cancellationToken).ConfigureAwait(false);
        var operation = DeserializeRequired<long>(operationJson);
        var state = await WaitForPromiseAsync(operation, cancellationToken)
            .ConfigureAwait(false);
        if (state.ObjectHandle is null or <= 0)
        {
            throw new InvalidOperationException(
                $"JavaScript promise {operation} did not resolve to an object.");
        }
        return new JavaScriptObjectReference(state.ObjectHandle.Value);
    }

    public async ValueTask SetPropertyAsync(
        JavaScriptObjectReference target,
        string property,
        JavaScriptArgument value,
        CancellationToken cancellationToken = default)
    {
        ValidateInvocation(target, property);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.setProperty({target.Id}, {JsonSerializer.Serialize(property)}, {JsonSerializer.Serialize(value.Json)})",
            "webscene-interop-set-property.js",
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<T?> InvokeAsync<T>(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateInvocation(target, method);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            Invocation("invokeValue", target, method, arguments),
            "webscene-interop-value.js",
            cancellationToken).ConfigureAwait(false);
        return Deserialize<T>(json);
    }

    public async ValueTask<T?> InvokePromiseAsync<T>(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateInvocation(target, method);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var operationJson = await EvaluateAsync(
            Invocation("invokePromise", target, method, arguments),
            "webscene-interop-promise.js",
            cancellationToken).ConfigureAwait(false);
        var operation = DeserializeRequired<long>(operationJson);
        var state = await WaitForPromiseAsync(operation, cancellationToken)
            .ConfigureAwait(false);
        if (state.ObjectHandle is not null)
        {
            if (typeof(T) == typeof(JavaScriptObjectReference)
                || Nullable.GetUnderlyingType(typeof(T))
                    == typeof(JavaScriptObjectReference))
            {
                object reference = new JavaScriptObjectReference(
                    state.ObjectHandle.Value);
                return (T)reference;
            }
            throw new InvalidOperationException(
                $"JavaScript promise {operation} resolved to an object; use the object invocation path.");
        }
        return state.Value.Deserialize<T>(_jsonOptions);
    }

    public async ValueTask<JavaScriptObjectReference> InvokePromiseObjectAsync(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateInvocation(target, method);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var operationJson = await EvaluateAsync(
            Invocation("invokePromise", target, method, arguments),
            "webscene-interop-promise.js",
            cancellationToken).ConfigureAwait(false);
        var operation = DeserializeRequired<long>(operationJson);
        var state = await WaitForPromiseAsync(operation, cancellationToken)
            .ConfigureAwait(false);
        if (state.ObjectHandle is null or <= 0)
        {
            throw new InvalidOperationException(
                $"JavaScript promise {operation} did not resolve to an object.");
        }
        return new JavaScriptObjectReference(state.ObjectHandle.Value);
    }

    public async ValueTask InvokeVoidAsync(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateInvocation(target, method);
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        await EvaluateAsync(
            Invocation("invokeVoid", target, method, arguments),
            "webscene-interop-void.js",
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask ReleaseAsync(
        JavaScriptObjectReference reference,
        CancellationToken cancellationToken = default)
    {
        if (reference.IsEmpty)
        {
            return;
        }
        lock (_callbackTargets)
        {
            if (_callbackTargetHandles.Remove(reference.Id, out var targetId))
            {
                _callbackTargets.Remove(targetId);
            }
        }

        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.release({reference.Id})",
            "webscene-interop-release.js",
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<JavaScriptObjectReference> RegisterCallbackTargetAsync(
        IJavaScriptCallbackTarget target,
        IReadOnlyList<JavaScriptCallbackMethod> methods,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(methods);
        var targetId = Interlocked.Increment(ref _nextCallbackTarget);
        lock (_callbackTargets)
        {
            _callbackTargets.Add(targetId, target);
        }
        try
        {
            await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
            var methodsJson = JsonSerializer.Serialize(
                methods.Select(static method => new
                {
                    name = method.Name,
                    returnKind = method.ReturnKind.ToString(),
                    synchronousResult = method.SynchronousResult.Value
                }),
                _jsonOptions);
            var result = await EvaluateAsync(
                $"globalThis.__webSceneDotNetInterop.createCallbackTarget({targetId}, {JsonSerializer.Serialize(methodsJson)})",
                "webscene-interop-register-callback.js",
                cancellationToken).ConfigureAwait(false);
            var reference = new JavaScriptObjectReference(DeserializeRequired<long>(result));
            lock (_callbackTargets)
            {
                _callbackTargetHandles[reference.Id] = targetId;
            }
            return reference;
        }
        catch
        {
            lock (_callbackTargets)
            {
                _callbackTargets.Remove(targetId);
            }
            throw;
        }
    }

    public async ValueTask<bool> PumpCallbackAsync(
        CancellationToken cancellationToken = default)
    {
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            "globalThis.__webSceneDotNetInterop.takeCallback()",
            "webscene-interop-take-callback.js",
            cancellationToken).ConfigureAwait(false);
        var request = Deserialize<CallbackRequest>(json);
        if (request is null)
        {
            return false;
        }
        IJavaScriptCallbackTarget target;
        lock (_callbackTargets)
        {
            if (!_callbackTargets.TryGetValue(request.Target, out target!))
            {
                throw new InvalidOperationException(
                    $"Unknown .NET JavaScript callback target {request.Target}.");
            }
        }
        try
        {
            var result = await target.DispatchAsync(
                request.Method,
                request.Arguments,
                cancellationToken).ConfigureAwait(false);
            await CompleteCallbackAsync(
                request.Call,
                succeeded: true,
                JsonSerializer.Serialize(result, _jsonOptions),
                cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            await CompleteCallbackAsync(
                request.Call,
                succeeded: false,
                JsonSerializer.Serialize(exception.Message),
                cancellationToken).ConfigureAwait(false);
            throw;
        }
        return true;
    }

    public async ValueTask<JavaScriptFunctionReference> RegisterFunctionAsync(
        JavaScriptCallbackHandler callback,
        JavaScriptCallbackReturnKind returnKind = JavaScriptCallbackReturnKind.Void,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(callback);
        var targetId = Interlocked.Increment(ref _nextCallbackTarget);
        lock (_callbackTargets)
        {
            _callbackTargets.Add(targetId, new DelegateCallbackTarget(callback));
        }
        try
        {
            await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
            var json = await EvaluateAsync(
                $"globalThis.__webSceneDotNetInterop.createCallbackFunction({targetId}, {JsonSerializer.Serialize(returnKind.ToString())})",
                "webscene-interop-register-function.js",
                cancellationToken).ConfigureAwait(false);
            var reference = new JavaScriptObjectReference(DeserializeRequired<long>(json));
            lock (_callbackTargets)
            {
                _callbackTargetHandles[reference.Id] = targetId;
            }
            return new JavaScriptFunctionReference(
                this,
                reference);
        }
        catch
        {
            lock (_callbackTargets)
            {
                _callbackTargets.Remove(targetId);
            }
            throw;
        }
    }

    public async ValueTask<JavaScriptFunctionReference> RegisterSynchronousFactoryAsync(
        JavaScriptObjectReference result,
        JavaScriptCallbackHandler callback,
        CancellationToken cancellationToken = default)
    {
        ValidateReference(result, nameof(result));
        ArgumentNullException.ThrowIfNull(callback);
        var targetId = Interlocked.Increment(ref _nextCallbackTarget);
        lock (_callbackTargets)
        {
            _callbackTargets.Add(targetId, new DelegateCallbackTarget(callback));
        }
        try
        {
            await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
            var json = await EvaluateAsync(
                $"globalThis.__webSceneDotNetInterop.createSynchronousFactory({targetId}, {result.Id})",
                "webscene-interop-register-synchronous-factory.js",
                cancellationToken).ConfigureAwait(false);
            var reference = new JavaScriptObjectReference(DeserializeRequired<long>(json));
            lock (_callbackTargets)
            {
                _callbackTargetHandles[reference.Id] = targetId;
            }
            return new JavaScriptFunctionReference(this, reference);
        }
        catch
        {
            lock (_callbackTargets)
            {
                _callbackTargets.Remove(targetId);
            }
            throw;
        }
    }

    public async ValueTask<T?> InvokeFunctionAsync<T>(
        JavaScriptObjectReference function,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateReference(function, nameof(function));
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        var json = await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.invokeFunction({function.Id}, {SerializeArguments(arguments)})",
            "webscene-interop-invoke-function.js",
            cancellationToken).ConfigureAwait(false);
        return Deserialize<T>(json);
    }

    public async ValueTask InvokeFunctionVoidAsync(
        JavaScriptObjectReference function,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
    {
        ValidateReference(function, nameof(function));
        await EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.invokeFunctionVoid({function.Id}, {SerializeArguments(arguments)})",
            "webscene-interop-invoke-function-void.js",
            cancellationToken).ConfigureAwait(false);
    }

    private Task<string> EvaluateAsync(
        string code,
        string documentName,
        CancellationToken cancellationToken)
        => _evaluateJsonAsync(code, documentName, cancellationToken);

    private async ValueTask EnsureInitializedAsync(CancellationToken cancellationToken)
    {
        if (_initialized)
        {
            return;
        }

        await _initializationGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_initialized)
            {
                return;
            }
            await EvaluateAsync(
                Bootstrap,
                "webscene-native-dotnet-interop.js",
                cancellationToken).ConfigureAwait(false);
            _initialized = true;
        }
        finally
        {
            _initializationGate.Release();
        }
    }

    private T? Deserialize<T>(string json)
        => JsonSerializer.Deserialize<T>(json, _jsonOptions);

    private T DeserializeRequired<T>(string json)
        => Deserialize<T>(json)
           ?? throw new InvalidOperationException(
               $"The native WebScene engine returned null for {typeof(T).Name}.");

    private async ValueTask<PromiseState> WaitForPromiseAsync(
        long operation,
        CancellationToken cancellationToken)
    {
        using var timeout = new CancellationTokenSource(_promiseTimeout);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            timeout.Token);
        try
        {
            while (true)
            {
                var stateJson = await EvaluateAsync(
                    $"globalThis.__webSceneDotNetInterop.takePromise({operation})",
                    "webscene-interop-promise-result.js",
                    linked.Token).ConfigureAwait(false);
                var state = DeserializeRequired<PromiseState>(stateJson);
                if (state.Status == "fulfilled")
                {
                    return state;
                }
                if (state.Status == "rejected")
                {
                    throw new InvalidOperationException(
                        $"JavaScript promise rejected: {state.Error}");
                }
                await Task.Delay(_promisePollInterval, linked.Token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (
            timeout.IsCancellationRequested && !cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"JavaScript promise {operation} did not settle within {_promiseTimeout}.");
        }
    }

    private static void ValidateInvocation(
        JavaScriptObjectReference target,
        string method)
    {
        if (target.IsEmpty)
        {
            throw new ArgumentException(
                "A JavaScript object handle is required.",
                nameof(target));
        }
        ArgumentException.ThrowIfNullOrWhiteSpace(method);
    }

    private static void ValidateGlobalName(string globalName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(globalName);
        if (!GlobalNamePattern().IsMatch(globalName))
        {
            throw new ArgumentException(
                "The function must be a dotted JavaScript identifier.",
                nameof(globalName));
        }
    }

    private static void ValidateReference(
        JavaScriptObjectReference reference,
        string parameter)
    {
        if (reference.IsEmpty)
        {
            throw new ArgumentException(
                "A JavaScript object handle is required.",
                parameter);
        }
    }

    private async ValueTask CompleteCallbackAsync(
        long call,
        bool succeeded,
        string json,
        CancellationToken cancellationToken)
        => await EvaluateAsync(
            $"globalThis.__webSceneDotNetInterop.completeCallback({call}, {(succeeded ? "true" : "false")}, {JsonSerializer.Serialize(json)})",
            "webscene-interop-complete-callback.js",
            cancellationToken).ConfigureAwait(false);

    private static string Invocation(
        string operation,
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments)
        => $"globalThis.__webSceneDotNetInterop.{operation}({target.Id}, {JsonSerializer.Serialize(method)}, {SerializeArguments(arguments)})";

    private static string GlobalInvocation(
        string operation,
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments)
        => $"globalThis.__webSceneDotNetInterop.{operation}({JsonSerializer.Serialize(globalName)}, {SerializeArguments(arguments)})";

    private static string SerializeArguments(IReadOnlyList<JavaScriptArgument> arguments)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        return JsonSerializer.Serialize(
            $"[{string.Join(",", arguments.Select(static argument => argument.Json))}]");
    }

    [GeneratedRegex(
        @"^[$A-Z_a-z][$\w]*(?:\.[$A-Z_a-z][$\w]*)*$",
        RegexOptions.CultureInvariant)]
    private static partial Regex GlobalNamePattern();

    private sealed record PromiseState(
        string Status,
        JsonElement Value,
        string? Error,
        long? ObjectHandle = null);

    private sealed record CallbackRequest(
        long Call,
        long Target,
        string Method,
        JsonElement Arguments);

    private sealed class DelegateCallbackTarget(JavaScriptCallbackHandler callback)
        : IJavaScriptCallbackTarget
    {
        public ValueTask<object?> DispatchAsync(
            string method,
            JsonElement arguments,
            CancellationToken cancellationToken = default)
        {
            if (!string.Equals(method, "invoke", StringComparison.Ordinal))
            {
                throw new MissingMethodException(
                    $"Unknown JavaScript function callback method '{method}'.");
            }
            return callback(arguments, cancellationToken);
        }
    }
}
