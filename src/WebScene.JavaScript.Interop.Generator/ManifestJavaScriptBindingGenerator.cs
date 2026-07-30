using System.Text;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace WebScene.JavaScript.Interop.Generator;

[Generator(LanguageNames.CSharp)]
public sealed class ManifestJavaScriptBindingGenerator : IIncrementalGenerator
{
    private static readonly DiagnosticDescriptor InvalidInput = new(
        "WEBSCENEJS001",
        "Invalid WebScene interop input",
        "{0}",
        "WebScene.Interop",
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor UnsupportedType = new(
        "WEBSCENEJS002",
        "JavaScript type requires a policy mapping",
        "TypeScript type '{0}' has no precise .NET mapping; generated member '{1}' uses JsonElement",
        "WebScene.Interop",
        DiagnosticSeverity.Warning,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor AmbiguousRetainedUnion = new(
        "WEBSCENEJS003",
        "JavaScript retained union requires a discriminator",
        "Retained JavaScript union for generated member '{0}' cannot distinguish multiple branches represented by '{1}'; those branches use the raw wire type until a policy mapping is supplied",
        "WebScene.Interop",
        DiagnosticSeverity.Warning,
        isEnabledByDefault: true);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var apiFiles = context.AdditionalTextsProvider
            .Where(static file => file.Path.EndsWith(
                ".webscene-interop-api.json",
                StringComparison.OrdinalIgnoreCase))
            .Select(static (file, cancellationToken) => new InputFile(
                file.Path,
                file.GetText(cancellationToken)?.ToString()));
        var policyFiles = context.AdditionalTextsProvider
            .Where(static file => file.Path.EndsWith(
                ".webscene-interop-policy.json",
                StringComparison.OrdinalIgnoreCase))
            .Select(static (file, cancellationToken) => new InputFile(
                file.Path,
                file.GetText(cancellationToken)?.ToString()));

        context.RegisterSourceOutput(
            apiFiles.Collect().Combine(policyFiles.Collect()),
            static (productionContext, inputs) =>
                Emit(productionContext, inputs.Left, inputs.Right));
    }

    private static void Emit(
        SourceProductionContext context,
        IReadOnlyList<InputFile> apiFiles,
        IReadOnlyList<InputFile> policyFiles)
    {
        foreach (var policyFile in policyFiles)
        {
            try
            {
                if (string.IsNullOrWhiteSpace(policyFile.Content))
                {
                    throw new InvalidDataException(
                        $"Interop policy '{policyFile.Path}' is empty.");
                }

                using var policyDocument = JsonDocument.Parse(policyFile.Content!);
                var policy = policyDocument.RootElement;
                RequireSchema(policy, policyFile.Path!);
                var apiName = RequiredString(policy, "api", policyFile.Path!);
                var apiFile = apiFiles.FirstOrDefault(candidate =>
                    string.Equals(
                        Path.GetFileName(candidate.Path),
                        apiName,
                        StringComparison.OrdinalIgnoreCase));
                if (apiFile.Path is null || string.IsNullOrWhiteSpace(apiFile.Content))
                {
                    throw new InvalidDataException(
                        $"Policy '{policyFile.Path}' references missing API manifest '{apiName}'.");
                }

                using var apiDocument = JsonDocument.Parse(apiFile.Content!);
                RequireSchema(apiDocument.RootElement, apiFile.Path!);
                EmitPolicy(
                    context,
                    apiDocument.RootElement,
                    policy,
                    policyFile.Path!);
            }
            catch (Exception exception) when (
                exception is JsonException
                    or InvalidDataException
                    or InvalidOperationException)
            {
                context.ReportDiagnostic(Diagnostic.Create(
                    InvalidInput,
                    Location.None,
                    exception.Message));
            }
        }
    }

    private static void EmitPolicy(
        SourceProductionContext context,
        JsonElement api,
        JsonElement policy,
        string policyPath)
    {
        var namespaceName = RequiredString(policy, "namespace", policyPath);
        if (policy.TryGetProperty("apiFingerprint", out var expectedFingerprint)
            && expectedFingerprint.ValueKind == JsonValueKind.String
            && api.TryGetProperty("apiFingerprint", out var actualFingerprint)
            && !string.Equals(
                expectedFingerprint.GetString(),
                actualFingerprint.GetString(),
                StringComparison.Ordinal))
        {
            throw new InvalidDataException(
                $"Policy '{policyPath}' targets API fingerprint '{expectedFingerprint.GetString()}', but the manifest uses '{actualFingerprint.GetString()}'.");
        }
        var roots = api.GetProperty("roots")
            .EnumerateArray()
            .ToDictionary(
                static root => root.GetProperty("qualifiedName").GetString()!,
                static root => root,
                StringComparer.Ordinal);
        var typesElement = api.TryGetProperty("types", out var discoveredTypes)
            ? discoveredTypes
            : api.GetProperty("roots");
        var types = typesElement
            .EnumerateArray()
            .ToDictionary(
                static type => type.GetProperty("qualifiedName").GetString()!,
                static type => type,
                StringComparer.Ordinal);
        var bindings = policy.GetProperty("bindings")
            .EnumerateArray()
            .Where(static binding =>
                !binding.TryGetProperty("include", out var include)
                || include.ValueKind != JsonValueKind.False)
            .Select(binding =>
            {
                var source = RequiredString(binding, "source", policyPath);
                if (!roots.TryGetValue(source, out var root))
                {
                    throw new InvalidDataException(
                        $"Policy binding source '{source}' was not discovered.");
                }
                return new Binding(
                    source,
                    RequiredString(binding, "name", policyPath),
                    binding,
                    root);
            })
            .ToArray();
        var bindingNames = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var binding in bindings)
        {
            bindingNames[binding.Source] = binding.Name;
            bindingNames[LastSegment(binding.Source)] = binding.Name;
            bindingNames[binding.Root.GetProperty("name").GetString()!] = binding.Name;
        }
        var models = policy.TryGetProperty("models", out var modelPolicies)
            ? modelPolicies.EnumerateArray()
                .Where(static model =>
                    !model.TryGetProperty("include", out var include)
                    || include.ValueKind != JsonValueKind.False)
                .Select(model =>
                {
                    var source = RequiredString(model, "source", policyPath);
                    if (!types.TryGetValue(source, out var type))
                    {
                        throw new InvalidDataException(
                            $"Policy model source '{source}' was not discovered.");
                    }
                    return new Model(
                        source,
                        OptionalString(model, "name")
                            ?? SuggestedTypeName(type.GetProperty("name").GetString()!),
                        model,
                        type);
                })
                .ToArray()
            : [];
        var adapters = policy.TryGetProperty("adapters", out var adapterPolicies)
            ? adapterPolicies.EnumerateArray()
                .Where(static adapter =>
                    !adapter.TryGetProperty("include", out var include)
                    || include.ValueKind != JsonValueKind.False)
                .Select(adapter =>
                {
                    var source = RequiredString(adapter, "source", policyPath);
                    if (!types.TryGetValue(source, out var type))
                    {
                        throw new InvalidDataException(
                            $"Policy adapter source '{source}' was not discovered.");
                    }
                    return new Adapter(
                        source,
                        RequiredString(adapter, "name", policyPath),
                        adapter,
                        type);
                })
                .ToArray()
            : [];
        var modelNames = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var model in models)
        {
            modelNames[model.Source] = model.Name;
            modelNames[LastSegment(model.Source)] = model.Name;
            modelNames[model.Type.GetProperty("name").GetString()!] = model.Name;
        }
        var adapterNames = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var adapter in adapters)
        {
            adapterNames[adapter.Source] = adapter.Name;
            adapterNames[LastSegment(adapter.Source)] = adapter.Name;
            adapterNames[adapter.Type.GetProperty("name").GetString()!] = adapter.Name;
        }
        var typeMappings = ReadStringMap(policy, "typeMappings");
        var generation = new GenerationContext(
            context,
            namespaceName,
            bindingNames,
            modelNames,
            adapterNames,
            types,
            typeMappings);
        var availableFunctions = api.TryGetProperty("functions", out var discoveredFunctions)
            ? discoveredFunctions.EnumerateArray().ToArray()
            : [];
        var functions = policy.TryGetProperty("functions", out var functionPolicies)
            ? functionPolicies.EnumerateArray()
                .Where(static method =>
                    !method.TryGetProperty("include", out var include)
                    || include.ValueKind != JsonValueKind.False)
                .Select(methodPolicy =>
                {
                    var source = RequiredString(methodPolicy, "source", policyPath);
                    var overload = methodPolicy.TryGetProperty("overload", out var overloadValue)
                        ? overloadValue.GetInt32()
                        : 0;
                    var method = availableFunctions.FirstOrDefault(candidate =>
                        string.Equals(
                            candidate.GetProperty("qualifiedName").GetString(),
                            source,
                            StringComparison.Ordinal)
                        && candidate.GetProperty("overload").GetInt32() == overload);
                    if (method.ValueKind == JsonValueKind.Undefined)
                    {
                        throw new InvalidDataException(
                            $"Global function '{source}' overload {overload} was not discovered.");
                    }
                    return new GlobalFunction(
                        source,
                        RequiredString(methodPolicy, "globalName", policyPath),
                        OptionalString(methodPolicy, "name")
                            ?? SuggestedAdapterMethodName(
                                method.GetProperty("name").GetString()!,
                                method),
                        methodPolicy,
                        method);
                })
                .ToArray()
            : [];
        var availableGlobals = api.TryGetProperty("globals", out var discoveredGlobals)
            ? discoveredGlobals.EnumerateArray().ToArray()
            : [];
        var globalProperties = policy.TryGetProperty(
                "globalProperties",
                out var globalPropertyPolicies)
            ? globalPropertyPolicies.EnumerateArray()
                .Where(static property =>
                    !property.TryGetProperty("include", out var include)
                    || include.ValueKind != JsonValueKind.False)
                .Select(propertyPolicy =>
                {
                    var source = RequiredString(
                        propertyPolicy,
                        "source",
                        policyPath);
                    var property = availableGlobals.FirstOrDefault(candidate =>
                        string.Equals(
                            candidate.GetProperty("qualifiedName").GetString(),
                            source,
                            StringComparison.Ordinal));
                    if (property.ValueKind == JsonValueKind.Undefined)
                    {
                        throw new InvalidDataException(
                            $"Global property '{source}' was not discovered.");
                    }
                    return new GlobalProperty(
                        source,
                        RequiredString(
                            propertyPolicy,
                            "globalName",
                            policyPath),
                        OptionalString(propertyPolicy, "getterName")
                            ?? "Get"
                            + PascalCase(property.GetProperty("name").GetString()!)
                            + "Async",
                        property);
                })
                .ToArray()
            : [];

        foreach (var model in models)
        {
            var source = GenerateModel(generation, model);
            context.AddSource(
                $"{Sanitize(namespaceName)}_{Sanitize(model.Name)}.WebSceneModel.g.cs",
                SourceText.From(source, Encoding.UTF8));
        }
        foreach (var adapter in adapters)
        {
            var source = GenerateAdapter(generation, adapter, policyPath);
            context.AddSource(
                $"{Sanitize(namespaceName)}_{Sanitize(adapter.Name)}.WebSceneAdapter.g.cs",
                SourceText.From(source, Encoding.UTF8));
        }
        foreach (var binding in bindings)
        {
            var source = GenerateBinding(generation, binding, policyPath);
            context.AddSource(
                $"{Sanitize(namespaceName)}_{Sanitize(binding.Name)}.WebSceneManifestInterop.g.cs",
                SourceText.From(source, Encoding.UTF8));
        }
        if (functions.Length > 0 || globalProperties.Length > 0)
        {
            var functionsClassName = OptionalString(policy, "functionsClassName")
                                     ?? "JavaScriptGlobals";
            var source = GenerateGlobalFunctions(
                generation,
                functionsClassName,
                functions,
                globalProperties);
            context.AddSource(
                $"{Sanitize(namespaceName)}_{Sanitize(functionsClassName)}.WebSceneGlobalFunctions.g.cs",
                SourceText.From(source, Encoding.UTF8));
        }
        for (var index = 0; index < generation.StructuralModels.Count; index++)
        {
            var structural = generation.StructuralModels[index];
            var genericSuffix = structural.TypeParameters.Length == 0
                ? string.Empty
                : "<" + string.Join(
                    ", ",
                    structural.TypeParameters.Select(EscapeIdentifier)) + ">";
            var source = new StringBuilder(
                """
                // <auto-generated />
                #nullable enable

                """);
            source.Append("namespace ").Append(generation.Namespace).AppendLine(";")
                .AppendLine();
            EmitObjectModel(
                source,
                generation,
                structural.Name + genericSuffix,
                structural.Name,
                structural.Type,
                structural.TypeParameters);
            context.AddSource(
                $"{Sanitize(namespaceName)}_{Sanitize(structural.Name)}.WebSceneStructuralModel.g.cs",
                SourceText.From(source.ToString(), Encoding.UTF8));
        }
    }

    private static string GenerateGlobalFunctions(
        GenerationContext generation,
        string className,
        IReadOnlyList<GlobalFunction> functions,
        IReadOnlyList<GlobalProperty> properties)
    {
        var source = new StringBuilder(
            """
            // <auto-generated />
            #nullable enable

            """);
        source.Append("namespace ").Append(generation.Namespace).AppendLine(";")
            .AppendLine()
            .Append("public static class ").Append(className).AppendLine()
            .AppendLine("{");
        foreach (var function in functions)
        {
            EmitGlobalFunction(source, generation, function);
        }
        foreach (var property in properties)
        {
            EmitGlobalProperty(source, generation, property);
        }
        return source.AppendLine("}").ToString();
    }

    private static void EmitGlobalProperty(
        StringBuilder source,
        GenerationContext generation,
        GlobalProperty property)
    {
        var declaredType = property.Property.GetProperty("type");
        var promise = IsPromiseLikeReturn(declaredType);
        var effectiveType = Kind(declaredType) == "promise"
            ? declaredType.GetProperty("result")
            : declaredType;
        var mapping = MapType(
            generation,
            effectiveType,
            optional: false,
            property.Name);
        var valueInvocation = promise
            ? "GetGlobalPromiseAsync"
            : "GetGlobalAsync";
        var objectInvocation = promise
            ? "GetGlobalPromiseObjectAsync"
            : "GetGlobalObjectAsync";
        var binarySupported = CanEmitBinaryType(generation, effectiveType);
        var binaryName = "__WebSceneBinary"
                         + PascalCase(property.Name)
                         + "Global";
        source.AppendLine()
            .Append("    public static ");
        if (!binarySupported)
        {
            source.Append("async ");
        }
        source.Append("global::System.Threading.Tasks.ValueTask<")
            .Append(mapping.CSharpType).Append("> ").Append(property.Name).AppendLine("(")
            .AppendLine("        global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker,")
            .AppendLine("        global::System.Threading.CancellationToken cancellationToken = default)")
            .AppendLine("    {")
            .AppendLine("        global::System.ArgumentNullException.ThrowIfNull(invoker);");
        if (binarySupported)
        {
            source.AppendLine("        var binaryInvoker = invoker as global::WebScene.JavaScript.Interop.IJavaScriptBinaryInvoker")
                .AppendLine("            ?? throw new global::System.NotSupportedException(")
                .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
                .AppendLine("        if (!binaryInvoker.IsBinaryInteropAvailable)")
                .AppendLine("        {")
                .AppendLine("            throw new global::System.NotSupportedException(")
                .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
                .AppendLine("        }")
                .Append("        return binaryInvoker.InvokeBinaryAsync<global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid, ")
                .Append(mapping.CSharpType).Append(", ").Append(binaryName)
                .Append("Codec>(").Append(binaryName)
                .AppendLine("CallSite, default, new global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid(), cancellationToken);")
                .AppendLine("    }");
            EmitBinaryGlobalPropertyCodec(
                source,
                generation,
                effectiveType,
                mapping,
                binaryName,
                property.GlobalName,
                promise);
            return;
        }
        if (mapping.IsBinding && mapping.IsNullable)
        {
            source.Append("        var reference = await invoker.")
                .Append(valueInvocation)
                .Append("<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                .Append(Literal(property.GlobalName))
                .AppendLine(", cancellationToken).ConfigureAwait(false);")
                .Append("        return reference is { } value ? new ")
                .Append(mapping.NonNullableCSharpType)
                .AppendLine("(invoker, value) : null;");
        }
        else if (mapping.IsBinding)
        {
            source.Append("        return new ").Append(mapping.CSharpType)
                .Append("(invoker, await invoker.").Append(objectInvocation).Append('(')
                .Append(Literal(property.GlobalName))
                .AppendLine(", cancellationToken).ConfigureAwait(false));");
        }
        else if (mapping.IsFunctionReference && mapping.IsNullable)
        {
            source.Append("        var reference = await invoker.")
                .Append(valueInvocation)
                .Append("<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                .Append(Literal(property.GlobalName))
                .AppendLine(", cancellationToken).ConfigureAwait(false);")
                .AppendLine("        return reference is { } value")
                .AppendLine("            ? new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(invoker, value)")
                .AppendLine("            : null;");
        }
        else if (mapping.IsFunctionReference)
        {
            source.Append("        return new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(invoker, await invoker.")
                .Append(objectInvocation).Append('(')
                .Append(Literal(property.GlobalName))
                .AppendLine(", cancellationToken).ConfigureAwait(false));");
        }
        else if (mapping.IsObjectReference && !mapping.IsNullable)
        {
            source.Append("        return await invoker.").Append(objectInvocation).Append('(')
                .Append(Literal(property.GlobalName))
                .AppendLine(", cancellationToken).ConfigureAwait(false);");
        }
        else if (mapping.RequiresWireConversion)
        {
            source.Append("        var wire = (await invoker.")
                .Append(valueInvocation).Append('<')
                .Append(mapping.EffectiveWireCSharpType).Append(">(")
                .Append(Literal(property.GlobalName))
                .AppendLine(", cancellationToken).ConfigureAwait(false))!;")
                .Append("        return ")
                .Append(mapping.ConvertFromWire("wire", "invoker"))
                .AppendLine(";");
        }
        else
        {
            source.Append("        return (await invoker.").Append(valueInvocation).Append('<')
                .Append(mapping.CSharpType).Append(">(")
                .Append(Literal(property.GlobalName))
                .AppendLine(", cancellationToken).ConfigureAwait(false))!;");
        }
        source.AppendLine("    }");
    }

    private static void EmitBinaryGlobalPropertyCodec(
        StringBuilder source,
        GenerationContext generation,
        JsonElement effectiveType,
        TypeMapping mapping,
        string binaryName,
        string globalName,
        bool promise)
    {
        var resultMode = mapping.IsBinding
                         || mapping.IsObjectReference
                         || mapping.IsFunctionReference
            ? "RetainedHandle"
            : "Value";
        source.AppendLine()
            .Append("    private static readonly global::WebScene.JavaScript.Interop.JavaScriptBinaryCallSite ")
            .Append(binaryName).AppendLine("CallSite = new(")
            .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryOperation.GetGlobal,")
            .Append("        globalName: ").Append(Literal(globalName))
            .AppendLine(",")
            .AppendLine("        memberName: null,")
            .Append("        global::WebScene.JavaScript.Interop.JavaScriptBinaryResultMode.")
            .Append(resultMode);
        if (promise)
        {
            source.AppendLine(",")
                .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryCallFlags.AwaitPromise);");
        }
        else
        {
            source.AppendLine(");");
        }
        source.AppendLine()
            .Append("    private readonly struct ").Append(binaryName)
            .Append("Codec : global::WebScene.JavaScript.Interop.IJavaScriptBinaryCodec<global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid, ")
            .Append(mapping.CSharpType).AppendLine(">")
            .AppendLine("    {")
            .AppendLine("        public static uint EncodeArguments(")
            .AppendLine("            ref global::WebScene.JavaScript.Interop.JavaScriptBinaryWriter writer,")
            .AppendLine("            in global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid arguments)")
            .AppendLine("            => writer.BeginArray(0);")
            .AppendLine()
            .Append("        public static ").Append(mapping.CSharpType)
            .AppendLine(" DecodeResult(")
            .AppendLine("            global::WebScene.JavaScript.Interop.JavaScriptBinaryValue value,")
            .AppendLine("            global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker)")
            .AppendLine("        {");
        var result = EmitBinaryReadValue(
            source,
            generation,
            effectiveType,
            "value",
            "invoker",
            "            ");
        source.Append("            return ").Append(result).AppendLine(";")
            .AppendLine("        }")
            .AppendLine("    }");
    }

    private static void EmitGlobalFunction(
        StringBuilder source,
        GenerationContext generation,
        GlobalFunction function)
    {
        var method = function.Method;
        var typeParameters = method.TryGetProperty("typeParameters", out var genericParameters)
            ? genericParameters.EnumerateArray()
                .Select(static item => EscapeIdentifier(item.GetString()!))
                .ToArray()
            : [];
        var genericSuffix = typeParameters.Length == 0
            ? string.Empty
            : "<" + string.Join(", ", typeParameters) + ">";
        var omitOptional = !function.Policy.TryGetProperty(
                               "omitOptionalParameters",
                               out var omitValue)
                           || omitValue.ValueKind != JsonValueKind.False;
        var parameters = method.GetProperty("parameters")
            .EnumerateArray()
            .Where(parameter =>
                !omitOptional
                || !parameter.GetProperty("optional").GetBoolean())
            .Select(parameter =>
            {
                var name = parameter.GetProperty("name").GetString()!;
                var optional = parameter.GetProperty("optional").GetBoolean();
                var rest = parameter.GetProperty("rest").GetBoolean();
                var mapping = MapOptionalParameterType(
                    generation,
                    parameter.GetProperty("type"),
                    optional,
                    $"{function.Name} parameter '{name}'");
                if (optional)
                {
                    mapping = mapping with
                    {
                        CSharpType =
                            $"global::WebScene.JavaScript.Interop.JavaScriptOptional<{mapping.CSharpType}>"
                    };
                }
                return new MappedParameter(
                    name,
                    mapping,
                    optional,
                    rest,
                    parameter.GetProperty("type").Clone());
            })
            .ToArray();
        var declaredReturn = method.GetProperty("returns");
        var promise = IsPromiseLikeReturn(declaredReturn);
        var effectiveReturn = Kind(declaredReturn) == "promise"
            ? declaredReturn.GetProperty("result")
            : declaredReturn;
        var returnsVoid = IsVoidLikeReturn(declaredReturn);
        var returnMapping = returnsVoid
            ? new TypeMapping("void", false)
            : MapType(
                generation,
                effectiveReturn,
                optional: false,
                $"{function.Name} return");
        var binarySupported = typeParameters.Length == 0
                              && parameters.All(parameter =>
                                  !parameter.Rest
                                  && CanEmitBinaryType(
                                      generation,
                                      parameter.Type))
                              && (returnsVoid
                                  || CanEmitBinaryType(
                                      generation,
                                      effectiveReturn));
        var binaryName = "__WebSceneBinary"
                         + PascalCase(function.Name)
                         + "Global";

        source.AppendLine()
            .Append("    public static ");
        if (!binarySupported)
        {
            source.Append("async ");
        }
        source.Append("global::System.Threading.Tasks.ValueTask");
        if (!returnsVoid)
        {
            source.Append('<').Append(returnMapping.CSharpType).Append('>');
        }
        source.Append(' ').Append(function.Name).Append(genericSuffix).AppendLine("(")
            .AppendLine("        global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker,");
        foreach (var parameter in parameters)
        {
            source.Append("        ").Append(parameter.Mapping.CSharpType)
                .Append(" @").Append(parameter.Name);
            if (parameter.Optional)
            {
                source.Append(" = default");
            }
            source.AppendLine(",");
        }
        source.AppendLine("        global::System.Threading.CancellationToken cancellationToken = default)")
            .AppendLine("    {")
            .AppendLine("        global::System.ArgumentNullException.ThrowIfNull(invoker);");
        if (binarySupported)
        {
            EmitBinaryGlobalFunctionDispatch(
                source,
                parameters,
                returnsVoid,
                returnMapping,
                binaryName);
            source.AppendLine("    }");
            EmitBinaryInvocationCodec(
                source,
                generation,
                parameters,
                returnsVoid,
                effectiveReturn,
                returnMapping,
                binaryName,
                operation: "InvokeGlobal",
                globalName: function.GlobalName,
                memberName: null,
                promise: promise);
            return;
        }

        var arguments = ArgumentArray(parameters.Select(parameter =>
            new Parameter(
                parameter.Name,
                parameter.Mapping.IsBinding,
                parameter.Mapping.IsNullable,
                parameter.Optional,
                parameter.Rest)));
        if (promise)
        {
            if (returnsVoid)
            {
                source.Append("        await invoker.InvokeGlobalPromiseAsync<global::System.Text.Json.JsonElement>(")
                    .Append(Literal(function.GlobalName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false);");
            }
            else if (returnMapping.IsBinding && returnMapping.IsNullable)
            {
                source.Append("        var reference = await invoker.InvokeGlobalPromiseAsync<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                    .Append(Literal(function.GlobalName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false);")
                    .Append("        return reference is { } value ? new ")
                    .Append(returnMapping.NonNullableCSharpType)
                    .AppendLine("(invoker, value) : null;");
            }
            else if (returnMapping.IsBinding)
            {
                source.Append("        return new ").Append(returnMapping.CSharpType)
                    .Append("(invoker, await invoker.InvokeGlobalPromiseObjectAsync(")
                    .Append(Literal(function.GlobalName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false));");
            }
            else if (returnMapping.IsFunctionReference && returnMapping.IsNullable)
            {
                source.Append("        var reference = await invoker.InvokeGlobalPromiseAsync<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                    .Append(Literal(function.GlobalName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false);")
                    .AppendLine("        return reference is { } value")
                    .AppendLine("            ? new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(invoker, value)")
                    .AppendLine("            : null;");
            }
            else if (returnMapping.IsFunctionReference)
            {
                source.Append("        return new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(invoker, await invoker.InvokeGlobalPromiseObjectAsync(")
                    .Append(Literal(function.GlobalName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false));");
            }
            else if (returnMapping.IsObjectReference && !returnMapping.IsNullable)
            {
                source.Append("        return await invoker.InvokeGlobalPromiseObjectAsync(")
                    .Append(Literal(function.GlobalName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false);");
            }
            else if (returnMapping.RequiresWireConversion)
            {
                source.Append("        var wire = (await invoker.InvokeGlobalPromiseAsync<")
                    .Append(returnMapping.EffectiveWireCSharpType).Append(">(")
                    .Append(Literal(function.GlobalName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false))!;")
                    .Append("        return ")
                    .Append(returnMapping.ConvertFromWire("wire", "invoker"))
                    .AppendLine(";");
            }
            else
            {
                source.Append("        return (await invoker.InvokeGlobalPromiseAsync<")
                    .Append(returnMapping.CSharpType).Append(">(")
                    .Append(Literal(function.GlobalName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false))!;");
            }
        }
        else if (returnsVoid)
        {
            source.Append("        await invoker.InvokeGlobalVoidAsync(")
                .Append(Literal(function.GlobalName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false);");
        }
        else if (returnMapping.IsBinding && returnMapping.IsNullable)
        {
            source.Append("        var reference = await invoker.InvokeGlobalAsync<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                .Append(Literal(function.GlobalName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false);")
                .Append("        return reference is { } value ? new ")
                .Append(returnMapping.NonNullableCSharpType)
                .AppendLine("(invoker, value) : null;");
        }
        else if (returnMapping.IsBinding)
        {
            source.Append("        return new ").Append(returnMapping.CSharpType)
                .Append("(invoker, await invoker.InvokeGlobalObjectAsync(")
                .Append(Literal(function.GlobalName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false));");
        }
        else if (returnMapping.IsFunctionReference && returnMapping.IsNullable)
        {
            source.Append("        var reference = await invoker.InvokeGlobalAsync<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                .Append(Literal(function.GlobalName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false);")
                .AppendLine("        return reference is { } value")
                .AppendLine("            ? new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(invoker, value)")
                .AppendLine("            : null;");
        }
        else if (returnMapping.IsFunctionReference)
        {
            source.Append("        return new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(invoker, await invoker.InvokeGlobalObjectAsync(")
                .Append(Literal(function.GlobalName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false));");
        }
        else if (returnMapping.IsObjectReference && !returnMapping.IsNullable)
        {
            source.Append("        return await invoker.InvokeGlobalObjectAsync(")
                .Append(Literal(function.GlobalName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false);");
        }
        else if (returnMapping.RequiresWireConversion)
        {
            source.Append("        var wire = (await invoker.InvokeGlobalAsync<")
                .Append(returnMapping.EffectiveWireCSharpType).Append(">(")
                .Append(Literal(function.GlobalName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false))!;")
                .Append("        return ")
                .Append(returnMapping.ConvertFromWire("wire", "invoker"))
                .AppendLine(";");
        }
        else
        {
            source.Append("        return (await invoker.InvokeGlobalAsync<")
                .Append(returnMapping.CSharpType).Append(">(")
                .Append(Literal(function.GlobalName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false))!;");
        }
        source.AppendLine("    }");
    }

    private static void EmitBinaryGlobalFunctionDispatch(
        StringBuilder source,
        IReadOnlyList<MappedParameter> parameters,
        bool returnsVoid,
        TypeMapping returnMapping,
        string binaryName)
    {
        var argumentsType = parameters.Count == 0
            ? "global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid"
            : binaryName + "Arguments";
        var arguments = parameters.Count == 0
            ? "new global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid()"
            : "new " + binaryName + "Arguments("
              + string.Join(
                  ", ",
                  parameters.Select(parameter => "@" + parameter.Name))
              + ")";
        source.AppendLine("        var binaryInvoker = invoker as global::WebScene.JavaScript.Interop.IJavaScriptBinaryInvoker")
            .AppendLine("            ?? throw new global::System.NotSupportedException(")
            .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
            .AppendLine("        if (!binaryInvoker.IsBinaryInteropAvailable)")
            .AppendLine("        {")
            .AppendLine("            throw new global::System.NotSupportedException(")
            .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
            .AppendLine("        }");
        if (returnsVoid)
        {
            source.Append("        return binaryInvoker.InvokeBinaryVoidAsync<")
                .Append(argumentsType).Append(", ").Append(binaryName)
                .Append("Codec>(").Append(binaryName)
                .Append("CallSite, default, ")
                .Append(arguments)
                .AppendLine(", cancellationToken);");
        }
        else
        {
            source.Append("        return binaryInvoker.InvokeBinaryAsync<")
                .Append(argumentsType).Append(", ")
                .Append(returnMapping.CSharpType).Append(", ")
                .Append(binaryName).Append("Codec>(")
                .Append(binaryName)
                .Append("CallSite, default, ")
                .Append(arguments)
                .AppendLine(", cancellationToken);");
        }
    }

    private static string GenerateAdapter(
        GenerationContext generation,
        Adapter adapter,
        string policyPath)
    {
        var adapterTypeParameters = adapter.Type.TryGetProperty(
            "typeParameters",
            out var declaredTypeParameters)
            ? declaredTypeParameters.EnumerateArray()
                .Select(static item => EscapeIdentifier(item.GetString()!))
                .ToArray()
            : [];
        var adapterTypeName = adapter.Name + (adapterTypeParameters.Length == 0
            ? string.Empty
            : "<" + string.Join(", ", adapterTypeParameters) + ">");
        var selectedMethods = adapter.Policy.TryGetProperty("methods", out var methods)
            ? methods.EnumerateArray()
                .Where(static method =>
                    !method.TryGetProperty("include", out var include)
                    || include.ValueKind != JsonValueKind.False)
                .Select(methodPolicy =>
                {
                    var sourceName = RequiredString(methodPolicy, "source", policyPath);
                    var overload = methodPolicy.TryGetProperty("overload", out var overloadValue)
                        ? overloadValue.GetInt32()
                        : 0;
                    var method = adapter.Type.GetProperty("methods")
                        .EnumerateArray()
                        .FirstOrDefault(candidate =>
                            string.Equals(
                                candidate.GetProperty("name").GetString(),
                                sourceName,
                                StringComparison.Ordinal)
                            && candidate.GetProperty("overload").GetInt32() == overload);
                    if (method.ValueKind == JsonValueKind.Undefined)
                    {
                        throw new InvalidDataException(
                            $"Adapter method '{adapter.Source}.{sourceName}' overload {overload} was not discovered.");
                    }
                    return new AdapterMethod(
                        sourceName,
                        OptionalString(methodPolicy, "name")
                            ?? SuggestedAdapterMethodName(sourceName, method),
                        method);
                })
                .ToArray()
            : adapter.Type.GetProperty("methods")
                .EnumerateArray()
                .Where(static method => method.GetProperty("overload").GetInt32() == 0)
                .Select(method => new AdapterMethod(
                    method.GetProperty("name").GetString()!,
                    SuggestedAdapterMethodName(
                        method.GetProperty("name").GetString()!,
                        method),
                    method))
                .ToArray();
        var binaryAdapterSupported = selectedMethods.All(
            method => CanEmitBinaryAdapterMethod(
                generation,
                adapter,
                method));
        var source = new StringBuilder(
            """
            // <auto-generated />
            #nullable enable

            """);
        source.Append("namespace ").Append(generation.Namespace).AppendLine(";")
            .AppendLine()
            .Append("public abstract class ").Append(adapterTypeName)
            .AppendLine(binaryAdapterSupported
                ? " : global::WebScene.JavaScript.Interop.IJavaScriptBinaryCallbackTarget, global::WebScene.JavaScript.Interop.IJavaScriptObjectReferenceProvider, global::System.IAsyncDisposable"
                : " : global::WebScene.JavaScript.Interop.IJavaScriptCallbackTarget, global::WebScene.JavaScript.Interop.IJavaScriptObjectReferenceProvider, global::System.IAsyncDisposable")
            .AppendLine("{");
        source.Append("    private global::WebScene.JavaScript.Interop.")
            .Append(binaryAdapterSupported
                ? "IJavaScriptBinaryBidirectionalInvoker"
                : "IJavaScriptBidirectionalInvoker")
            .AppendLine("? __webSceneInvoker;")
            .AppendLine("    private global::WebScene.JavaScript.Interop.JavaScriptObjectReference __webSceneReference;")
            .AppendLine()
            .AppendLine("    public global::WebScene.JavaScript.Interop.JavaScriptObjectReference JavaScriptReference => __webSceneReference;")
            .AppendLine()
            .AppendLine("    public async global::System.Threading.Tasks.ValueTask<global::WebScene.JavaScript.Interop.JavaScriptObjectReference> RegisterAsync(");
        source.Append("        global::WebScene.JavaScript.Interop.")
            .Append(binaryAdapterSupported
                ? "IJavaScriptBinaryBidirectionalInvoker"
                : "IJavaScriptBidirectionalInvoker")
            .AppendLine(" invoker,")
            .AppendLine("        global::System.Threading.CancellationToken cancellationToken = default)")
            .AppendLine("    {")
            .AppendLine("        global::System.ArgumentNullException.ThrowIfNull(invoker);")
            .AppendLine("        if (!__webSceneReference.IsEmpty)")
            .AppendLine("        {")
            .AppendLine("            throw new global::System.InvalidOperationException(\"This callback adapter is already registered.\");")
            .AppendLine("        }")
            .AppendLine("        __webSceneInvoker = invoker;");
        if (binaryAdapterSupported)
        {
            source.AppendLine("        __webSceneReference = await invoker.RegisterBinaryCallbackTargetAsync(")
                .AppendLine("            this,")
                .AppendLine("            new global::WebScene.JavaScript.Interop.JavaScriptBinaryCallbackMethod[]")
                .AppendLine("            {");
            for (var index = 0; index < selectedMethods.Length; index++)
            {
                var method = selectedMethods[index];
                source.Append("                new(")
                    .Append(Literal(method.SourceName)).Append(", ")
                    .Append(index).Append("U, global::WebScene.JavaScript.Interop.JavaScriptCallbackReturnKind.")
                    .Append(AdapterReturnKind(method.Method));
                if (AdapterReturnKind(method.Method) == "Synchronous"
                    && method.Method.GetProperty("parameters").GetArrayLength() == 0)
                {
                    source.Append(", true");
                }
                source.AppendLine("),");
            }
            source.AppendLine("            },")
                .AppendLine("            cancellationToken).ConfigureAwait(false);");
        }
        else
        {
            source.AppendLine("        __webSceneReference = await invoker.RegisterCallbackTargetAsync(")
                .AppendLine("            this,")
                .AppendLine("            new global::WebScene.JavaScript.Interop.JavaScriptCallbackMethod[]")
                .AppendLine("            {");
            foreach (var method in selectedMethods)
            {
                source.Append("                new(").Append(Literal(method.SourceName))
                    .Append(", global::WebScene.JavaScript.Interop.JavaScriptCallbackReturnKind.")
                    .Append(AdapterReturnKind(method.Method));
                if (AdapterReturnKind(method.Method) == "Synchronous"
                    && method.Method.GetProperty("parameters").GetArrayLength() == 0)
                {
                    source.Append(", global::WebScene.JavaScript.Interop.JavaScriptJson.Serialize(")
                        .Append(method.Name).Append("(cancellationToken))");
                }
                source.AppendLine("),");
            }
            source.AppendLine("            },")
                .AppendLine("            cancellationToken).ConfigureAwait(false);");
        }
        source
            .AppendLine("        return __webSceneReference;")
            .AppendLine("    }")
            .AppendLine()
            .AppendLine("    public async global::System.Threading.Tasks.ValueTask DisposeAsync()")
            .AppendLine("    {")
            .AppendLine("        if (__webSceneInvoker is not null && !__webSceneReference.IsEmpty)")
            .AppendLine("        {")
            .AppendLine("            await __webSceneInvoker.ReleaseAsync(__webSceneReference).ConfigureAwait(false);")
            .AppendLine("            __webSceneReference = default;")
            .AppendLine("        }")
            .AppendLine("    }");

        foreach (var method in selectedMethods)
        {
            EmitAdapterMethod(source, generation, adapter, method);
        }
        if (binaryAdapterSupported)
        {
            EmitAdapterBinaryDispatch(
                source,
                generation,
                adapter,
                selectedMethods);
        }
        else
        {
            EmitAdapterDispatch(source, generation, adapter, selectedMethods);
        }
        return source.AppendLine("}").ToString();
    }

    private static void EmitAdapterMethod(
        StringBuilder source,
        GenerationContext generation,
        Adapter adapter,
        AdapterMethod method)
    {
        var erasedTypeParameters = AdapterMethodTypeParameters(method.Method);
        var declaredReturn = method.Method.GetProperty("returns");
        var promise = IsPromiseLikeReturn(declaredReturn);
        var effectiveReturn = Kind(declaredReturn) == "promise"
            ? declaredReturn.GetProperty("result")
            : declaredReturn;
        var returnsVoid = IsVoidLikeReturn(declaredReturn);
        var returnMapping = returnsVoid
            ? new TypeMapping("void", false)
            : MapAdapterType(
                generation,
                effectiveReturn,
                optional: false,
                $"{adapter.Name}.{method.Name} return",
                erasedTypeParameters);
        source.AppendLine();
        if (promise || returnsVoid)
        {
            source.Append("    public abstract global::System.Threading.Tasks.ValueTask");
            if (!returnsVoid)
            {
                source.Append('<').Append(returnMapping.CSharpType).Append('>');
            }
        }
        else
        {
            source.Append("    public abstract ").Append(returnMapping.CSharpType);
        }
        source.Append(' ').Append(method.Name).AppendLine("(");
        foreach (var parameter in method.Method.GetProperty("parameters").EnumerateArray())
        {
            var name = parameter.GetProperty("name").GetString()!;
            var optional = parameter.GetProperty("optional").GetBoolean();
            var mapping = MapAdapterParameterType(
                generation,
                parameter.GetProperty("type"),
                optional,
                $"{adapter.Name}.{method.Name} parameter '{name}'",
                erasedTypeParameters);
            var parameterType = optional
                ? $"global::WebScene.JavaScript.Interop.JavaScriptOptional<{mapping.CSharpType}>"
                : mapping.CSharpType;
            source.Append("        ").Append(parameterType)
                .Append(' ').Append(EscapeIdentifier(name)).AppendLine(",");
        }
        source.AppendLine("        global::System.Threading.CancellationToken cancellationToken = default);");
    }

    private static void EmitAdapterDispatch(
        StringBuilder source,
        GenerationContext generation,
        Adapter adapter,
        IReadOnlyList<AdapterMethod> methods)
    {
        source.AppendLine()
            .AppendLine("    async global::System.Threading.Tasks.ValueTask<object?> global::WebScene.JavaScript.Interop.IJavaScriptCallbackTarget.DispatchAsync(")
            .AppendLine("        string method,")
            .AppendLine("        global::System.Text.Json.JsonElement arguments,")
            .AppendLine("        global::System.Threading.CancellationToken cancellationToken)")
            .AppendLine("    {")
            .AppendLine("        var invoker = __webSceneInvoker ?? throw new global::System.InvalidOperationException(\"The callback adapter is not registered.\");")
            .AppendLine("        switch (method)")
            .AppendLine("        {");
        foreach (var method in methods)
        {
            var erasedTypeParameters = AdapterMethodTypeParameters(method.Method);
            var declaredReturn = method.Method.GetProperty("returns");
            var promise = IsPromiseLikeReturn(declaredReturn);
            var effectiveReturn = Kind(declaredReturn) == "promise"
                ? declaredReturn.GetProperty("result")
                : declaredReturn;
            var returnsVoid = IsVoidLikeReturn(declaredReturn);
            source.Append("            case ").Append(Literal(method.SourceName)).AppendLine(":")
                .Append("                ");
            if (promise && !returnsVoid)
            {
                source.Append("return await ");
            }
            else if (promise || returnsVoid)
            {
                source.Append("await ");
            }
            else
            {
                source.Append("return ");
            }
            source.Append(method.Name).AppendLine("(");
            var parameters = method.Method.GetProperty("parameters").EnumerateArray().ToArray();
            for (var index = 0; index < parameters.Length; index++)
            {
                var parameter = parameters[index];
                var optional = parameter.GetProperty("optional").GetBoolean();
                var mapping = MapAdapterParameterType(
                    generation,
                    parameter.GetProperty("type"),
                    optional,
                    $"{adapter.Name}.{method.Name} dispatch",
                    erasedTypeParameters);
                if (optional && mapping.IsBinding)
                {
                    source.Append("                    global::WebScene.JavaScript.Interop.JavaScriptCallbackArguments.HasValue(arguments, ")
                        .Append(index).AppendLine(")")
                        .Append("                        ? new global::WebScene.JavaScript.Interop.JavaScriptOptional<")
                        .Append(mapping.CSharpType).AppendLine(">(")
                        .Append("                            global::WebScene.JavaScript.Interop.JavaScriptCallbackArguments.IsNull(arguments, ")
                        .Append(index).AppendLine(")")
                        .AppendLine("                                ? null")
                        .Append("                                : new ")
                        .Append(mapping.NonNullableCSharpType)
                        .Append("(invoker, global::WebScene.JavaScript.Interop.JavaScriptCallbackArguments.Get<global::WebScene.JavaScript.Interop.JavaScriptObjectReference>(arguments, ")
                        .Append(index).AppendLine(", invoker)))")
                        .AppendLine("                        : default,");
                }
                else if (optional && mapping.IsCallbackWrapper)
                {
                    source.Append("                    global::WebScene.JavaScript.Interop.JavaScriptCallbackArguments.HasValue(arguments, ")
                        .Append(index).AppendLine(")")
                        .Append("                        ? new global::WebScene.JavaScript.Interop.JavaScriptOptional<")
                        .Append(mapping.CSharpType).AppendLine(">(")
                        .Append("                            global::WebScene.JavaScript.Interop.JavaScriptCallbackArguments.IsNull(arguments, ")
                        .Append(index).AppendLine(")")
                        .AppendLine("                                ? null")
                        .Append("                                : new ")
                        .Append(mapping.NonNullableCSharpType)
                        .Append("(invoker, global::WebScene.JavaScript.Interop.JavaScriptCallbackArguments.GetFunctionReference(arguments, ")
                        .Append(index).AppendLine(", invoker)))")
                        .AppendLine("                        : default,");
                }
                else if (optional)
                {
                    source.Append("                    global::WebScene.JavaScript.Interop.JavaScriptCallbackArguments.GetOptional<")
                        .Append(mapping.CSharpType).Append(">(arguments, ").Append(index)
                        .AppendLine(", invoker),");
                }
                else if (mapping.IsCallbackWrapper)
                {
                    source.Append("                    new ").Append(mapping.CSharpType)
                        .Append("(invoker, global::WebScene.JavaScript.Interop.JavaScriptCallbackArguments.GetFunctionReference(arguments, ")
                        .Append(index).AppendLine(", invoker)),");
                }
                else if (mapping.IsBinding)
                {
                    source.Append("                    new ")
                        .Append(mapping.NonNullableCSharpType)
                        .Append("(invoker, global::WebScene.JavaScript.Interop.JavaScriptCallbackArguments.Get<global::WebScene.JavaScript.Interop.JavaScriptObjectReference>(arguments, ")
                        .Append(index).AppendLine(", invoker)),");
                }
                else
                {
                    source.Append("                    global::WebScene.JavaScript.Interop.JavaScriptCallbackArguments.Get<")
                        .Append(mapping.CSharpType).Append(">(arguments, ").Append(index)
                        .AppendLine(", invoker)!,");
                }
            }
            source.AppendLine("                    cancellationToken)");
            if (promise || returnsVoid)
            {
                source.AppendLine("                    .ConfigureAwait(false);");
                if (returnsVoid)
                {
                    source.AppendLine("                return null;");
                }
            }
            else
            {
                source.AppendLine(";");
            }
        }
        source.AppendLine("            default:")
            .AppendLine("                throw new global::System.MissingMethodException($\"Unknown JavaScript callback method '{method}'.\");")
            .AppendLine("        }")
            .AppendLine("    }");
    }

    private static void EmitAdapterBinaryDispatch(
        StringBuilder source,
        GenerationContext generation,
        Adapter adapter,
        IReadOnlyList<AdapterMethod> methods)
    {
        var binaryActionHelpers = new StringBuilder();
        var binaryActionHelperIndex = 0;
        source.AppendLine()
            .AppendLine("    global::System.Threading.Tasks.ValueTask global::WebScene.JavaScript.Interop.IJavaScriptBinaryCallbackTarget.DispatchBinaryAsync(")
            .AppendLine("        uint methodId,")
            .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryValue arguments,")
            .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryCallbackCompletion completion,")
            .AppendLine("        global::System.Threading.CancellationToken cancellationToken)")
            .AppendLine("    {")
            .AppendLine("        var invoker = __webSceneInvoker ?? throw new global::System.InvalidOperationException(\"The callback adapter is not registered.\");")
            .AppendLine("        if (arguments.Kind != global::WebScene.JavaScript.Interop.JavaScriptBinaryValueKind.Array)")
            .AppendLine("        {")
            .AppendLine("            throw new global::System.IO.InvalidDataException(\"Binary callback arguments must be an array.\");")
            .AppendLine("        }")
            .AppendLine("        switch (methodId)")
            .AppendLine("        {");
        for (var methodIndex = 0; methodIndex < methods.Count; methodIndex++)
        {
            source.Append("            case ").Append(methodIndex)
                .Append("U: return __WebSceneDispatchBinaryCallback")
                .Append(methodIndex)
                .AppendLine("(arguments, completion, invoker, cancellationToken);");
        }
        source.AppendLine("            default:")
            .AppendLine("                throw new global::System.MissingMethodException($\"Unknown binary JavaScript callback method ID {methodId}.\");")
            .AppendLine("        }")
            .AppendLine("    }");

        for (var methodIndex = 0; methodIndex < methods.Count; methodIndex++)
        {
            var method = methods[methodIndex];
            var erasedTypeParameters = AdapterMethodTypeParameters(method.Method);
            var declaredReturn = method.Method.GetProperty("returns");
            var promise = IsPromiseLikeReturn(declaredReturn);
            var effectiveReturn = Kind(declaredReturn) == "promise"
                ? declaredReturn.GetProperty("result")
                : declaredReturn;
            var returnsVoid = IsVoidLikeReturn(declaredReturn);
            source.AppendLine()
                .Append("    private global::System.Threading.Tasks.ValueTask __WebSceneDispatchBinaryCallback")
                .Append(methodIndex).AppendLine("(")
                .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryValue arguments,")
                .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryCallbackCompletion completion,")
                .AppendLine("        global::WebScene.JavaScript.Interop.IJavaScriptBidirectionalInvoker invoker,")
                .AppendLine("        global::System.Threading.CancellationToken cancellationToken)")
                .AppendLine("    {");
            var decodedArguments = new List<string>();
            var parameters = method.Method.GetProperty("parameters")
                .EnumerateArray()
                .ToArray();
            for (var parameterIndex = 0;
                 parameterIndex < parameters.Length;
                 parameterIndex++)
            {
                decodedArguments.Add(EmitAdapterBinaryArgument(
                    source,
                    binaryActionHelpers,
                    ref binaryActionHelperIndex,
                    generation,
                    adapter,
                    method,
                    parameters[parameterIndex],
                    parameterIndex,
                    erasedTypeParameters,
                    "        "));
            }
            decodedArguments.Add("cancellationToken");
            var invocation = method.Name + "("
                + string.Join(", ", decodedArguments) + ")";
            if (returnsVoid)
            {
                var pending = generation.NextLocal("callbackPending");
                source.Append("        var ").Append(pending)
                    .Append(" = ").Append(invocation).AppendLine(";")
                    .Append("        if (").Append(pending)
                    .AppendLine(".IsCompletedSuccessfully)")
                    .AppendLine("        {")
                    .Append("            ").Append(pending)
                    .AppendLine(".GetAwaiter().GetResult();")
                    .AppendLine("            completion.SetVoid();")
                    .AppendLine("            return global::System.Threading.Tasks.ValueTask.CompletedTask;")
                    .AppendLine("        }")
                    .Append("        return __WebSceneAwaitBinaryCallback")
                    .Append(methodIndex).Append('(').Append(pending)
                    .AppendLine(", completion);");
            }
            else if (promise)
            {
                if (!CanEmitBinaryType(generation, effectiveReturn))
                {
                    source.AppendLine("        throw new global::System.NotSupportedException(\"This callback return type has no generated binary codec.\");");
                }
                else
                {
                    var returnMapping = MapAdapterType(
                        generation,
                        effectiveReturn,
                        optional: false,
                        $"{adapter.Name}.{method.Name} binary return",
                        erasedTypeParameters);
                    var pending = generation.NextLocal("callbackPending");
                    var result = generation.NextLocal("callbackResult");
                    source.Append("        var ").Append(pending)
                        .Append(" = ").Append(invocation).AppendLine(";")
                        .Append("        if (").Append(pending)
                        .AppendLine(".IsCompletedSuccessfully)")
                        .AppendLine("        {")
                        .Append("            var ").Append(result)
                        .Append(" = ").Append(pending)
                        .AppendLine(".GetAwaiter().GetResult();")
                        .Append("            completion.SetResult<")
                        .Append(returnMapping.CSharpType).Append(", __WebSceneCallbackResultCodec")
                        .Append(methodIndex).Append(">(in ").Append(result)
                        .AppendLine(");")
                        .AppendLine("            return global::System.Threading.Tasks.ValueTask.CompletedTask;")
                        .AppendLine("        }")
                        .Append("        return __WebSceneAwaitBinaryCallback")
                        .Append(methodIndex).Append('(').Append(pending)
                        .AppendLine(", completion);");
                }
            }
            else
            {
                if (!CanEmitBinaryType(generation, effectiveReturn))
                {
                    source.AppendLine("        throw new global::System.NotSupportedException(\"This synchronous callback return type has no generated binary codec.\");");
                }
                else
                {
                    var returnMapping = MapAdapterType(
                        generation,
                        effectiveReturn,
                        optional: false,
                        $"{adapter.Name}.{method.Name} binary return",
                        erasedTypeParameters);
                    var result = generation.NextLocal("callbackResult");
                    source.Append("        var ").Append(result)
                        .Append(" = ").Append(invocation).AppendLine(";")
                        .Append("        completion.SetResult<")
                        .Append(returnMapping.CSharpType)
                        .Append(", __WebSceneCallbackResultCodec")
                        .Append(methodIndex).Append(">(in ").Append(result)
                        .AppendLine(");")
                        .AppendLine("        return global::System.Threading.Tasks.ValueTask.CompletedTask;");
                }
            }
            source.AppendLine("    }");
        }

        for (var methodIndex = 0; methodIndex < methods.Count; methodIndex++)
        {
            var method = methods[methodIndex];
            var declaredReturn = method.Method.GetProperty("returns");
            var promise = IsPromiseLikeReturn(declaredReturn);
            var effectiveReturn = Kind(declaredReturn) == "promise"
                ? declaredReturn.GetProperty("result")
                : declaredReturn;
            var returnsVoid = IsVoidLikeReturn(declaredReturn);
            var erasedTypeParameters = AdapterMethodTypeParameters(method.Method);
            if (returnsVoid)
            {
                source.AppendLine()
                    .Append("    private static async global::System.Threading.Tasks.ValueTask __WebSceneAwaitBinaryCallback")
                    .Append(methodIndex).AppendLine("(")
                    .AppendLine("        global::System.Threading.Tasks.ValueTask pending,")
                    .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryCallbackCompletion completion)")
                    .AppendLine("    {")
                    .AppendLine("        await pending.ConfigureAwait(false);")
                    .AppendLine("        completion.SetVoid();")
                    .AppendLine("    }");
                continue;
            }
            if (!CanEmitBinaryType(generation, effectiveReturn))
            {
                continue;
            }
            var returnMapping = MapAdapterType(
                generation,
                effectiveReturn,
                optional: false,
                $"{adapter.Name}.{method.Name} binary result codec",
                erasedTypeParameters);
            if (promise)
            {
                source.AppendLine()
                    .Append("    private static async global::System.Threading.Tasks.ValueTask __WebSceneAwaitBinaryCallback")
                    .Append(methodIndex).AppendLine("(")
                    .Append("        global::System.Threading.Tasks.ValueTask<")
                    .Append(returnMapping.CSharpType).AppendLine("> pending,")
                    .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryCallbackCompletion completion)")
                    .AppendLine("    {")
                    .AppendLine("        var result = await pending.ConfigureAwait(false);")
                    .Append("        completion.SetResult<")
                    .Append(returnMapping.CSharpType)
                    .Append(", __WebSceneCallbackResultCodec")
                    .Append(methodIndex).AppendLine(">(in result);")
                    .AppendLine("    }");
            }
            source.AppendLine()
                .Append("    private readonly struct __WebSceneCallbackResultCodec")
                .Append(methodIndex)
                .Append(" : global::WebScene.JavaScript.Interop.IJavaScriptBinaryCallbackResultCodec<")
                .Append(returnMapping.CSharpType).AppendLine(">")
                .AppendLine("    {")
                .AppendLine("        public static uint EncodeResult(")
                .AppendLine("            ref global::WebScene.JavaScript.Interop.JavaScriptBinaryWriter writer,")
                .Append("            in ").Append(returnMapping.CSharpType)
                .AppendLine(" result)")
                .AppendLine("        {");
            var encoded = EmitBinaryWriteValue(
                source,
                generation,
                effectiveReturn,
                "result",
                "            ");
            source.Append("            return ").Append(encoded)
                .AppendLine(";")
                .AppendLine("        }")
                .AppendLine("    }");
        }

        source.Append(binaryActionHelpers)
            .AppendLine()
            .AppendLine("    uint global::WebScene.JavaScript.Interop.IJavaScriptBinaryCallbackTarget.EncodeSynchronousResult(")
            .AppendLine("        uint methodId,")
            .AppendLine("        ref global::WebScene.JavaScript.Interop.JavaScriptBinaryWriter writer,")
            .AppendLine("        global::System.Threading.CancellationToken cancellationToken)")
            .AppendLine("    {")
            .AppendLine("        switch (methodId)")
            .AppendLine("        {");
        for (var methodIndex = 0; methodIndex < methods.Count; methodIndex++)
        {
            var method = methods[methodIndex];
            var declaredReturn = method.Method.GetProperty("returns");
            var effectiveReturn = Kind(declaredReturn) == "promise"
                ? declaredReturn.GetProperty("result")
                : declaredReturn;
            if (AdapterReturnKind(method.Method) != "Synchronous"
                || method.Method.GetProperty("parameters").GetArrayLength() != 0
                || !CanEmitBinaryType(generation, effectiveReturn))
            {
                continue;
            }
            var result = generation.NextLocal("synchronousResult");
            source.Append("            case ").Append(methodIndex)
                .AppendLine("U:")
                .AppendLine("            {")
                .Append("                var ").Append(result)
                .Append(" = ").Append(method.Name)
                .AppendLine("(cancellationToken);")
                .Append("                return __WebSceneCallbackResultCodec")
                .Append(methodIndex).Append(".EncodeResult(ref writer, in ")
                .Append(result).AppendLine(");")
                .AppendLine("            }");
        }
        source.AppendLine("            default:")
            .AppendLine("                throw new global::System.NotSupportedException($\"Callback method {methodId} has no precomputed synchronous result.\");")
            .AppendLine("        }")
            .AppendLine("    }");
    }

    private static string EmitAdapterBinaryArgument(
        StringBuilder source,
        StringBuilder binaryActionHelpers,
        ref int binaryActionHelperIndex,
        GenerationContext generation,
        Adapter adapter,
        AdapterMethod method,
        JsonElement parameter,
        int parameterIndex,
        ISet<string> erasedTypeParameters,
        string indent)
    {
        var type = parameter.GetProperty("type");
        var optional = parameter.GetProperty("optional").GetBoolean();
        var valueType = OptionalParameterValueType(type, optional);
        var mapping = MapAdapterParameterType(
            generation,
            type,
            optional,
            $"{adapter.Name}.{method.Name} binary dispatch",
            erasedTypeParameters);
        if (!optional)
        {
            var value = generation.NextLocal("callbackArgument");
            source.Append(indent).Append("var ").Append(value)
                .Append(" = arguments.GetArrayItem(")
                .Append(parameterIndex).AppendLine(");");
            return EmitAdapterBinaryDecodedValue(
                source,
                binaryActionHelpers,
                ref binaryActionHelperIndex,
                generation,
                valueType,
                mapping,
                value,
                "invoker",
                indent);
        }

        var optionalValue = generation.NextLocal("callbackOptional");
        var valueLocal = generation.NextLocal("callbackArgument");
        source.Append(indent)
            .Append("global::WebScene.JavaScript.Interop.JavaScriptOptional<")
            .Append(mapping.CSharpType).Append("> ").Append(optionalValue)
            .AppendLine(" = default;")
            .Append(indent).Append("if (").Append(parameterIndex)
            .AppendLine(" < arguments.Count)")
            .Append(indent).AppendLine("{")
            .Append(indent).Append("    var ").Append(valueLocal)
            .Append(" = arguments.GetArrayItem(").Append(parameterIndex)
            .AppendLine(");")
            .Append(indent).Append("    if (").Append(valueLocal)
            .AppendLine(".Kind != global::WebScene.JavaScript.Interop.JavaScriptBinaryValueKind.Undefined)")
            .Append(indent).AppendLine("    {");
        if (mapping.IsBinding || mapping.IsCallbackWrapper
            || IsCallbackType(type))
        {
            source.Append(indent).Append("        if (").Append(valueLocal)
                .AppendLine(".Kind == global::WebScene.JavaScript.Interop.JavaScriptBinaryValueKind.Null)")
                .Append(indent).AppendLine("        {")
                .Append(indent).Append("            ").Append(optionalValue)
                .Append(" = new global::WebScene.JavaScript.Interop.JavaScriptOptional<")
                .Append(mapping.CSharpType).AppendLine(">(default);")
                .Append(indent).AppendLine("        }")
                .Append(indent).AppendLine("        else")
                .Append(indent).AppendLine("        {");
            var decoded = EmitAdapterBinaryDecodedValue(
                source,
                binaryActionHelpers,
                ref binaryActionHelperIndex,
                generation,
                valueType,
                mapping,
                valueLocal,
                "invoker",
                indent + "            ");
            source.Append(indent).Append("            ").Append(optionalValue)
                .Append(" = new global::WebScene.JavaScript.Interop.JavaScriptOptional<")
                .Append(mapping.CSharpType).Append(">(").Append(decoded)
                .AppendLine(");")
                .Append(indent).AppendLine("        }");
        }
        else
        {
            var decoded = EmitAdapterBinaryDecodedValue(
                source,
                binaryActionHelpers,
                ref binaryActionHelperIndex,
                generation,
                valueType,
                mapping,
                valueLocal,
                "invoker",
                indent + "        ");
            source.Append(indent).Append("        ").Append(optionalValue)
                .Append(" = new global::WebScene.JavaScript.Interop.JavaScriptOptional<")
                .Append(mapping.CSharpType).Append(">(").Append(decoded)
                .AppendLine(");");
        }
        source.Append(indent).AppendLine("    }")
            .Append(indent).AppendLine("}");
        return optionalValue;
    }

    private static string EmitAdapterBinaryDecodedValue(
        StringBuilder source,
        StringBuilder binaryActionHelpers,
        ref int binaryActionHelperIndex,
        GenerationContext generation,
        JsonElement type,
        TypeMapping mapping,
        string valueExpression,
        string invokerExpression,
        string indent)
    {
        if (IsCallbackType(type))
        {
            var function = generation.NextLocal("callbackFunction");
            source.Append(indent).Append("var ").Append(function)
                .Append(" = new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(")
                .Append(invokerExpression).Append(", ")
                .Append(valueExpression).AppendLine(".GetHandle());");
            if (!mapping.IsCallbackWrapper)
            {
                return function;
            }
            var wrapper = generation.NextLocal("callbackWrapper");
            var binaryActionHelper = "__WebSceneInvokeBinaryAction"
                + binaryActionHelperIndex++;
            EmitBinaryCallbackWrapperHelper(
                binaryActionHelpers,
                generation,
                type,
                binaryActionHelper);
            source.Append(indent).Append("var ").Append(wrapper)
                .Append(" = new ").Append(mapping.NonNullableCSharpType)
                .Append('(').Append(invokerExpression).Append(", ")
                .Append(function).Append(", ")
                .Append(binaryActionHelper).AppendLine(");");
            return wrapper;
        }
        if (mapping.IsBinding)
        {
            var binding = generation.NextLocal("callbackBinding");
            source.Append(indent).Append("var ").Append(binding)
                .Append(" = new ").Append(mapping.NonNullableCSharpType)
                .Append('(').Append(invokerExpression).Append(", ")
                .Append(valueExpression).AppendLine(".GetHandle());");
            return binding;
        }
        if (!CanEmitBinaryType(generation, type))
        {
            source.Append(indent)
                .AppendLine("throw new global::System.NotSupportedException(\"This callback argument type has no generated binary codec.\");");
            return "default!";
        }
        return EmitBinaryReadValue(
            source,
            generation,
            type,
            valueExpression,
            invokerExpression,
            indent);
    }

    private static void EmitBinaryCallbackWrapperHelper(
        StringBuilder source,
        GenerationContext generation,
        JsonElement type,
        string helperName)
    {
        var callback = Kind(type) == "callback"
            ? type
            : FlattenUnionTypes(type).First(
                static candidate => Kind(candidate) == "callback");
        var signature = callback.GetProperty("signatures")[0];
        var parameters = signature.GetProperty("parameters")
            .EnumerateArray()
            .ToArray();
        var mappings = parameters
            .Select(parameter => MapType(
                generation,
                parameter.GetProperty("type"),
                optional: parameter.TryGetProperty(
                              "optional",
                              out var optional)
                          && optional.GetBoolean(),
                helperName))
            .ToArray();
        var argumentsType = parameters.Length switch
        {
            0 => "global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid",
            1 => mappings[0].CSharpType,
            _ => TupleType(
                mappings.Select(static mapping => mapping.CSharpType)
                    .ToArray())
        };
        var codecName = helperName + "Codec";

        source.AppendLine()
            .Append("    private static global::System.Threading.Tasks.ValueTask ")
            .Append(helperName).AppendLine("(")
            .AppendLine("        global::WebScene.JavaScript.Interop.IJavaScriptBinaryBidirectionalInvoker invoker,")
            .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptObjectReference function,");
        for (var index = 0; index < parameters.Length; index++)
        {
            source.Append("        ").Append(mappings[index].CSharpType)
                .Append(" argument").Append(index + 1).AppendLine(",");
        }
        source.AppendLine("        global::System.Threading.CancellationToken cancellationToken)")
            .AppendLine("    {")
            .Append("        return invoker.InvokeBinaryFunctionVoidAsync<")
            .Append(argumentsType).Append(", ").Append(codecName)
            .AppendLine(">(")
            .AppendLine("            function,");
        if (parameters.Length == 0)
        {
            source.AppendLine("            new global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid(),");
        }
        else if (parameters.Length == 1)
        {
            source.AppendLine("            argument1,");
        }
        else
        {
            source.Append("            (")
                .Append(string.Join(
                    ", ",
                    Enumerable.Range(1, parameters.Length)
                        .Select(static index => "argument" + index)))
                .AppendLine("),");
        }
        source.AppendLine("            cancellationToken);")
            .AppendLine("    }")
            .AppendLine()
            .Append("    private readonly struct ").Append(codecName)
            .Append(" : global::WebScene.JavaScript.Interop.IJavaScriptBinaryCodec<")
            .Append(argumentsType)
            .AppendLine(", global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid>")
            .AppendLine("    {")
            .AppendLine("        public static uint EncodeArguments(")
            .AppendLine("            ref global::WebScene.JavaScript.Interop.JavaScriptBinaryWriter writer,")
            .Append("            in ").Append(argumentsType)
            .AppendLine(" arguments)")
            .AppendLine("        {")
            .Append("            var root = writer.BeginArray(")
            .Append(parameters.Length).AppendLine(");");
        for (var index = 0; index < parameters.Length; index++)
        {
            var expression = parameters.Length == 1
                ? "arguments"
                : "arguments.Item" + (index + 1);
            var optional = parameters[index].TryGetProperty(
                               "optional",
                               out var optionalValue)
                           && optionalValue.GetBoolean();
            var encoded = EmitBinaryWriteCallbackArgument(
                source,
                generation,
                parameters[index].GetProperty("type"),
                expression,
                optional,
                "            ");
            source.Append("            writer.SetArrayItem(root, ")
                .Append(index).Append(", ").Append(encoded)
                .AppendLine(");");
        }
        source.AppendLine("            return root;")
            .AppendLine("        }")
            .AppendLine()
            .AppendLine("        public static global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid DecodeResult(")
            .AppendLine("            global::WebScene.JavaScript.Interop.JavaScriptBinaryValue value,")
            .AppendLine("            global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker)")
            .AppendLine("            => new();")
            .AppendLine("    }");
    }

    private static string EmitBinaryWriteCallbackArgument(
        StringBuilder source,
        GenerationContext generation,
        JsonElement type,
        string valueExpression,
        bool optional,
        string indent)
    {
        if (!optional)
        {
            return EmitBinaryWriteValue(
                source,
                generation,
                type,
                valueExpression,
                indent);
        }

        var result = generation.NextLocal("binaryCallbackValue");
        source.Append(indent).Append("uint ").Append(result)
            .AppendLine(";")
            .Append(indent).Append("if (").Append(valueExpression)
            .AppendLine(" is null)")
            .Append(indent).AppendLine("{")
            .Append(indent).Append("    ").Append(result)
            .AppendLine(" = writer.WriteNull();")
            .Append(indent).AppendLine("}")
            .Append(indent).AppendLine("else")
            .Append(indent).AppendLine("{");
        var payloadType = Kind(type) == "union"
                          && TryGetBinaryUnionPayloadType(
                              type,
                              out var unionPayload)
            ? unionPayload
            : type;
        var requiredMapping = MapType(
            generation,
            payloadType,
            optional: false,
            "binary callback argument");
        var concreteExpression = IsValueType(requiredMapping.CSharpType)
            ? valueExpression + ".Value"
            : valueExpression;
        var encoded = EmitBinaryWriteValue(
            source,
            generation,
            payloadType,
            concreteExpression,
            indent + "    ");
        source.Append(indent).Append("    ").Append(result)
            .Append(" = ").Append(encoded).AppendLine(";")
            .Append(indent).AppendLine("}");
        return result;
    }

    private static bool IsCallbackType(JsonElement type)
    {
        if (Kind(type) == "callback") return true;
        return Kind(type) == "union"
               && FlattenUnionTypes(type).Any(
                   static candidate => Kind(candidate) == "callback");
    }

    private static bool CanEmitBinaryAdapterMethod(
        GenerationContext generation,
        Adapter adapter,
        AdapterMethod method)
    {
        static bool ContainsErasedTypeParameter(
            JsonElement type,
            ISet<string> erased)
            => erased.Count != 0
               && ContainsTypeParameter(type, erased);

        var erased = AdapterMethodTypeParameters(method.Method);
        foreach (var parameter in method.Method.GetProperty("parameters")
                     .EnumerateArray())
        {
            var type = parameter.GetProperty("type");
            if (ContainsErasedTypeParameter(type, erased)
                || !IsCallbackType(type)
                && !CanEmitBinaryType(generation, type))
            {
                return false;
            }
        }
        var declaredReturn = method.Method.GetProperty("returns");
        if (IsVoidLikeReturn(declaredReturn))
        {
            return true;
        }
        var effectiveReturn = Kind(declaredReturn) == "promise"
            ? declaredReturn.GetProperty("result")
            : declaredReturn;
        return !ContainsErasedTypeParameter(effectiveReturn, erased)
               && CanEmitBinaryType(generation, effectiveReturn);
    }

    private static TypeMapping MapAdapterType(
        GenerationContext generation,
        JsonElement type,
        bool optional,
        string member,
        ISet<string>? erasedTypeParameters = null)
    {
        if (erasedTypeParameters is { Count: > 0 }
            && ContainsTypeParameter(type, erasedTypeParameters))
        {
            var callback = Kind(type) == "callback"
                || Kind(type) == "union"
                && type.GetProperty("types").EnumerateArray().Any(
                    static candidate => Kind(candidate) == "callback");
            var erased = callback
                ? "global::WebScene.JavaScript.Interop.JavaScriptFunctionReference"
                : "global::System.Text.Json.JsonElement";
            return new TypeMapping(optional ? Nullable(erased) : erased, false);
        }
        if (Kind(type) == "callback")
        {
            var wrapper = ResolveCallbackWrapper(generation, type, member);
            return new TypeMapping(
                optional ? Nullable(wrapper) : wrapper,
                false,
                IsCallbackWrapper: wrapper
                    != "global::WebScene.JavaScript.Interop.JavaScriptFunctionReference");
        }
        if (Kind(type) == "union")
        {
            var nonNull = type.GetProperty("types").EnumerateArray()
                .Where(static candidate => Kind(candidate) is not ("null" or "undefined"))
                .ToArray();
            if (nonNull.Length == 1 && Kind(nonNull[0]) == "callback")
            {
                var wrapper = ResolveCallbackWrapper(
                    generation,
                    nonNull[0],
                    member);
                var nullable = type.GetProperty("types").EnumerateArray().Any(
                    static candidate => Kind(candidate) is "null" or "undefined");
                return new TypeMapping(
                    optional || nullable ? Nullable(wrapper) : wrapper,
                    false,
                    IsCallbackWrapper: wrapper
                        != "global::WebScene.JavaScript.Interop.JavaScriptFunctionReference");
            }
        }
        return MapType(generation, type, optional, member);
    }

    private static TypeMapping MapAdapterParameterType(
        GenerationContext generation,
        JsonElement type,
        bool optional,
        string member,
        ISet<string>? erasedTypeParameters)
    {
        if (!optional)
        {
            return MapAdapterType(
                generation,
                type,
                optional: false,
                member,
                erasedTypeParameters);
        }

        var callback = Kind(type) == "callback";
        if (Kind(type) == "union")
        {
            var concrete = type.GetProperty("types").EnumerateArray()
                .Where(static candidate => Kind(candidate) is not ("null" or "undefined"))
                .ToArray();
            callback = concrete.Length == 1 && Kind(concrete[0]) == "callback";
        }
        if (callback
            || erasedTypeParameters is { Count: > 0 }
            && ContainsTypeParameter(type, erasedTypeParameters))
        {
            return MapAdapterType(
                generation,
                type,
                optional: false,
                member,
                erasedTypeParameters);
        }
        return MapOptionalParameterType(generation, type, optional: true, member);
    }

    private static HashSet<string> AdapterMethodTypeParameters(JsonElement method)
        => method.TryGetProperty("typeParameters", out var parameters)
            ? new HashSet<string>(
                parameters.EnumerateArray()
                    .Select(static parameter => parameter.GetString()!),
                StringComparer.Ordinal)
            : new HashSet<string>(StringComparer.Ordinal);

    private static bool ContainsTypeParameter(
        JsonElement value,
        ISet<string> names)
    {
        if (value.ValueKind == JsonValueKind.Object)
        {
            if (value.TryGetProperty("kind", out var kind)
                && kind.GetString() == "typeParameter"
                && value.TryGetProperty("name", out var name)
                && names.Contains(name.GetString()!))
            {
                return true;
            }
            return value.EnumerateObject().Any(
                property => ContainsTypeParameter(property.Value, names));
        }
        return value.ValueKind == JsonValueKind.Array
               && value.EnumerateArray().Any(item => ContainsTypeParameter(item, names));
    }

    private static string ResolveCallbackWrapper(
        GenerationContext generation,
        JsonElement callback,
        string member)
    {
        var signatures = callback.GetProperty("signatures");
        if (signatures.GetArrayLength() != 1)
        {
            return "global::WebScene.JavaScript.Interop.JavaScriptFunctionReference";
        }
        var signature = signatures[0];
        if (Kind(signature.GetProperty("returns")) != "void")
        {
            return "global::WebScene.JavaScript.Interop.JavaScriptFunctionReference";
        }
        var parameters = signature.GetProperty("parameters")
            .EnumerateArray()
            .Select(parameter => MapType(
                generation,
                parameter.GetProperty("type"),
                optional: parameter.TryGetProperty("optional", out var optional)
                          && optional.GetBoolean(),
                member).CSharpType)
            .ToArray();
        if (parameters.Length == 0)
        {
            return "global::WebScene.JavaScript.Interop.JavaScriptAction";
        }
        if (parameters.Length <= 4)
        {
            return "global::WebScene.JavaScript.Interop.JavaScriptAction<"
                   + string.Join(", ", parameters)
                   + ">";
        }
        return "global::WebScene.JavaScript.Interop.JavaScriptTupleAction<"
               + TupleType(parameters)
               + ">";
    }

    private static string AdapterReturnKind(JsonElement method)
    {
        var returns = method.GetProperty("returns");
        if (IsPromiseLikeReturn(returns))
        {
            return "Promise";
        }
        return IsVoidLikeReturn(returns) ? "Void" : "Synchronous";
    }

    private static string SuggestedAdapterMethodName(
        string sourceName,
        JsonElement method)
        => PascalCase(sourceName)
           + (AdapterReturnKind(method) == "Synchronous" ? string.Empty : "Async");

    private static string GenerateModel(
        GenerationContext generation,
        Model model)
    {
        var kind = Kind(model.Type);
        if (kind == "enum")
        {
            return GenerateEnumModel(generation.Namespace, model);
        }
        if (kind == "typeAlias"
            && model.Type.TryGetProperty("aliasTarget", out var aliasTarget))
        {
            if (IsStringLiteralUnion(aliasTarget))
            {
                return GenerateStringUnionModel(generation.Namespace, model, aliasTarget);
            }
            return GenerateAliasModel(generation, model, aliasTarget);
        }

        var objectType = model.Type;
        var typeParameters = model.Type.TryGetProperty("typeParameters", out var parameters)
            ? parameters.EnumerateArray().Select(static item => item.GetString()!).ToArray()
            : [];
        var genericSuffix = typeParameters.Length == 0
            ? string.Empty
            : "<" + string.Join(", ", typeParameters.Select(EscapeIdentifier)) + ">";
        var source = new StringBuilder(
            """
            // <auto-generated />
            #nullable enable

            """);
        source.Append("namespace ").Append(generation.Namespace).AppendLine(";")
            .AppendLine();
        EmitObjectModel(
            source,
            generation,
            model.Name + genericSuffix,
            model.Name,
            objectType,
            typeParameters);
        return source.ToString();
    }

    private static string GenerateAliasModel(
        GenerationContext generation,
        Model model,
        JsonElement aliasTarget)
    {
        var typeParameters = model.Type.TryGetProperty("typeParameters", out var parameters)
            ? parameters.EnumerateArray().Select(static item => item.GetString()!).ToArray()
            : [];
        var escapedTypeParameters = typeParameters.Select(EscapeIdentifier).ToArray();
        var genericSuffix = escapedTypeParameters.Length == 0
            ? string.Empty
            : "<" + string.Join(", ", escapedTypeParameters) + ">";
        var modelType = model.Name + genericSuffix;
        if (Kind(aliasTarget) == "inlineObject")
        {
            var source = new StringBuilder(
                """
                // <auto-generated />
                #nullable enable

                """);
            source.Append("namespace ").Append(generation.Namespace).AppendLine(";")
                .AppendLine();
            EmitObjectModel(
                source,
                generation,
                modelType,
                model.Name,
                aliasTarget,
                typeParameters);
            return source.ToString();
        }
        var underlying = MapType(
            generation,
            aliasTarget,
            optional: false,
            $"alias '{model.Source}'").CSharpType;
        var sourceBuilder = new StringBuilder(
            """
            // <auto-generated />
            #nullable enable

            """);
        sourceBuilder.Append("namespace ").Append(generation.Namespace).AppendLine(";")
            .AppendLine()
            .Append("[global::System.Text.Json.Serialization.JsonConverter(typeof(")
            .Append(model.Name).Append(escapedTypeParameters.Length == 0
                ? "JsonConverter"
                : "JsonConverterFactory").AppendLine("))]")
            .Append("public readonly record struct ").Append(modelType)
            .Append('(').Append(underlying).AppendLine(" Value)")
            .AppendLine("{")
            .Append("    public static implicit operator ").Append(underlying)
            .Append('(').Append(modelType).AppendLine(" value) => value.Value;")
            .Append("    public static implicit operator ").Append(modelType)
            .Append('(').Append(underlying).AppendLine(" value) => new(value);")
            .AppendLine("    public override string? ToString() => global::System.Convert.ToString(Value, global::System.Globalization.CultureInfo.InvariantCulture);")
            .AppendLine("}");
        if (escapedTypeParameters.Length > 0)
        {
            var openModel = OpenGenericType(model.Name, escapedTypeParameters.Length);
            var openConverter = OpenGenericType(
                model.Name + "JsonConverter",
                escapedTypeParameters.Length);
            sourceBuilder.AppendLine()
                .Append("public sealed class ").Append(model.Name)
                .AppendLine("JsonConverterFactory : global::System.Text.Json.Serialization.JsonConverterFactory")
                .AppendLine("{")
                .AppendLine("    public override bool CanConvert(global::System.Type typeToConvert)")
                .Append("        => typeToConvert.IsGenericType && typeToConvert.GetGenericTypeDefinition() == typeof(")
                .Append(openModel).AppendLine(");")
                .AppendLine()
                .AppendLine("    public override global::System.Text.Json.Serialization.JsonConverter CreateConverter(")
                .AppendLine("        global::System.Type typeToConvert,")
                .AppendLine("        global::System.Text.Json.JsonSerializerOptions options)")
                .Append("        => (global::System.Text.Json.Serialization.JsonConverter)global::System.Activator.CreateInstance(typeof(")
                .Append(openConverter)
                .AppendLine(").MakeGenericType(typeToConvert.GetGenericArguments()))!;")
                .AppendLine("}");
        }
        sourceBuilder.AppendLine()
            .Append("public sealed class ").Append(model.Name)
            .Append("JsonConverter").Append(genericSuffix)
            .Append(" : global::System.Text.Json.Serialization.JsonConverter<")
            .Append(modelType).AppendLine(">")
            .AppendLine("{")
            .Append("    public override ").Append(modelType)
            .AppendLine(" Read(ref global::System.Text.Json.Utf8JsonReader reader, global::System.Type typeToConvert, global::System.Text.Json.JsonSerializerOptions options)")
            .Append("        => new(global::System.Text.Json.JsonSerializer.Deserialize<")
            .Append(underlying).AppendLine(">(ref reader, options)!);")
            .AppendLine()
            .Append("    public override void Write(global::System.Text.Json.Utf8JsonWriter writer, ")
            .Append(modelType)
            .AppendLine(" value, global::System.Text.Json.JsonSerializerOptions options)")
            .AppendLine("        => global::System.Text.Json.JsonSerializer.Serialize(writer, value.Value, options);")
            .AppendLine("}");
        return sourceBuilder.ToString();
    }

    private static string OpenGenericType(string name, int arity)
        => name + "<" + new string(',', arity - 1) + ">";

    private static void EmitObjectModel(
        StringBuilder source,
        GenerationContext generation,
        string declarationName,
        string generatedName,
        JsonElement objectType,
        IReadOnlyList<string> typeParameters)
    {
        var properties = objectType.GetProperty("properties");
        var usedPropertyNames = new HashSet<string>(StringComparer.Ordinal);
        var hasIndexSignatures =
            objectType.TryGetProperty("indexSignatures", out var indexes)
            && indexes.GetArrayLength() > 0;
        ObjectModelIndex? mappedIndex = null;
        if (hasIndexSignatures)
        {
            usedPropertyNames.Add("AdditionalProperties");
        }
        var mappedProperties = new List<ObjectModelProperty>();
        var previousTypeParameterMappings =
            generation.ActiveTypeParameterMappings;
        generation.ActiveTypeParameterMappings = typeParameters.ToDictionary(
            static parameter => parameter,
            static parameter => new TypeMapping(
                EscapeIdentifier(parameter),
                IsBinding: false,
                WireCSharpType: EscapeIdentifier(parameter + "Wire"),
                FromWireTemplate:
                    "convert"
                    + PascalCase(parameter)
                    + "(__VALUE__)"),
            StringComparer.Ordinal);
        try
        {
            foreach (var property in properties.EnumerateArray())
            {
                var javascriptName = property.GetProperty("name").GetString()!;
                var propertyName = PascalCase(javascriptName);
                if (propertyName.Length == 0)
                {
                    propertyName = "Value";
                }
                if (string.Equals(propertyName, generatedName, StringComparison.Ordinal))
                {
                    propertyName += "Value";
                }
                if (propertyName is "Equals" or "GetHashCode" or "GetType"
                    or "MemberwiseClone" or "ReferenceEquals" or "ToString")
                {
                    propertyName += "Value";
                }
                var preferredPropertyName = propertyName;
                for (var suffix = 2; !usedPropertyNames.Add(propertyName); suffix++)
                {
                    propertyName = preferredPropertyName + suffix;
                }
                var optional = property.GetProperty("optional").GetBoolean();
                var propertyType = property.GetProperty("type");
                var mapping = MapOptionalParameterType(
                    generation,
                    propertyType,
                    optional,
                    $"{generatedName}.{propertyName}");
                if (optional)
                {
                    mapping = MapOptionalModelProperty(mapping);
                }
                mappedProperties.Add(new ObjectModelProperty(
                    javascriptName,
                    propertyName,
                    optional,
                    mapping,
                    propertyType.Clone()));
            }
            if (hasIndexSignatures)
            {
                var signatures = indexes.EnumerateArray().ToArray();
                var signature = signatures.FirstOrDefault(candidate =>
                    string.Equals(
                        candidate.GetProperty("key").GetString(),
                        "string",
                        StringComparison.Ordinal));
                if (signature.ValueKind == JsonValueKind.Undefined)
                {
                    signature = signatures[0];
                }
                var numericKey = string.Equals(
                    signature.GetProperty("key").GetString(),
                    "number",
                    StringComparison.Ordinal);
                mappedIndex = new ObjectModelIndex(
                    numericKey ? "double" : "string",
                    numericKey,
                    MapType(
                        generation,
                        signature.GetProperty("value"),
                        optional: false,
                        $"{generatedName} index value"));
            }
        }
        finally
        {
            generation.ActiveTypeParameterMappings =
                previousTypeParameterMappings;
        }

        var genericSuffix = declarationName.Substring(generatedName.Length);
        var wireTypeParameters = typeParameters
            .Select(static parameter => EscapeIdentifier(parameter + "Wire"))
            .ToArray();
        var wireGenericSuffix = wireTypeParameters.Length == 0
            ? string.Empty
            : "<" + string.Join(", ", wireTypeParameters) + ">";
        var wireDeclarationName =
            generatedName + "Wire" + wireGenericSuffix;
        if (mappedIndex is not null)
        {
            source.Append("[global::System.Text.Json.Serialization.JsonConverter(typeof(")
                .Append(generatedName)
                .Append(typeParameters.Count == 0
                    ? "JsonConverter"
                    : "JsonConverterFactory")
                .AppendLine("))]");
        }
        source.Append("public sealed record ").Append(declarationName)
            .AppendLine()
            .AppendLine("{");
        foreach (var property in mappedProperties)
        {
            source.Append("    [global::System.Text.Json.Serialization.JsonPropertyName(")
                .Append(Literal(property.JavaScriptName)).AppendLine(")]");
            if (property.Optional)
            {
                source.AppendLine("    [global::System.Text.Json.Serialization.JsonIgnore(Condition = global::System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]");
            }
            source
                .Append("    public ");
            if (!property.Optional && !IsValueType(property.Mapping.CSharpType))
            {
                source.Append("required ");
            }
            source.Append(property.Mapping.CSharpType).Append(' ')
                .Append(EscapeIdentifier(property.CSharpName))
                .Append(" { get; init; }");
            if (property.Optional || IsValueType(property.Mapping.CSharpType))
            {
                source.Append(" = default;");
            }
            source.AppendLine().AppendLine();
        }
        if (mappedIndex is { } typedIndex)
        {
            source.Append("    public global::System.Collections.Generic.IReadOnlyDictionary<")
                .Append(typedIndex.KeyType).Append(", ")
                .Append(typedIndex.ValueMapping.CSharpType)
                .AppendLine(">? AdditionalProperties { get; init; }")
                .AppendLine();
        }
        source.Append("    internal static ").Append(declarationName)
            .Append(" FromWire");
        if (wireTypeParameters.Length > 0)
        {
            source.Append(wireGenericSuffix);
        }
        source.Append('(').Append(wireDeclarationName)
            .Append(" wire, global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker");
        for (var index = 0; index < typeParameters.Count; index++)
        {
            source.Append(", global::System.Func<")
                .Append(wireTypeParameters[index]).Append(", ")
                .Append(EscapeIdentifier(typeParameters[index]))
                .Append("> convert")
                .Append(PascalCase(typeParameters[index]));
        }
        source.AppendLine(")")
            .AppendLine("        => new()")
            .AppendLine("        {");
        foreach (var property in mappedProperties)
        {
            var propertyName = EscapeIdentifier(property.CSharpName);
            var conversion = property.Mapping.ConvertFromWire(
                "wire." + propertyName,
                "invoker");
            if (property.Mapping.RequiresWireConversion)
            {
                conversion =
                    "((global::System.Func<"
                    + property.Mapping.CSharpType
                    + ">)(() => "
                    + conversion
                    + "))()";
            }
            source.Append("            ").Append(propertyName).Append(" = ")
                .Append(conversion)
                .AppendLine(",");
        }
        if (mappedIndex is { } indexMapping)
        {
            var wireValue =
                "global::WebScene.JavaScript.Interop.JavaScriptInteropSerializer.Deserialize<"
                + indexMapping.ValueMapping.EffectiveWireCSharpType
                + ">(pair.Value)";
            var convertedValue = indexMapping.ValueMapping.ConvertFromWire(
                wireValue,
                "invoker");
            if (indexMapping.ValueMapping.RequiresWireConversion)
            {
                convertedValue =
                    "((global::System.Func<"
                    + indexMapping.ValueMapping.CSharpType
                    + ">)(() => "
                    + convertedValue
                    + "))()";
            }
            var convertedKey = indexMapping.NumericKey
                ? "double.Parse(pair.Key, global::System.Globalization.CultureInfo.InvariantCulture)"
                : "pair.Key";
            source.AppendLine("            AdditionalProperties = wire.AdditionalProperties is { } additionalProperties")
                .AppendLine("                ? global::System.Linq.Enumerable.ToDictionary(")
                .AppendLine("                    additionalProperties,")
                .Append("                    pair => ").Append(convertedKey).AppendLine(",")
                .Append("                    pair => ").Append(convertedValue).AppendLine(")")
                .AppendLine("                : null,");
        }
        source.AppendLine("        };");
        if (typeParameters.Count == 0
            && mappedIndex is null
            && mappedProperties.All(property =>
                CanEmitBinaryType(generation, property.Type)))
        {
            EmitObjectModelBinaryCodec(
                source,
                generation,
                declarationName,
                mappedProperties);
        }
        source.AppendLine("}")
            .AppendLine()
            .Append("internal sealed record ").Append(wireDeclarationName)
            .AppendLine()
            .AppendLine("{");
        foreach (var property in mappedProperties)
        {
            var wireType = property.Mapping.EffectiveWireCSharpType;
            source.Append("    [global::System.Text.Json.Serialization.JsonPropertyName(")
                .Append(Literal(property.JavaScriptName)).AppendLine(")]");
            if (property.Optional)
            {
                source.AppendLine("    [global::System.Text.Json.Serialization.JsonIgnore(Condition = global::System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]");
            }
            source
                .Append("    public ");
            if (!property.Optional && !IsValueType(wireType))
            {
                source.Append("required ");
            }
            source.Append(wireType).Append(' ')
                .Append(EscapeIdentifier(property.CSharpName))
                .Append(" { get; init; }");
            if (property.Optional || IsValueType(wireType))
            {
                source.Append(" = default;");
            }
            source.AppendLine().AppendLine();
        }
        if (hasIndexSignatures)
        {
            source.AppendLine("    [global::System.Text.Json.Serialization.JsonExtensionData]")
                .AppendLine("    public global::System.Collections.Generic.IDictionary<string, global::System.Text.Json.JsonElement>? AdditionalProperties { get; init; }")
                .AppendLine();
        }
        source.AppendLine("}");
        if (mappedIndex is { } converterIndex)
        {
            EmitIndexedModelJsonConverter(
                source,
                declarationName,
                generatedName,
                typeParameters,
                mappedProperties,
                converterIndex);
        }
    }

    private static void EmitObjectModelBinaryCodec(
        StringBuilder source,
        GenerationContext generation,
        string declarationName,
        IReadOnlyList<ObjectModelProperty> properties)
    {
        var requiredCount = properties.Count(static property => !property.Optional);
        source.AppendLine()
            .AppendLine("    internal static uint __WebSceneWriteBinary(")
            .AppendLine("        ref global::WebScene.JavaScript.Interop.JavaScriptBinaryWriter writer,")
            .Append("        ").Append(declarationName).AppendLine(" value)")
            .AppendLine("    {")
            .AppendLine("        global::System.ArgumentNullException.ThrowIfNull(value);")
            .Append("        var propertyCount = ").Append(requiredCount)
            .AppendLine(";");
        foreach (var property in properties.Where(static property => property.Optional))
        {
            source.Append("        if (value.")
                .Append(EscapeIdentifier(property.CSharpName))
                .AppendLine(".HasValue) propertyCount++;");
        }
        source.AppendLine("        var result = writer.BeginObject(propertyCount);")
            .AppendLine("        var propertyIndex = 0;");
        foreach (var property in properties)
        {
            var propertyName = EscapeIdentifier(property.CSharpName);
            var valueType = property.Optional
                ? OptionalBinaryPayloadType(property.Type)
                : property.Type;
            if (property.Optional)
            {
                source.Append("        if (value.").Append(propertyName)
                    .AppendLine(".HasValue)")
                    .AppendLine("        {");
            }
            var indent = property.Optional ? "            " : "        ";
            var value = EmitBinaryWriteValue(
                source,
                generation,
                valueType,
                "value." + propertyName + (property.Optional ? ".Value!" : string.Empty),
                indent);
            source.Append(indent).Append("writer.SetObjectProperty(result, propertyIndex++, ")
                .Append(Literal(property.JavaScriptName)).Append("u8, ")
                .Append(value).AppendLine(");");
            if (property.Optional)
            {
                source.AppendLine("        }");
            }
        }
        source.AppendLine("        return result;")
            .AppendLine("    }")
            .AppendLine()
            .Append("    internal static ").Append(declarationName)
            .AppendLine(" __WebSceneReadBinary(")
            .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryValue value,")
            .AppendLine("        global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker)")
            .AppendLine("    {");

        var values = new List<(ObjectModelProperty Property, string Local)>();
        foreach (var property in properties)
        {
            if (!property.Optional)
            {
                var local = EmitBinaryReadValue(
                    source,
                    generation,
                    property.Type,
                    "value.GetRequiredProperty(" + Literal(property.JavaScriptName) + "u8)",
                    "invoker",
                    "        ");
                values.Add((property, local));
                continue;
            }

            var optionalType = property.Mapping.CSharpType;
            var node = generation.NextLocal("binaryProperty");
            var localOptional = generation.NextLocal("binaryOptional");
            source.Append("        ").Append(optionalType).Append(' ')
                .Append(localOptional).AppendLine(";")
                .Append("        if (value.TryGetProperty(")
                .Append(Literal(property.JavaScriptName)).Append("u8, out var ")
                .Append(node).AppendLine(")")
                .Append("            && ").Append(node)
                .AppendLine(".Kind != global::WebScene.JavaScript.Interop.JavaScriptBinaryValueKind.Undefined)")
                .AppendLine("        {");
            var child = EmitBinaryReadValue(
                source,
                generation,
                OptionalBinaryPayloadType(property.Type),
                node,
                "invoker",
                "            ");
            source.Append("            ").Append(localOptional).Append(" = ")
                .Append(child).AppendLine(";")
                .AppendLine("        }")
                .AppendLine("        else")
                .AppendLine("        {")
                .Append("            ").Append(localOptional)
                .AppendLine(" = default;")
                .AppendLine("        }");
            values.Add((property, localOptional));
        }
        source.AppendLine("        return new()")
            .AppendLine("        {");
        foreach (var (property, local) in values)
        {
            source.Append("            ")
                .Append(EscapeIdentifier(property.CSharpName))
                .Append(" = ").Append(local).AppendLine(",");
        }
        source.AppendLine("        };")
            .AppendLine("    }");
    }

    private static JsonElement OptionalBinaryPayloadType(JsonElement type)
    {
        if (Kind(type) != "union")
        {
            return type;
        }
        var candidates = FlattenUnionTypes(type)
            .Where(static candidate => Kind(candidate) != "undefined")
            .ToArray();
        if (candidates.Length == 1)
        {
            return candidates[0];
        }
        return type;
    }

    private static void EmitIndexedModelJsonConverter(
        StringBuilder source,
        string declarationName,
        string generatedName,
        IReadOnlyList<string> typeParameters,
        IReadOnlyList<ObjectModelProperty> properties,
        ObjectModelIndex index)
    {
        var escapedTypeParameters = typeParameters.Select(EscapeIdentifier).ToArray();
        var genericSuffix = escapedTypeParameters.Length == 0
            ? string.Empty
            : "<" + string.Join(", ", escapedTypeParameters) + ">";
        if (escapedTypeParameters.Length > 0)
        {
            source.AppendLine()
                .Append("public sealed class ").Append(generatedName)
                .AppendLine("JsonConverterFactory : global::System.Text.Json.Serialization.JsonConverterFactory")
                .AppendLine("{")
                .AppendLine("    public override bool CanConvert(global::System.Type typeToConvert)")
                .Append("        => typeToConvert.IsGenericType && typeToConvert.GetGenericTypeDefinition() == typeof(")
                .Append(OpenGenericType(generatedName, escapedTypeParameters.Length))
                .AppendLine(");")
                .AppendLine()
                .AppendLine("    public override global::System.Text.Json.Serialization.JsonConverter CreateConverter(")
                .AppendLine("        global::System.Type typeToConvert,")
                .AppendLine("        global::System.Text.Json.JsonSerializerOptions options)")
                .Append("        => (global::System.Text.Json.Serialization.JsonConverter)global::System.Activator.CreateInstance(typeof(")
                .Append(OpenGenericType(
                    generatedName + "JsonConverter",
                    escapedTypeParameters.Length))
                .AppendLine(").MakeGenericType(typeToConvert.GetGenericArguments()))!;")
                .AppendLine("}");
        }
        source.AppendLine()
            .Append("public sealed class ").Append(generatedName)
            .Append("JsonConverter").Append(genericSuffix)
            .Append(" : global::System.Text.Json.Serialization.JsonConverter<")
            .Append(declarationName).AppendLine(">")
            .AppendLine("{")
            .Append("    public override ").Append(declarationName)
            .AppendLine(" Read(ref global::System.Text.Json.Utf8JsonReader reader, global::System.Type typeToConvert, global::System.Text.Json.JsonSerializerOptions options)")
            .AppendLine("        => throw new global::System.NotSupportedException(\"Indexed JavaScript models are materialized through their generated native-engine binding.\");")
            .AppendLine()
            .Append("    public override void Write(global::System.Text.Json.Utf8JsonWriter writer, ")
            .Append(declarationName)
            .AppendLine(" value, global::System.Text.Json.JsonSerializerOptions options)")
            .AppendLine("    {")
            .AppendLine("        writer.WriteStartObject();");
        foreach (var property in properties)
        {
            var propertyAccess = "value." + EscapeIdentifier(property.CSharpName);
            if (property.Optional)
            {
                source.Append("        if (").Append(propertyAccess).AppendLine(".HasValue)")
                    .AppendLine("        {")
                    .Append("            writer.WritePropertyName(")
                    .Append(Literal(property.JavaScriptName)).AppendLine(");")
                    .Append("            global::System.Text.Json.JsonSerializer.Serialize(writer, ")
                    .Append(propertyAccess).AppendLine(".Value, options);")
                    .AppendLine("        }");
            }
            else
            {
                source.Append("        writer.WritePropertyName(")
                    .Append(Literal(property.JavaScriptName)).AppendLine(");")
                    .Append("        global::System.Text.Json.JsonSerializer.Serialize(writer, ")
                    .Append(propertyAccess).AppendLine(", options);");
            }
        }
        source.AppendLine("        if (value.AdditionalProperties is { } additionalProperties)")
            .AppendLine("        {")
            .AppendLine("            foreach (var pair in additionalProperties)")
            .AppendLine("            {");
        if (!index.NumericKey && properties.Count > 0)
        {
            source.Append("                if (");
            for (var propertyIndex = 0; propertyIndex < properties.Count; propertyIndex++)
            {
                if (propertyIndex > 0)
                {
                    source.Append(" || ");
                }
                source.Append("pair.Key == ")
                    .Append(Literal(properties[propertyIndex].JavaScriptName));
            }
            source.AppendLine(")")
                .AppendLine("                {")
                .AppendLine("                    continue;")
                .AppendLine("                }");
        }
        source.Append("                writer.WritePropertyName(")
            .Append(index.NumericKey
                ? "pair.Key.ToString(global::System.Globalization.CultureInfo.InvariantCulture)"
                : "pair.Key")
            .AppendLine(");")
            .AppendLine("                global::System.Text.Json.JsonSerializer.Serialize(writer, pair.Value, options);")
            .AppendLine("            }")
            .AppendLine("        }")
            .AppendLine("        writer.WriteEndObject();")
            .AppendLine("    }")
            .AppendLine("}");
    }

    private static bool TryGetInlineObject(
        JsonElement type,
        out JsonElement inlineObject,
        out bool nullable)
    {
        if (Kind(type) == "inlineObject")
        {
            inlineObject = type;
            nullable = false;
            return true;
        }
        if (Kind(type) == "union")
        {
            var candidates = type.GetProperty("types").EnumerateArray().ToArray();
            var concrete = candidates
                .Where(candidate => Kind(candidate) is not ("undefined" or "null"))
                .ToArray();
            if (concrete.Length == 1 && Kind(concrete[0]) == "inlineObject")
            {
                inlineObject = concrete[0];
                nullable = concrete.Length != candidates.Length;
                return true;
            }
        }
        inlineObject = default;
        nullable = false;
        return false;
    }

    private static string GenerateEnumModel(string namespaceName, Model model)
    {
        var members = model.Type.TryGetProperty("enumMembers", out var enumMembers)
            ? enumMembers.EnumerateArray().ToArray()
            : [];
        var isString = members.Any(member =>
            member.TryGetProperty("value", out var value)
            && value.ValueKind == JsonValueKind.String);
        if (isString)
        {
            var syntheticUnion = JsonDocument.Parse(
                "{\"kind\":\"union\",\"types\":["
                + string.Join(",", members.Select(member =>
                    "{\"kind\":\"literal\",\"value\":"
                    + JsonSerializer.Serialize(member.GetProperty("value").GetString())
                    + "}"))
                + "]}");
            using (syntheticUnion)
            {
                return GenerateStringUnionModel(
                    namespaceName,
                    model,
                    syntheticUnion.RootElement);
            }
        }

        var source = new StringBuilder(
            """
            // <auto-generated />
            #nullable enable

            """);
        source.Append("namespace ").Append(namespaceName).AppendLine(";")
            .AppendLine()
            .Append("public enum ").Append(model.Name).AppendLine()
            .AppendLine("{");
        for (var index = 0; index < members.Length; index++)
        {
            var member = members[index];
            source.Append("    ").Append(EscapeIdentifier(
                PascalCase(member.GetProperty("name").GetString()!)));
            if (member.TryGetProperty("value", out var value)
                && value.ValueKind == JsonValueKind.Number)
            {
                source.Append(" = ").Append(value.GetRawText());
            }
            source.AppendLine(index == members.Length - 1 ? string.Empty : ",");
        }
        return source.AppendLine("}").ToString();
    }

    private static string GenerateStringUnionModel(
        string namespaceName,
        Model model,
        JsonElement union)
    {
        var values = union.GetProperty("types")
            .EnumerateArray()
            .Where(static candidate =>
                Kind(candidate) == "literal"
                && candidate.GetProperty("value").ValueKind == JsonValueKind.String)
            .Select(static candidate => candidate.GetProperty("value").GetString()!)
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        var source = new StringBuilder(
            """
            // <auto-generated />
            #nullable enable

            """);
        source.Append("namespace ").Append(namespaceName).AppendLine(";")
            .AppendLine()
            .Append("[global::System.Text.Json.Serialization.JsonConverter(typeof(")
            .Append(model.Name).AppendLine("JsonConverter))]")
            .Append("public readonly record struct ").Append(model.Name)
            .AppendLine("(string Value)")
            .AppendLine("{");
        foreach (var value in values)
        {
            source.Append("    public static ").Append(model.Name).Append(' ')
                .Append(EscapeIdentifier(PascalCase(value)))
                .Append(" { get; } = new(").Append(Literal(value)).AppendLine(");");
        }
        source.AppendLine()
            .Append("    public static implicit operator string(").Append(model.Name)
            .AppendLine(" value) => value.Value;")
            .Append("    public static implicit operator ").Append(model.Name)
            .AppendLine("(string value) => new(value);")
            .AppendLine("    public override string ToString() => Value;")
            .AppendLine("}")
            .AppendLine()
            .Append("public sealed class ").Append(model.Name)
            .Append("JsonConverter : global::System.Text.Json.Serialization.JsonConverter<")
            .Append(model.Name).AppendLine(">")
            .AppendLine("{")
            .Append("    public override ").Append(model.Name)
            .AppendLine(" Read(ref global::System.Text.Json.Utf8JsonReader reader, global::System.Type typeToConvert, global::System.Text.Json.JsonSerializerOptions options)")
            .Append("        => new(reader.GetString() ?? throw new global::System.Text.Json.JsonException(")
            .Append(Literal($"Expected a string for {model.Name}.")).AppendLine("));")
            .AppendLine()
            .Append("    public override void Write(global::System.Text.Json.Utf8JsonWriter writer, ")
            .Append(model.Name)
            .AppendLine(" value, global::System.Text.Json.JsonSerializerOptions options)")
            .AppendLine("        => writer.WriteStringValue(value.Value);")
            .AppendLine("}");
        return source.ToString();
    }

    private static string GenerateBinding(
        GenerationContext generation,
        Binding binding,
        string policyPath)
    {
        var selfType = BindingTypeName(binding);
        var source = new StringBuilder(
            """
            // <auto-generated />
            #nullable enable

            """);
        source.Append("namespace ").Append(generation.Namespace).AppendLine(";")
            .AppendLine()
            .Append("public sealed class ").Append(selfType)
            .AppendLine(" : global::WebScene.JavaScript.Interop.IJavaScriptObjectReferenceProvider, global::System.IAsyncDisposable")
            .AppendLine("{")
            .AppendLine("    private readonly global::WebScene.JavaScript.Interop.IJavaScriptInvoker __webSceneInvoker;")
            .AppendLine("    private readonly global::WebScene.JavaScript.Interop.JavaScriptObjectReference __webSceneReference;")
            .AppendLine()
            .Append("    internal ").Append(binding.Name)
            .AppendLine("(global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker, global::WebScene.JavaScript.Interop.JavaScriptObjectReference reference)")
            .AppendLine("    {")
            .AppendLine("        __webSceneInvoker = invoker;")
            .AppendLine("        __webSceneReference = reference;")
            .AppendLine("    }")
            .AppendLine()
            .AppendLine("    public global::WebScene.JavaScript.Interop.JavaScriptObjectReference JavaScriptReference => __webSceneReference;")
            .AppendLine("    internal global::WebScene.JavaScript.Interop.JavaScriptObjectReference __WebSceneReference => __webSceneReference;")
            .AppendLine()
            .AppendLine("    public global::System.Threading.Tasks.ValueTask DisposeAsync() => __webSceneInvoker.ReleaseAsync(__webSceneReference);");
        source.AppendLine()
            .Append("    public static ").Append(selfType).AppendLine(" FromReference(")
            .AppendLine("        global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker,")
            .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptObjectReference reference)")
            .Append("        => new ").Append(selfType).AppendLine("(invoker, reference);");

        if (binding.Policy.TryGetProperty("constructors", out var constructors)
            && constructors.ValueKind == JsonValueKind.Array
            && constructors.GetArrayLength() > 0)
        {
            foreach (var constructor in constructors.EnumerateArray())
            {
                EmitConstructor(source, generation, binding, constructor, policyPath);
            }
        }
        else if (binding.Policy.TryGetProperty("constructor", out var constructor)
                 && constructor.ValueKind == JsonValueKind.Object)
        {
            EmitConstructor(source, generation, binding, constructor, policyPath);
        }

        if (binding.Policy.TryGetProperty("methods", out var methods))
        {
            foreach (var methodPolicy in methods.EnumerateArray())
            {
                if (methodPolicy.TryGetProperty("include", out var include)
                    && include.ValueKind == JsonValueKind.False)
                {
                    continue;
                }
                EmitMethod(
                    source,
                    generation,
                    binding,
                    methodPolicy,
                    policyPath);
            }
        }
        if (binding.Policy.TryGetProperty("properties", out var properties))
        {
            foreach (var propertyPolicy in properties.EnumerateArray())
            {
                if (propertyPolicy.TryGetProperty("include", out var include)
                    && include.ValueKind == JsonValueKind.False)
                {
                    continue;
                }
                EmitProperty(
                    source,
                    generation,
                    binding,
                    propertyPolicy,
                    policyPath);
            }
        }

        return source.AppendLine("}").ToString();
    }

    private static void EmitProperty(
        StringBuilder source,
        GenerationContext generation,
        Binding binding,
        JsonElement propertyPolicy,
        string policyPath)
    {
        var memberName = RequiredString(propertyPolicy, "source", policyPath);
        var property = binding.Root.GetProperty("properties")
            .EnumerateArray()
            .FirstOrDefault(candidate => string.Equals(
                candidate.GetProperty("name").GetString(),
                memberName,
                StringComparison.Ordinal));
        if (property.ValueKind == JsonValueKind.Undefined)
        {
            throw new InvalidDataException(
                $"Property '{binding.Source}.{memberName}' was not discovered.");
        }
        var getterName = OptionalString(propertyPolicy, "getterName")
                         ?? "Get" + PascalCase(memberName) + "Async";
        var declaredType = property.GetProperty("type");
        var promise = IsPromiseLikeReturn(declaredType);
        var effectiveType = Kind(declaredType) == "promise"
            ? declaredType.GetProperty("result")
            : declaredType;
        var mapping = MapType(
            generation,
            effectiveType,
            property.GetProperty("optional").GetBoolean(),
            $"{binding.Name}.{getterName}");
        var valueInvocation = promise
            ? "GetPromisePropertyAsync"
            : "GetPropertyAsync";
        var objectInvocation = promise
            ? "GetPromiseObjectPropertyAsync"
            : "GetObjectPropertyAsync";
        var binarySupported = CanEmitBinaryType(generation, effectiveType);
        var binaryName = "__WebSceneBinary"
                         + PascalCase(getterName)
                         + "Property";
        source.AppendLine()
            .Append("    public ");
        if (!binarySupported)
        {
            source.Append("async ");
        }
        source.Append("global::System.Threading.Tasks.ValueTask<")
            .Append(mapping.CSharpType).Append("> ").Append(getterName).AppendLine("(")
            .AppendLine("        global::System.Threading.CancellationToken cancellationToken = default)")
            .AppendLine("    {");
        if (binarySupported)
        {
            source.AppendLine("        var binaryInvoker = __webSceneInvoker as global::WebScene.JavaScript.Interop.IJavaScriptBinaryInvoker")
                .AppendLine("            ?? throw new global::System.NotSupportedException(")
                .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
                .AppendLine("        if (!binaryInvoker.IsBinaryInteropAvailable)")
                .AppendLine("        {")
                .AppendLine("            throw new global::System.NotSupportedException(")
                .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
                .AppendLine("        }")
                .Append("        return binaryInvoker.InvokeBinaryAsync<global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid, ")
                .Append(mapping.CSharpType).Append(", ").Append(binaryName)
                .Append("Codec>(").Append(binaryName)
                .AppendLine("CallSite, __webSceneReference, new global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid(), cancellationToken);")
                .AppendLine("    }");
            EmitBinaryInvocationCodec(
                source,
                generation,
                parameters: [],
                returnsVoid: false,
                effectiveReturn: effectiveType,
                returnMapping: mapping,
                binaryName: binaryName,
                operation: "GetProperty",
                globalName: null,
                memberName: memberName,
                promise: promise);
        }
        else if (mapping.IsBinding && mapping.IsNullable)
        {
            source.Append("        var reference = await __webSceneInvoker.")
                .Append(valueInvocation)
                .Append("<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                .Append("__webSceneReference, ").Append(Literal(memberName))
                .AppendLine(", cancellationToken).ConfigureAwait(false);")
                .Append("        return reference is { } value ? new ")
                .Append(mapping.NonNullableCSharpType)
                .AppendLine("(__webSceneInvoker, value) : null;");
        }
        else if (mapping.IsBinding)
        {
            source.Append("        return new ").Append(mapping.CSharpType)
                .Append("(__webSceneInvoker, await __webSceneInvoker.")
                .Append(objectInvocation).Append('(')
                .Append("__webSceneReference, ").Append(Literal(memberName))
                .AppendLine(", cancellationToken).ConfigureAwait(false));");
        }
        else if (mapping.IsFunctionReference && mapping.IsNullable)
        {
            source.Append("        var reference = await __webSceneInvoker.")
                .Append(valueInvocation)
                .Append("<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                .Append("__webSceneReference, ").Append(Literal(memberName))
                .AppendLine(", cancellationToken).ConfigureAwait(false);")
                .AppendLine("        return reference is { } value")
                .AppendLine("            ? new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(__webSceneInvoker, value)")
                .AppendLine("            : null;");
        }
        else if (mapping.IsFunctionReference)
        {
            source.Append("        return new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(__webSceneInvoker, await __webSceneInvoker.")
                .Append(objectInvocation).Append('(')
                .Append("__webSceneReference, ").Append(Literal(memberName))
                .AppendLine(", cancellationToken).ConfigureAwait(false));");
        }
        else if (mapping.IsObjectReference && !mapping.IsNullable)
        {
            source.Append("        return await __webSceneInvoker.")
                .Append(objectInvocation).Append('(')
                .Append("__webSceneReference, ").Append(Literal(memberName))
                .AppendLine(", cancellationToken).ConfigureAwait(false);");
        }
        else if (mapping.RequiresWireConversion)
        {
            source.Append("        var wire = (await __webSceneInvoker.")
                .Append(valueInvocation).Append('<')
                .Append(mapping.EffectiveWireCSharpType)
                .Append(">(__webSceneReference, ").Append(Literal(memberName))
                .AppendLine(", cancellationToken).ConfigureAwait(false))!;")
                .Append("        return ")
                .Append(mapping.ConvertFromWire("wire", "__webSceneInvoker"))
                .AppendLine(";");
        }
        else
        {
            source.Append("        return (await __webSceneInvoker.")
                .Append(valueInvocation).Append('<')
                .Append(mapping.CSharpType).Append(">(__webSceneReference, ")
                .Append(Literal(memberName))
                .AppendLine(", cancellationToken).ConfigureAwait(false))!;");
        }
        if (!binarySupported)
        {
            source.AppendLine("    }");
        }

        var writable = !property.GetProperty("readonly").GetBoolean()
                       && (!propertyPolicy.TryGetProperty("write", out var write)
                           || write.ValueKind != JsonValueKind.False);
        if (!writable)
        {
            return;
        }
        var setterName = OptionalString(propertyPolicy, "setterName")
                         ?? "Set" + PascalCase(memberName) + "Async";
        var setterMapping = promise
            ? new TypeMapping(
                "global::WebScene.JavaScript.Interop.JavaScriptObjectReference",
                IsBinding: false,
                IsObjectReference: true)
            : mapping;
        var binarySetterSupported =
            !promise
            && !property.GetProperty("optional").GetBoolean()
            && CanEmitBinaryType(generation, effectiveType);
        var binarySetterName = "__WebSceneBinary"
                               + PascalCase(setterName)
                               + "Property";
        source.AppendLine()
            .Append("    public global::System.Threading.Tasks.ValueTask ")
            .Append(setterName).AppendLine("(")
            .Append("        ").Append(setterMapping.CSharpType).AppendLine(" value,")
            .AppendLine("        global::System.Threading.CancellationToken cancellationToken = default)")
            .AppendLine("    {");
        if (binarySetterSupported)
        {
            source.AppendLine("        var binaryInvoker = __webSceneInvoker as global::WebScene.JavaScript.Interop.IJavaScriptBinaryInvoker")
                .AppendLine("            ?? throw new global::System.NotSupportedException(")
                .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
                .AppendLine("        if (!binaryInvoker.IsBinaryInteropAvailable)")
                .AppendLine("        {")
                .AppendLine("            throw new global::System.NotSupportedException(")
                .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
                .AppendLine("        }")
                .Append("        return binaryInvoker.InvokeBinaryVoidAsync<")
                .Append(binarySetterName).Append("Arguments, ")
                .Append(binarySetterName).Append("Codec>(")
                .Append(binarySetterName)
                .Append("CallSite, __webSceneReference, new ")
                .Append(binarySetterName)
                .AppendLine("Arguments(value), cancellationToken);")
                .AppendLine("    }");
            EmitBinaryInvocationCodec(
                source,
                generation,
                parameters:
                [
                    new MappedParameter(
                        "value",
                        setterMapping,
                        Optional: false,
                        Rest: false,
                        effectiveType.Clone())
                ],
                returnsVoid: true,
                effectiveReturn: effectiveType,
                returnMapping: new TypeMapping("void", false),
                binaryName: binarySetterName,
                operation: "SetProperty",
                globalName: null,
                memberName: memberName,
                promise: false);
            return;
        }
        source.AppendLine("        return __webSceneInvoker.SetPropertyAsync(")
            .AppendLine("            __webSceneReference,")
            .Append("            ").Append(Literal(memberName)).AppendLine(",")
            .Append("            ");
        if (setterMapping.IsBinding && setterMapping.IsNullable)
        {
            source.AppendLine("value is null")
                .AppendLine("                ? global::WebScene.JavaScript.Interop.JavaScriptArgument.From<object?>(null)")
                .AppendLine("                : global::WebScene.JavaScript.Interop.JavaScriptArgument.From(value.__WebSceneReference),");
        }
        else
        {
            source.Append("global::WebScene.JavaScript.Interop.JavaScriptArgument.From(")
                .Append(setterMapping.IsBinding
                    ? "value.__WebSceneReference"
                    : "value")
                .AppendLine("),");
        }
        source
            .AppendLine("            cancellationToken);");
        source.AppendLine("    }");
    }

    private static void EmitConstructor(
        StringBuilder source,
        GenerationContext generation,
        Binding binding,
        JsonElement constructor,
        string policyPath)
    {
        var globalName = RequiredString(constructor, "globalName", policyPath);
        var methodName = OptionalString(constructor, "name") ?? "CreateAsync";
        ConstructorParameter[] parameters;
        if (constructor.TryGetProperty("parameters", out var parameterList))
        {
            parameters = parameterList.EnumerateArray()
                .Select(parameter => new ConstructorParameter(
                    RequiredString(parameter, "name", policyPath),
                    RequiredString(parameter, "dotnetType", policyPath),
                    IsBinding: false,
                    IsNullable: false,
                    Optional: false,
                    Rest: false,
                    BinaryMapping: null,
                    BinaryType: default))
                .ToArray();
        }
        else if (binding.Root.TryGetProperty("constructors", out var constructors)
                 && constructors.GetArrayLength() > 0)
        {
            var overload = constructor.TryGetProperty("overload", out var overloadValue)
                ? overloadValue.GetInt32()
                : 0;
            if ((uint)overload >= (uint)constructors.GetArrayLength())
            {
                throw new InvalidDataException(
                    $"Constructor overload {overload} for '{binding.Source}' was not discovered.");
            }
            parameters = constructors[overload].GetProperty("parameters")
                .EnumerateArray()
                .Select(parameter =>
                {
                    var name = parameter.GetProperty("name").GetString()!;
                    var optional = parameter.GetProperty("optional").GetBoolean();
                    var rest = parameter.GetProperty("rest").GetBoolean();
                    var mapping = MapOptionalParameterType(
                        generation,
                        parameter.GetProperty("type"),
                        optional,
                        $"{binding.Name}.{methodName} parameter '{name}'");
                    var typeName = optional
                        ? $"global::WebScene.JavaScript.Interop.JavaScriptOptional<{mapping.CSharpType}>"
                        : mapping.CSharpType;
                    return new ConstructorParameter(
                        name,
                        typeName,
                        mapping.IsBinding,
                        mapping.IsNullable,
                        optional,
                        rest,
                        mapping,
                        parameter.GetProperty("type").Clone());
                })
                .ToArray();
        }
        else
        {
            parameters = [];
        }
        var binarySupported = parameters.All(parameter =>
            !parameter.Rest
            && parameter.BinaryMapping is not null
            && CanEmitBinaryType(generation, parameter.BinaryType));
        var binaryName = "__WebSceneBinary"
                         + PascalCase(methodName)
                         + "Constructor";
        source.AppendLine()
            .Append("    public static ");
        if (!binarySupported)
        {
            source.Append("async ");
        }
        source.Append("global::System.Threading.Tasks.ValueTask<")
            .Append(BindingTypeName(binding)).Append("> ").Append(methodName).AppendLine("(")
            .AppendLine("        global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker,");
        for (var index = 0; index < parameters.Length; index++)
        {
            var parameter = parameters[index];
            source.Append("        ")
                .Append(parameter.CSharpType)
                .Append(" @")
                .Append(parameter.Name);
            if (parameter.Optional)
            {
                source.Append(" = default");
            }
            source.AppendLine(",");
        }
        source.AppendLine("        global::System.Threading.CancellationToken cancellationToken = default)")
            .AppendLine("    {");
        if (binarySupported)
        {
            var binaryArgumentsType = parameters.Length == 0
                ? "global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid"
                : binaryName + "Arguments";
            var binaryArguments = parameters.Length == 0
                ? "new global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid()"
                : "new " + binaryName + "Arguments("
                  + string.Join(
                      ", ",
                      parameters.Select(parameter => "@" + parameter.Name))
                  + ")";
            source.AppendLine("        global::System.ArgumentNullException.ThrowIfNull(invoker);")
                .AppendLine("        var binaryInvoker = invoker as global::WebScene.JavaScript.Interop.IJavaScriptBinaryInvoker")
                .AppendLine("            ?? throw new global::System.NotSupportedException(")
                .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
                .AppendLine("        if (!binaryInvoker.IsBinaryInteropAvailable)")
                .AppendLine("        {")
                .AppendLine("            throw new global::System.NotSupportedException(")
                .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
                .AppendLine("        }")
                .Append("        return binaryInvoker.InvokeBinaryAsync<")
                .Append(binaryArgumentsType).Append(", ")
                .Append(BindingTypeName(binding)).Append(", ")
                .Append(binaryName).Append("Codec>(")
                .Append(binaryName).Append("CallSite, default, ")
                .Append(binaryArguments).AppendLine(", cancellationToken);")
                .AppendLine("    }");
            EmitBinaryConstructorCodec(
                source,
                generation,
                binding,
                parameters,
                binaryName,
                globalName);
            return;
        }
        source.Append("        return new ").Append(BindingTypeName(binding))
            .AppendLine("(invoker, await invoker.ConstructAsync(")
            .Append("            ").Append(Literal(globalName)).AppendLine(",")
            .Append("            ").Append(ArgumentArray(
                parameters.Select(parameter => new Parameter(
                    parameter.Name,
                    parameter.IsBinding,
                    parameter.IsNullable,
                    parameter.Optional,
                    parameter.Rest)))).AppendLine(",")
            .AppendLine("            cancellationToken).ConfigureAwait(false));")
            .AppendLine("    }");
    }

    private static void EmitMethod(
        StringBuilder source,
        GenerationContext generation,
        Binding binding,
        JsonElement methodPolicy,
        string policyPath)
    {
        var memberName = RequiredString(methodPolicy, "source", policyPath);
        var overload = methodPolicy.TryGetProperty("overload", out var overloadValue)
            ? overloadValue.GetInt32()
            : 0;
        var method = binding.Root.GetProperty("methods")
            .EnumerateArray()
            .FirstOrDefault(candidate =>
                string.Equals(
                    candidate.GetProperty("name").GetString(),
                    memberName,
                    StringComparison.Ordinal)
                && candidate.GetProperty("overload").GetInt32() == overload);
        if (method.ValueKind == JsonValueKind.Undefined)
        {
            throw new InvalidDataException(
                $"Method '{binding.Source}.{memberName}' overload {overload} was not discovered.");
        }

        var methodName = OptionalString(methodPolicy, "name")
                         ?? PascalCase(memberName) + "Async";
        var borrowedName = OptionalString(methodPolicy, "borrowedName");
        var methodTypeParameters = method.TryGetProperty("typeParameters", out var genericParameters)
            ? genericParameters.EnumerateArray()
                .Select(static item => EscapeIdentifier(item.GetString()!))
                .ToArray()
            : [];
        var methodGenericSuffix = methodTypeParameters.Length == 0
            ? string.Empty
            : "<" + string.Join(", ", methodTypeParameters) + ">";
        var omitOptional = !methodPolicy.TryGetProperty(
                               "omitOptionalParameters",
                               out var omitValue)
                           || omitValue.ValueKind != JsonValueKind.False;
        var parameters = method.GetProperty("parameters")
            .EnumerateArray()
            .Where(parameter =>
                !omitOptional
                || !parameter.GetProperty("optional").GetBoolean())
            .Select(parameter =>
            {
                var name = parameter.GetProperty("name").GetString()!;
                var optional = parameter.GetProperty("optional").GetBoolean();
                var rest = parameter.GetProperty("rest").GetBoolean();
                var mapping = MapOptionalParameterType(
                    generation,
                    parameter.GetProperty("type"),
                    optional,
                    $"{binding.Name}.{methodName} parameter '{name}'");
                if (optional)
                {
                    mapping = mapping with
                    {
                        CSharpType =
                            $"global::WebScene.JavaScript.Interop.JavaScriptOptional<{mapping.CSharpType}>"
                    };
                }
                return new MappedParameter(
                    name,
                    mapping,
                    optional,
                    rest,
                    parameter.GetProperty("type").Clone());
            })
            .ToArray();

        var declaredReturn = method.GetProperty("returns");
        var promise = IsPromiseLikeReturn(declaredReturn);
        var effectiveReturn = Kind(declaredReturn) == "promise"
            ? declaredReturn.GetProperty("result")
            : declaredReturn;
        var returnsVoid = IsVoidLikeReturn(declaredReturn);
        var returnMapping = returnsVoid
            ? new TypeMapping("void", false)
            : MapType(
                generation,
                effectiveReturn,
                optional: false,
                $"{binding.Name}.{methodName} return");
        var binarySupported = methodTypeParameters.Length == 0
                              && parameters.All(parameter =>
                                  !parameter.Rest
                                  && CanEmitBinaryType(
                                      generation,
                                      parameter.Type))
                              && (returnsVoid
                                  || CanEmitBinaryType(
                                      generation,
                                      effectiveReturn));
        var binaryName = "__WebSceneBinary"
                         + PascalCase(memberName)
                         + overload;
        if (borrowedName is not null
            && (!binarySupported || Kind(effectiveReturn) != "array"))
        {
            throw new InvalidDataException(
                $"Borrowed method '{binding.Name}.{borrowedName}' requires a binary-supported array return type.");
        }

        source.AppendLine()
            .Append("    public ");
        if (!binarySupported)
        {
            source.Append("async ");
        }
        source.Append("global::System.Threading.Tasks.ValueTask");
        if (!returnsVoid)
        {
            source.Append('<').Append(returnMapping.CSharpType).Append('>');
        }
        source.Append(' ').Append(methodName).Append(methodGenericSuffix).AppendLine("(");
        foreach (var parameter in parameters)
        {
            source.Append("        ").Append(parameter.Mapping.CSharpType)
                .Append(" @").Append(parameter.Name);
            if (parameter.Optional)
            {
                source.Append(" = default");
            }
            source.AppendLine(",");
        }
        source.AppendLine("        global::System.Threading.CancellationToken cancellationToken = default)")
            .AppendLine("    {");

        if (binarySupported)
        {
            EmitBinaryMethodDispatch(
                source,
                parameters,
                returnsVoid,
                returnMapping,
                binaryName);
            source.AppendLine("    }");
            EmitBinaryInvocationCodec(
                source,
                generation,
                parameters,
                returnsVoid,
                effectiveReturn,
                returnMapping,
                binaryName,
                operation: "InvokeMember",
                globalName: null,
                memberName: memberName,
                promise: promise);
            if (borrowedName is not null)
            {
                EmitBorrowedBinaryMethod(
                    source,
                    parameters,
                    borrowedName,
                    binaryName);
            }
            return;
        }

        var arguments = ArgumentArray(parameters.Select(parameter =>
            new Parameter(
                parameter.Name,
                parameter.Mapping.IsBinding,
                parameter.Mapping.IsNullable,
                parameter.Optional,
                parameter.Rest)));
        if (promise)
        {
            if (returnsVoid)
            {
                source.Append("        await __webSceneInvoker.InvokePromiseAsync<global::System.Text.Json.JsonElement>(")
                    .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false);");
            }
            else if (returnMapping.IsBinding && returnMapping.IsNullable)
            {
                source.Append("        var reference = await __webSceneInvoker.InvokePromiseAsync<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                    .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false);")
                    .Append("        return reference is { } value ? new ")
                    .Append(returnMapping.NonNullableCSharpType)
                    .AppendLine("(__webSceneInvoker, value) : null;");
            }
            else if (returnMapping.IsBinding)
            {
                source.Append("        return new ").Append(returnMapping.CSharpType)
                    .Append("(__webSceneInvoker, await __webSceneInvoker.InvokePromiseObjectAsync(")
                    .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false));");
            }
            else if (returnMapping.IsFunctionReference && returnMapping.IsNullable)
            {
                source.Append("        var reference = await __webSceneInvoker.InvokePromiseAsync<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                    .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false);")
                    .AppendLine("        return reference is { } value")
                    .AppendLine("            ? new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(__webSceneInvoker, value)")
                    .AppendLine("            : null;");
            }
            else if (returnMapping.IsFunctionReference)
            {
                source.Append("        return new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(__webSceneInvoker, await __webSceneInvoker.InvokePromiseObjectAsync(")
                    .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false));");
            }
            else if (returnMapping.IsObjectReference && !returnMapping.IsNullable)
            {
                source.Append("        return await __webSceneInvoker.InvokePromiseObjectAsync(")
                    .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                    .Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false);");
            }
            else if (returnMapping.RequiresWireConversion)
            {
                source.Append("        var wire = (await __webSceneInvoker.InvokePromiseAsync<")
                    .Append(returnMapping.EffectiveWireCSharpType)
                    .Append(">(__webSceneReference, ").Append(Literal(memberName))
                    .Append(", ").Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false))!;")
                    .Append("        return ")
                    .Append(returnMapping.ConvertFromWire(
                        "wire",
                        "__webSceneInvoker"))
                    .AppendLine(";");
            }
            else
            {
                source.Append("        return (await __webSceneInvoker.InvokePromiseAsync<")
                    .Append(returnMapping.CSharpType).Append(">(__webSceneReference, ")
                    .Append(Literal(memberName)).Append(", ").Append(arguments)
                    .AppendLine(", cancellationToken).ConfigureAwait(false))!;");
            }
        }
        else if (returnsVoid)
        {
            source.Append("        await __webSceneInvoker.InvokeVoidAsync(__webSceneReference, ")
                .Append(Literal(memberName)).Append(", ").Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false);");
        }
        else if (returnMapping.IsBinding && returnMapping.IsNullable)
        {
            source.Append("        var reference = await __webSceneInvoker.InvokeAsync<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false);")
                .Append("        return reference is { } value ? new ")
                .Append(returnMapping.NonNullableCSharpType)
                .AppendLine("(__webSceneInvoker, value) : null;");
        }
        else if (returnMapping.IsBinding)
        {
            source.Append("        return new ").Append(returnMapping.CSharpType)
                .Append("(__webSceneInvoker, await __webSceneInvoker.InvokeObjectAsync(")
                .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false));");
        }
        else if (returnMapping.IsFunctionReference && returnMapping.IsNullable)
        {
            source.Append("        var reference = await __webSceneInvoker.InvokeAsync<global::WebScene.JavaScript.Interop.JavaScriptObjectReference?>(")
                .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false);")
                .AppendLine("        return reference is { } value")
                .AppendLine("            ? new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(__webSceneInvoker, value)")
                .AppendLine("            : null;");
        }
        else if (returnMapping.IsFunctionReference)
        {
            source.Append("        return new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(__webSceneInvoker, await __webSceneInvoker.InvokeObjectAsync(")
                .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false));");
        }
        else if (returnMapping.IsObjectReference && !returnMapping.IsNullable)
        {
            source.Append("        return await __webSceneInvoker.InvokeObjectAsync(")
                .Append("__webSceneReference, ").Append(Literal(memberName)).Append(", ")
                .Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false);");
        }
        else if (returnMapping.RequiresWireConversion)
        {
            source.Append("        var wire = (await __webSceneInvoker.InvokeAsync<")
                .Append(returnMapping.EffectiveWireCSharpType)
                .Append(">(__webSceneReference, ").Append(Literal(memberName))
                .Append(", ").Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false))!;")
                .Append("        return ")
                .Append(returnMapping.ConvertFromWire(
                    "wire",
                    "__webSceneInvoker"))
                .AppendLine(";");
        }
        else
        {
            source.Append("        return (await __webSceneInvoker.InvokeAsync<")
                .Append(returnMapping.CSharpType).Append(">(__webSceneReference, ")
                .Append(Literal(memberName)).Append(", ").Append(arguments)
                .AppendLine(", cancellationToken).ConfigureAwait(false))!;");
        }
        source.AppendLine("    }");
    }

    private static void EmitBorrowedBinaryMethod(
        StringBuilder source,
        IReadOnlyList<MappedParameter> parameters,
        string borrowedName,
        string binaryName)
    {
        var argumentsType = parameters.Count == 0
            ? "global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid"
            : binaryName + "Arguments";
        var arguments = parameters.Count == 0
            ? "new global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid()"
            : "new " + binaryName + "Arguments("
              + string.Join(
                  ", ",
                  parameters.Select(parameter => "@" + parameter.Name))
              + ")";
        var typeStem = borrowedName.EndsWith(
                "Async",
                StringComparison.Ordinal)
            ? borrowedName.Substring(0, borrowedName.Length - 5)
            : borrowedName;
        typeStem = PascalCase(typeStem);
        var leaseType = typeStem + "Lease";
        var viewType = typeStem + "View";

        source.AppendLine()
            .Append("    public async global::System.Threading.Tasks.ValueTask<")
            .Append(leaseType).Append("> ").Append(borrowedName)
            .AppendLine("(");
        foreach (var parameter in parameters)
        {
            source.Append("        ").Append(parameter.Mapping.CSharpType)
                .Append(" @").Append(parameter.Name);
            if (parameter.Optional)
            {
                source.Append(" = default");
            }
            source.AppendLine(",");
        }
        source.AppendLine("        global::System.Threading.CancellationToken cancellationToken = default)")
            .AppendLine("    {")
            .AppendLine("        if (__webSceneInvoker is not global::WebScene.JavaScript.Interop.IJavaScriptBinaryInvoker binaryInvoker")
            .AppendLine("            || !binaryInvoker.IsBinaryInteropAvailable)")
            .AppendLine("        {")
            .AppendLine("            throw new global::System.NotSupportedException(")
            .AppendLine("                \"Borrowed JavaScript results require the native binary transport.\");")
            .AppendLine("        }")
            .Append("        return new ").Append(leaseType).AppendLine("(")
            .Append("            await binaryInvoker.InvokeBinaryBorrowedAsync<")
            .Append(argumentsType).Append(", ").Append(binaryName)
            .Append("Codec>(").Append(binaryName)
            .Append("CallSite, __webSceneReference, ")
            .Append(arguments)
            .AppendLine(", cancellationToken).ConfigureAwait(false));")
            .AppendLine("    }")
            .AppendLine()
            .Append("    public readonly struct ").Append(leaseType)
            .AppendLine(" : global::System.IDisposable")
            .AppendLine("    {")
            .AppendLine("        private readonly global::WebScene.JavaScript.Interop.JavaScriptBinaryResultLease? _lease;")
            .AppendLine()
            .Append("        internal ").Append(leaseType).AppendLine("(")
            .AppendLine("            global::WebScene.JavaScript.Interop.JavaScriptBinaryResultLease lease)")
            .AppendLine("        {")
            .AppendLine("            _lease = lease;")
            .AppendLine("        }")
            .AppendLine()
            .Append("        public ").Append(viewType).AppendLine(" Borrow()")
            .AppendLine("            => new((_lease ?? throw new global::System.InvalidOperationException(")
            .AppendLine("                \"The borrowed result lease is uninitialized.\")).Borrow());")
            .AppendLine()
            .AppendLine("        public void Dispose() => _lease?.Dispose();")
            .AppendLine("    }")
            .AppendLine()
            .Append("    public ref struct ").Append(viewType).AppendLine()
            .AppendLine("    {")
            .AppendLine("        private global::WebScene.JavaScript.Interop.JavaScriptBinaryBorrowScope _scope;")
            .AppendLine()
            .Append("        internal ").Append(viewType).AppendLine("(")
            .AppendLine("            global::WebScene.JavaScript.Interop.JavaScriptBinaryBorrowScope scope)")
            .AppendLine("        {")
            .AppendLine("            _scope = scope;")
            .AppendLine("            if (_scope.Root.Kind != global::WebScene.JavaScript.Interop.JavaScriptBinaryValueKind.Array)")
            .AppendLine("            {")
            .AppendLine("                _scope.Dispose();")
            .AppendLine("                throw new global::System.IO.InvalidDataException(")
            .AppendLine("                    \"The borrowed JavaScript result is not an array.\");")
            .AppendLine("            }")
            .AppendLine("        }")
            .AppendLine()
            .AppendLine("        public readonly int Count => _scope.Root.Count;")
            .AppendLine()
            .AppendLine("        public readonly global::WebScene.JavaScript.Interop.JavaScriptBinaryValue this[int index]")
            .AppendLine("            => _scope.Root.GetArrayItem(index);")
            .AppendLine()
            .AppendLine("        public void Dispose() => _scope.Dispose();")
            .AppendLine("    }");
    }

    private static void EmitBinaryMethodDispatch(
        StringBuilder source,
        IReadOnlyList<MappedParameter> parameters,
        bool returnsVoid,
        TypeMapping returnMapping,
        string binaryName)
    {
        var argumentsType = parameters.Count == 0
            ? "global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid"
            : binaryName + "Arguments";
        var arguments = parameters.Count == 0
            ? "new global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid()"
            : "new " + binaryName + "Arguments("
              + string.Join(
                  ", ",
                  parameters.Select(parameter => "@" + parameter.Name))
              + ")";
        source.AppendLine("        var binaryInvoker = __webSceneInvoker as global::WebScene.JavaScript.Interop.IJavaScriptBinaryInvoker")
            .AppendLine("            ?? throw new global::System.NotSupportedException(")
            .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
            .AppendLine("        if (!binaryInvoker.IsBinaryInteropAvailable)")
            .AppendLine("        {")
            .AppendLine("            throw new global::System.NotSupportedException(")
            .AppendLine("                \"Generated JavaScript APIs require the binary ABI 3 transport.\");")
            .AppendLine("        }");
        if (returnsVoid)
        {
            source.Append("        return binaryInvoker.InvokeBinaryVoidAsync<")
                .Append(argumentsType).Append(", ").Append(binaryName)
                .Append("Codec>(").Append(binaryName)
                .Append("CallSite, __webSceneReference, ")
                .Append(arguments)
                .AppendLine(", cancellationToken);");
        }
        else
        {
            source.Append("        return binaryInvoker.InvokeBinaryAsync<")
                .Append(argumentsType).Append(", ")
                .Append(returnMapping.CSharpType).Append(", ")
                .Append(binaryName).Append("Codec>(")
                .Append(binaryName)
                .Append("CallSite, __webSceneReference, ")
                .Append(arguments)
                .AppendLine(", cancellationToken);");
        }
    }

    private static void EmitBinaryInvocationCodec(
        StringBuilder source,
        GenerationContext generation,
        IReadOnlyList<MappedParameter> parameters,
        bool returnsVoid,
        JsonElement effectiveReturn,
        TypeMapping returnMapping,
        string binaryName,
        string operation,
        string? globalName,
        string? memberName,
        bool promise)
    {
        var argumentsType = parameters.Count == 0
            ? "global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid"
            : binaryName + "Arguments";
        var resultType = returnsVoid
            ? "global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid"
            : returnMapping.CSharpType;
        var resultMode = returnsVoid
            ? "Void"
            : returnMapping.IsBinding
              || returnMapping.IsObjectReference
              || returnMapping.IsFunctionReference
                ? "RetainedHandle"
                : "Value";
        source.AppendLine()
            .Append("    private static readonly global::WebScene.JavaScript.Interop.JavaScriptBinaryCallSite ")
            .Append(binaryName).AppendLine("CallSite = new(")
            .Append("        global::WebScene.JavaScript.Interop.JavaScriptBinaryOperation.")
            .Append(operation).AppendLine(",")
            .Append("        globalName: ")
            .Append(globalName is null ? "null" : Literal(globalName))
            .AppendLine(",")
            .Append("        memberName: ")
            .Append(memberName is null ? "null" : Literal(memberName))
            .AppendLine(",")
            .Append("        global::WebScene.JavaScript.Interop.JavaScriptBinaryResultMode.")
            .Append(resultMode);
        if (promise)
        {
            source.AppendLine(",")
                .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryCallFlags.AwaitPromise);");
        }
        else
        {
            source.AppendLine(");");
        }

        if (parameters.Count > 0)
        {
            source.AppendLine()
                .Append("    private readonly record struct ")
                .Append(binaryName).AppendLine("Arguments(");
            for (var index = 0; index < parameters.Count; index++)
            {
                var parameter = parameters[index];
                source.Append("        ").Append(parameter.Mapping.CSharpType)
                    .Append(' ').Append(PascalCase(parameter.Name));
                source.AppendLine(index + 1 == parameters.Count ? ");" : ",");
            }
        }

        source.AppendLine()
            .Append("    private readonly struct ").Append(binaryName)
            .Append("Codec : global::WebScene.JavaScript.Interop.IJavaScriptBinaryCodec<")
            .Append(argumentsType).Append(", ").Append(resultType)
            .AppendLine(">")
            .AppendLine("    {")
            .AppendLine("        public static uint EncodeArguments(")
            .AppendLine("            ref global::WebScene.JavaScript.Interop.JavaScriptBinaryWriter writer,")
            .Append("            in ").Append(argumentsType)
            .AppendLine(" arguments)")
            .AppendLine("        {")
            .Append("            var root = writer.BeginArray(")
            .Append(parameters.Count).AppendLine(");");
        for (var index = 0; index < parameters.Count; index++)
        {
            var parameter = parameters[index];
            var expression =
                "arguments." + PascalCase(parameter.Name);
            string value;
            if (parameter.Optional)
            {
                value = generation.NextLocal("binaryArgument");
                source.Append("            uint ").Append(value)
                    .AppendLine(";")
                    .Append("            if (").Append(expression)
                    .AppendLine(".HasValue)")
                    .AppendLine("            {");
                var child = EmitBinaryWriteValue(
                    source,
                    generation,
                    OptionalBinaryPayloadType(parameter.Type),
                    expression + ".Value!",
                    "                ");
                source.Append("                ").Append(value).Append(" = ")
                    .Append(child).AppendLine(";")
                    .AppendLine("            }")
                    .AppendLine("            else")
                    .AppendLine("            {")
                    .Append("                ").Append(value)
                    .AppendLine(" = writer.WriteUndefined();")
                    .AppendLine("            }");
            }
            else
            {
                value = EmitBinaryWriteValue(
                    source,
                    generation,
                    parameter.Type,
                    expression,
                    "            ");
            }
            source.Append("            writer.SetArrayItem(root, ")
                .Append(index).Append(", ").Append(value).AppendLine(");");
        }
        source.AppendLine("            return root;")
            .AppendLine("        }")
            .AppendLine()
            .Append("        public static ").Append(resultType)
            .AppendLine(" DecodeResult(")
            .AppendLine("            global::WebScene.JavaScript.Interop.JavaScriptBinaryValue value,")
            .AppendLine("            global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker)")
            .AppendLine("        {");
        if (returnsVoid)
        {
            source.AppendLine("            return new global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid();");
        }
        else
        {
            var result = EmitBinaryReadValue(
                source,
                generation,
                effectiveReturn,
                "value",
                "invoker",
                "            ");
            source.Append("            return ").Append(result)
                .AppendLine(";");
        }
        source.AppendLine("        }")
            .AppendLine("    }");
    }

    private static void EmitBinaryConstructorCodec(
        StringBuilder source,
        GenerationContext generation,
        Binding binding,
        IReadOnlyList<ConstructorParameter> parameters,
        string binaryName,
        string globalName)
    {
        var argumentsType = parameters.Count == 0
            ? "global::WebScene.JavaScript.Interop.JavaScriptBinaryVoid"
            : binaryName + "Arguments";
        var resultType = BindingTypeName(binding);
        source.AppendLine()
            .Append("    private static readonly global::WebScene.JavaScript.Interop.JavaScriptBinaryCallSite ")
            .Append(binaryName).AppendLine("CallSite = new(")
            .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryOperation.Construct,")
            .Append("        globalName: ").Append(Literal(globalName))
            .AppendLine(",")
            .AppendLine("        memberName: null,")
            .AppendLine("        global::WebScene.JavaScript.Interop.JavaScriptBinaryResultMode.RetainedHandle);");

        if (parameters.Count > 0)
        {
            source.AppendLine()
                .Append("    private readonly record struct ")
                .Append(binaryName).AppendLine("Arguments(");
            for (var index = 0; index < parameters.Count; index++)
            {
                var parameter = parameters[index];
                source.Append("        ").Append(parameter.CSharpType)
                    .Append(' ').Append(PascalCase(parameter.Name));
                source.AppendLine(index + 1 == parameters.Count ? ");" : ",");
            }
        }

        source.AppendLine()
            .Append("    private readonly struct ").Append(binaryName)
            .Append("Codec : global::WebScene.JavaScript.Interop.IJavaScriptBinaryCodec<")
            .Append(argumentsType).Append(", ").Append(resultType)
            .AppendLine(">")
            .AppendLine("    {")
            .AppendLine("        public static uint EncodeArguments(")
            .AppendLine("            ref global::WebScene.JavaScript.Interop.JavaScriptBinaryWriter writer,")
            .Append("            in ").Append(argumentsType)
            .AppendLine(" arguments)")
            .AppendLine("        {")
            .Append("            var root = writer.BeginArray(")
            .Append(parameters.Count).AppendLine(");");
        for (var index = 0; index < parameters.Count; index++)
        {
            var parameter = parameters[index];
            var expression = "arguments." + PascalCase(parameter.Name);
            string value;
            if (parameter.Optional)
            {
                value = generation.NextLocal("binaryArgument");
                source.Append("            uint ").Append(value)
                    .AppendLine(";")
                    .Append("            if (").Append(expression)
                    .AppendLine(".HasValue)")
                    .AppendLine("            {");
                var child = EmitBinaryWriteValue(
                    source,
                    generation,
                    OptionalBinaryPayloadType(parameter.BinaryType),
                    expression + ".Value!",
                    "                ");
                source.Append("                ").Append(value).Append(" = ")
                    .Append(child).AppendLine(";")
                    .AppendLine("            }")
                    .AppendLine("            else")
                    .AppendLine("            {")
                    .Append("                ").Append(value)
                    .AppendLine(" = writer.WriteUndefined();")
                    .AppendLine("            }");
            }
            else
            {
                value = EmitBinaryWriteValue(
                    source,
                    generation,
                    parameter.BinaryType,
                    expression,
                    "            ");
            }
            source.Append("            writer.SetArrayItem(root, ")
                .Append(index).Append(", ").Append(value).AppendLine(");");
        }
        source.AppendLine("            return root;")
            .AppendLine("        }")
            .AppendLine()
            .Append("        public static ").Append(resultType)
            .AppendLine(" DecodeResult(")
            .AppendLine("            global::WebScene.JavaScript.Interop.JavaScriptBinaryValue value,")
            .AppendLine("            global::WebScene.JavaScript.Interop.IJavaScriptInvoker invoker)")
            .AppendLine("        {")
            .Append("            return new ").Append(resultType)
            .AppendLine("(invoker, value.GetHandle());")
            .AppendLine("        }")
            .AppendLine("    }");
    }

    private static TypeMapping MapType(
        GenerationContext generation,
        JsonElement type,
        bool optional,
        string member)
    {
        var kind = Kind(type);
        if (kind == "typeParameter"
            && generation.ActiveTypeParameterMappings is { } substitutions
            && substitutions.TryGetValue(
                type.GetProperty("name").GetString()!,
                out var substitution))
        {
            if (!optional)
            {
                return substitution;
            }
            var nullablePublicType = Nullable(substitution.CSharpType);
            return substitution with
            {
                CSharpType = nullablePublicType,
                WireCSharpType = Nullable(
                    substitution.EffectiveWireCSharpType),
                FromWireTemplate =
                    "__VALUE__ is { } genericValue ? "
                    + substitution.ConvertFromWire(
                        "genericValue",
                        "__INVOKER__")
                    + " : default("
                    + nullablePublicType
                    + ")"
            };
        }
        if (kind == "array")
        {
            return MapArrayType(generation, type, optional, member);
        }
        if (kind == "tuple")
        {
            return MapTupleType(generation, type, optional, member);
        }
        if (kind == "inlineObject")
        {
            return MapInlineObjectType(generation, type, optional, member);
        }
        if (kind == "reference"
            && type.GetProperty("name").GetString() == "Record"
            && type.TryGetProperty("typeArguments", out var recordArguments)
            && recordArguments.GetArrayLength() >= 2)
        {
            return MapRecordType(
                generation,
                recordArguments,
                optional,
                member);
        }
        if (kind == "promise")
        {
            return MapType(
                generation,
                type.GetProperty("result"),
                optional,
                member);
        }
        string? csharpType = kind switch
        {
            "string" => "string",
            "number" => "double",
            "boolean" => "bool",
            "bigint" => "global::System.Numerics.BigInteger",
            "callback" => "global::WebScene.JavaScript.Interop.JavaScriptFunctionReference",
            "intersection" => ResolveIntersection(generation, type, member),
            "object" or "any" or "unknown"
                => "global::System.Text.Json.JsonElement",
            "display" or "symbol"
                => null,
            "never" or "void" => "global::System.Text.Json.JsonElement",
            "null" or "undefined"
                => "global::WebScene.JavaScript.Interop.JavaScriptNullish",
            "literal" => type.GetProperty("value").ValueKind == JsonValueKind.String
                ? "string"
                : type.GetProperty("value").ValueKind is JsonValueKind.True or JsonValueKind.False
                    ? "bool"
                    : "double",
            "typeParameter" => EscapeIdentifier(type.GetProperty("name").GetString()!),
            "reference" => ResolveReference(generation, type),
            "union" => ResolveUnion(generation, type, member),
            _ => null
        };
        var referenceName = kind == "reference"
            ? type.GetProperty("name").GetString()
            : null;
        var qualifiedReferenceName = kind == "reference"
                                     && type.TryGetProperty("qualifiedName", out var qualified)
            ? qualified.GetString()
            : null;
        TypeMapping? referenceAliasMapping = null;
        if (kind == "reference")
        {
            var sourceName = qualifiedReferenceName ?? referenceName!;
            if (TryGetType(
                    generation.Types,
                    sourceName,
                    referenceName!,
                    out var sourceType)
                && Kind(sourceType) == "typeAlias"
                && sourceType.TryGetProperty("aliasTarget", out var aliasTarget))
            {
                referenceAliasMapping = MapType(
                    generation,
                    aliasTarget,
                    optional: false,
                    $"alias '{sourceName}'");
            }
        }
        var isBinding = referenceName is not null
                        && (generation.BindingNames.ContainsKey(referenceName)
                            || qualifiedReferenceName is not null
                            && generation.BindingNames.ContainsKey(qualifiedReferenceName));
        if (referenceAliasMapping is { } aliasMapping)
        {
            isBinding = aliasMapping.IsBinding;
        }
        string? objectModelWireType = null;
        TypeMapping[] objectModelArgumentMappings = [];
        if (kind == "reference" && !isBinding)
        {
            var sourceName = qualifiedReferenceName ?? referenceName!;
            var modelName = qualifiedReferenceName is not null
                            && generation.ModelNames.TryGetValue(
                                qualifiedReferenceName,
                                out var qualifiedModelName)
                ? qualifiedModelName
                : generation.ModelNames.TryGetValue(
                    referenceName!,
                    out var simpleModelName)
                    ? simpleModelName
                    : null;
            if (modelName is not null
                && TryGetType(
                    generation.Types,
                    sourceName,
                    referenceName!,
                    out var modelType)
                && IsObjectModelType(modelType))
            {
                objectModelArgumentMappings =
                    type.TryGetProperty("typeArguments", out var typeArguments)
                    && typeArguments.GetArrayLength() > 0
                        ? typeArguments.EnumerateArray()
                            .Select(argument => MapType(
                                generation,
                                argument,
                                optional: false,
                                $"generic argument for '{modelName}'"))
                            .ToArray()
                        : [];
                var declaredParameterCount =
                    modelType.TryGetProperty(
                        "typeParameters",
                        out var declaredParameters)
                        ? declaredParameters.GetArrayLength()
                        : 0;
                if (objectModelArgumentMappings.Length == 0
                    && declaredParameterCount > 0)
                {
                    objectModelArgumentMappings = Enumerable.Repeat(
                            new TypeMapping(
                                "global::System.Text.Json.JsonElement",
                                IsBinding: false),
                            declaredParameterCount)
                        .ToArray();
                }
                objectModelWireType = modelName + "Wire";
                if (objectModelArgumentMappings.Length > 0)
                {
                    objectModelWireType += "<"
                                           + string.Join(
                                               ", ",
                                               objectModelArgumentMappings.Select(
                                                   static argument =>
                                                       argument.EffectiveWireCSharpType))
                                           + ">";
                }
            }
        }
        var isObjectReference = false;
        TypeMapping? unionConcreteMapping = null;
        TypeMapping[] unionCandidateMappings = [];
        var unionIsNullable = false;
        if (kind == "union")
        {
            var unionTypes = FlattenUnionTypes(type).ToArray();
            var concrete = unionTypes
                .Where(static candidate => Kind(candidate) is not ("null" or "undefined"))
                .ToArray();
            unionIsNullable = unionTypes.Any(
                static candidate => Kind(candidate) is "null" or "undefined");
            var mappedCandidates = concrete
                .Select(candidate => MapType(
                    generation,
                    candidate,
                    optional: false,
                    member))
                .ToArray();
            unionIsNullable = unionIsNullable
                              || mappedCandidates.Any(
                                  static candidate => candidate.IsNullable);
            unionCandidateMappings = mappedCandidates
                .GroupBy(
                    static candidate => candidate.NonNullableCSharpType,
                    StringComparer.Ordinal)
                .Select(static candidates =>
                    candidates.OrderBy(
                        static candidate => candidate.IsNullable).First())
                .ToArray();
            if (unionCandidateMappings.Any(
                    static candidate => candidate.RequiresWireConversion))
            {
                var wireCandidates = new List<TypeMapping>();
                foreach (var wireGroup in unionCandidateMappings.GroupBy(
                             static candidate =>
                                 candidate.EffectiveWireCSharpType.TrimEnd('?'),
                             StringComparer.Ordinal))
                {
                    var candidates = wireGroup.ToArray();
                    if (candidates.Length == 1)
                    {
                        wireCandidates.Add(candidates[0]);
                        continue;
                    }

                    var rawWireType = wireGroup.Key;
                    generation.Context.ReportDiagnostic(Diagnostic.Create(
                        AmbiguousRetainedUnion,
                        Location.None,
                        member,
                        rawWireType));
                    wireCandidates.Add(new TypeMapping(
                        rawWireType,
                        IsBinding: false,
                        IsObjectReference: string.Equals(
                            rawWireType,
                            "global::WebScene.JavaScript.Interop.JavaScriptObjectReference",
                            StringComparison.Ordinal)));
                }
                unionCandidateMappings = wireCandidates.ToArray();
                var collapsedUnionTypes = unionCandidateMappings
                    .Select(static candidate => candidate.CSharpType)
                    .ToArray();
                csharpType = collapsedUnionTypes.Length == 1
                    ? unionIsNullable
                        ? Nullable(collapsedUnionTypes[0])
                        : collapsedUnionTypes[0]
                    : JavaScriptUnionType(collapsedUnionTypes)
                      + (unionIsNullable ? "?" : string.Empty);
            }
            if (unionCandidateMappings.Length == 1)
            {
                var concreteMapping = unionCandidateMappings[0];
                unionConcreteMapping = concreteMapping;
                isBinding = concreteMapping.IsBinding;
                isObjectReference = concreteMapping.IsObjectReference;
            }
        }
        if (csharpType is null)
        {
            var display = type.TryGetProperty("text", out var text)
                ? text.GetString()
                : type.TryGetProperty("display", out var displayValue)
                    ? displayValue.GetString()
                    : kind;
            generation.Context.ReportDiagnostic(Diagnostic.Create(
                UnsupportedType,
                Location.None,
                display ?? kind,
                member));
            csharpType = "global::System.Text.Json.JsonElement";
        }
        if (optional)
        {
            csharpType = Nullable(csharpType);
        }
        isObjectReference = isObjectReference
                            || csharpType.TrimEnd('?')
                            == "global::WebScene.JavaScript.Interop.JavaScriptObjectReference";
        var isFunctionReference = csharpType.TrimEnd('?')
                                  == "global::WebScene.JavaScript.Interop.JavaScriptFunctionReference";
        if (referenceAliasMapping is { } resolvedAlias)
        {
            isObjectReference = resolvedAlias.IsObjectReference;
            isFunctionReference = resolvedAlias.IsFunctionReference;
        }
        isBinding = isBinding
                    || generation.BindingNames.Values.Any(bindingName =>
                        string.Equals(
                            csharpType.TrimEnd('?'),
                            bindingName,
                            StringComparison.Ordinal)
                        || csharpType.TrimEnd('?').StartsWith(
                            bindingName + "<",
                            StringComparison.Ordinal));
        string? wireCSharpType = null;
        string? fromWireTemplate = null;
        if (objectModelWireType is not null)
        {
            var nullableModel = csharpType.EndsWith(
                "?",
                StringComparison.Ordinal);
            wireCSharpType = nullableModel
                ? Nullable(objectModelWireType)
                : objectModelWireType;
            var publicModelType = csharpType.TrimEnd('?');
            var qualifiedPublicModelType =
                "global::" + generation.Namespace + "." + publicModelType;
            var converterArguments = string.Concat(
                objectModelArgumentMappings.Select((argument, index) =>
                {
                    var value = "genericValue" + index;
                    return ", "
                           + (argument.RequiresWireConversion
                               ? value
                                 + " => "
                                 + argument.ConvertFromWire(
                                     value,
                                     "__INVOKER__")
                               : "static " + value + " => " + value);
                }));
            fromWireTemplate = nullableModel
                ? "__VALUE__ is { } modelValue ? "
                  + qualifiedPublicModelType
                  + ".FromWire(modelValue, __INVOKER__"
                  + converterArguments
                  + ") : null"
                : qualifiedPublicModelType
                  + ".FromWire(__VALUE__, __INVOKER__"
                  + converterArguments
                  + ")";
        }
        else if (referenceAliasMapping is { RequiresWireConversion: true } aliasWire)
        {
            wireCSharpType = optional
                ? Nullable(aliasWire.EffectiveWireCSharpType)
                : aliasWire.EffectiveWireCSharpType;
            fromWireTemplate = optional
                ? "__VALUE__ is { } aliasValue ? "
                  + aliasWire.ConvertFromWire("aliasValue", "__INVOKER__")
                  + " : default("
                  + csharpType
                  + ")"
                : aliasWire.FromWireTemplate;
        }
        else if (unionCandidateMappings.Length > 1
            && unionCandidateMappings.Any(
                static candidate => candidate.RequiresWireConversion))
        {
            var apiTypes = unionCandidateMappings
                .Select(static candidate => candidate.CSharpType)
                .Distinct(StringComparer.Ordinal)
                .ToArray();
            var wireTypes = unionCandidateMappings
                .Select(static candidate => candidate.EffectiveWireCSharpType)
                .Distinct(StringComparer.Ordinal)
                .ToArray();
            if (apiTypes.Length == unionCandidateMappings.Length
                && wireTypes.Length == unionCandidateMappings.Length)
            {
                var apiUnion = JavaScriptUnionType(apiTypes);
                var wireUnion = JavaScriptUnionType(wireTypes);
                wireCSharpType = unionIsNullable || optional
                    ? Nullable(wireUnion)
                    : wireUnion;
                var switchValue = unionIsNullable || optional
                    ? "unionValue"
                    : "__VALUE__";
                var branches = string.Join(
                    ", ",
                    unionCandidateMappings.Select(candidate =>
                    {
                        var branch = generation.NextLocal("unionBranch");
                        var converted = candidate.RequiresWireConversion
                            ? candidate.ConvertFromWire(
                                branch,
                                "__INVOKER__")
                            : branch;
                        return candidate.EffectiveWirePatternCSharpType
                               + " "
                               + branch
                               + " => new "
                               + apiUnion
                               + "("
                               + converted
                               + ")";
                    }));
                var conversion = switchValue
                                 + ".Value switch { "
                                 + branches
                                 + ", _ => throw new global::System.Text.Json.JsonException("
                                 + Literal($"The JavaScript value does not match {apiUnion}.")
                                 + ") }";
                fromWireTemplate = unionIsNullable || optional
                    ? "__VALUE__ is { } unionValue ? "
                      + conversion
                      + " : default("
                      + Nullable(apiUnion)
                      + ")"
                    : conversion;
            }
            else
            {
                generation.Context.ReportDiagnostic(Diagnostic.Create(
                    UnsupportedType,
                    Location.None,
                    "union of retained JavaScript values with ambiguous handle branches",
                    member));
                csharpType = "global::System.Text.Json.JsonElement";
                isBinding = false;
                isObjectReference = false;
                isFunctionReference = false;
            }
        }
        else if (unionConcreteMapping is { RequiresWireConversion: true } concreteWire)
        {
            wireCSharpType = unionIsNullable || optional
                ? Nullable(concreteWire.EffectiveWireCSharpType)
                : concreteWire.EffectiveWireCSharpType;
            fromWireTemplate = unionIsNullable || optional
                ? "__VALUE__ is { } concreteValue ? "
                  + concreteWire.ConvertFromWire(
                      "concreteValue",
                      "__INVOKER__")
                  + " : default("
                  + csharpType
                  + ")"
                : concreteWire.FromWireTemplate;
        }
        else if (isBinding)
        {
            wireCSharpType =
                "global::WebScene.JavaScript.Interop.JavaScriptObjectReference"
                + (csharpType.EndsWith("?", StringComparison.Ordinal) ? "?" : "");
            fromWireTemplate = csharpType.EndsWith("?", StringComparison.Ordinal)
                ? "__VALUE__ is { } reference ? new "
                  + csharpType.TrimEnd('?')
                  + "(__INVOKER__, reference) : null"
                : "new " + csharpType + "(__INVOKER__, __VALUE__)";
        }
        else if (isFunctionReference)
        {
            wireCSharpType =
                "global::WebScene.JavaScript.Interop.JavaScriptObjectReference"
                + (csharpType.EndsWith("?", StringComparison.Ordinal) ? "?" : "");
            fromWireTemplate = csharpType.EndsWith("?", StringComparison.Ordinal)
                ? "__VALUE__ is { } reference ? new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(__INVOKER__, reference) : null"
                : "new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(__INVOKER__, __VALUE__)";
        }
        return new TypeMapping(
            csharpType,
            isBinding,
            IsObjectReference: isObjectReference || isFunctionReference,
            IsFunctionReference: isFunctionReference,
            WireCSharpType: wireCSharpType,
            FromWireTemplate: fromWireTemplate);
    }

    private static string JavaScriptUnionType(IReadOnlyList<string> types)
        => "global::WebScene.JavaScript.Interop.JavaScriptUnion<"
           + (types.Count <= 16
               ? string.Join(", ", types)
               : TupleType(types))
           + ">";

    private static TypeMapping MapArrayType(
        GenerationContext generation,
        JsonElement type,
        bool optional,
        string member)
    {
        var element = MapType(
            generation,
            type.GetProperty("element"),
            optional: false,
            member);
        var csharpType =
            $"global::System.Collections.Generic.IReadOnlyList<{element.CSharpType}>";
        if (optional)
        {
            csharpType = Nullable(csharpType);
        }
        if (!element.RequiresWireConversion)
        {
            return new TypeMapping(csharpType, IsBinding: false);
        }

        var wireType =
            $"global::System.Collections.Generic.IReadOnlyList<{element.EffectiveWireCSharpType}>";
        if (optional)
        {
            wireType = Nullable(wireType);
        }
        var projectionValue = optional ? "arrayValue" : "__VALUE__";
        var projection =
            "global::System.Linq.Enumerable.ToArray("
            + "global::System.Linq.Enumerable.Select(" + projectionValue + ", item => "
            + element.ConvertFromWire("item", "__INVOKER__")
            + "))";
        return new TypeMapping(
            csharpType,
            IsBinding: false,
            WireCSharpType: wireType,
            FromWireTemplate: optional
                ? "__VALUE__ is { } arrayValue ? " + projection + " : null"
                : projection);
    }

    private static TypeMapping MapRecordType(
        GenerationContext generation,
        JsonElement arguments,
        bool optional,
        string member)
    {
        var key = MapType(
            generation,
            arguments[0],
            optional: false,
            member);
        var value = MapType(
            generation,
            arguments[1],
            optional: false,
            member);
        var csharpType =
            "global::System.Collections.Generic.IReadOnlyDictionary<"
            + key.CSharpType
            + ", "
            + value.CSharpType
            + ">";
        if (optional)
        {
            csharpType = Nullable(csharpType);
        }
        if (!key.RequiresWireConversion && !value.RequiresWireConversion)
        {
            return new TypeMapping(csharpType, IsBinding: false);
        }

        var wireType =
            "global::System.Collections.Generic.IReadOnlyDictionary<"
            + key.EffectiveWireCSharpType
            + ", "
            + value.EffectiveWireCSharpType
            + ">";
        if (optional)
        {
            wireType = Nullable(wireType);
        }
        var dictionaryValue = optional ? "dictionaryValue" : "__VALUE__";
        var projection =
            "global::System.Linq.Enumerable.ToDictionary("
            + dictionaryValue
            + ", pair => "
            + key.ConvertFromWire("pair.Key", "__INVOKER__")
            + ", pair => "
            + value.ConvertFromWire("pair.Value", "__INVOKER__")
            + ")";
        return new TypeMapping(
            csharpType,
            IsBinding: false,
            WireCSharpType: wireType,
            FromWireTemplate: optional
                ? "__VALUE__ is { } dictionaryValue ? "
                  + projection
                  + " : null"
                : projection);
    }

    private static TypeMapping MapInlineObjectType(
        GenerationContext generation,
        JsonElement type,
        bool optional,
        string member)
    {
        var structural = generation.GetOrAddStructuralModel(type, member);
        var genericSuffix = structural.TypeParameters.Length == 0
            ? string.Empty
            : "<" + string.Join(
                ", ",
                structural.TypeParameters.Select(EscapeIdentifier)) + ">";
        var publicType = structural.Name + genericSuffix;
        var wireType = structural.Name + "Wire" + genericSuffix;
        if (optional)
        {
            publicType = Nullable(publicType);
            wireType = Nullable(wireType);
        }
        return new TypeMapping(
            publicType,
            IsBinding: false,
            WireCSharpType: wireType,
            FromWireTemplate: optional
                ? "__VALUE__ is { } objectValue ? "
                  + "global::"
                  + generation.Namespace
                  + "."
                  + structural.Name
                  + genericSuffix
                  + ".FromWire(objectValue, __INVOKER__) : null"
                : "global::"
                  + generation.Namespace
                  + "."
                  + structural.Name
                  + genericSuffix
                  + ".FromWire(__VALUE__, __INVOKER__)");
    }

    private static TypeMapping MapTupleType(
        GenerationContext generation,
        JsonElement type,
        bool optional,
        string member)
    {
        var elements = type.GetProperty("elements")
            .EnumerateArray()
            .Select(element => MapType(
                generation,
                element,
                optional: false,
                member))
            .ToArray();
        var csharpType = TupleType(
            elements.Select(static element => element.CSharpType).ToArray());
        var wirePatternType = ExplicitTupleType(elements
            .Select(static element => element.EffectiveWireCSharpType)
            .ToArray());
        if (optional)
        {
            csharpType = Nullable(csharpType);
        }
        if (!elements.Any(static element => element.RequiresWireConversion))
        {
            return new TypeMapping(
                csharpType,
                IsBinding: false,
                WirePatternCSharpType: wirePatternType);
        }

        var wireType = TupleType(elements
            .Select(static element => element.EffectiveWireCSharpType)
            .ToArray());
        if (optional)
        {
            wireType = Nullable(wireType);
        }
        var convertedElements = elements
            .Select((element, index) => element.ConvertFromWire(
                "__TUPLE__.Item" + (index + 1),
                "__INVOKER__"))
            .ToArray();
        var conversion = TupleExpression(convertedElements).Replace(
            "__TUPLE__",
            optional ? "tupleValue" : "__VALUE__");
        return new TypeMapping(
            csharpType,
            IsBinding: false,
            WireCSharpType: wireType,
            FromWireTemplate: optional
                ? "__VALUE__ is { } tupleValue ? " + conversion + " : null"
                : conversion,
            WirePatternCSharpType: wirePatternType);
    }

    private static string TupleType(IReadOnlyList<string> elements)
        => elements.Count switch
        {
            0 => "global::System.ValueTuple",
            1 => $"global::System.ValueTuple<{elements[0]}>",
            _ => "(" + string.Join(", ", elements) + ")"
        };

    private static string ExplicitTupleType(IReadOnlyList<string> elements)
    {
        if (elements.Count == 0)
        {
            return "global::System.ValueTuple";
        }
        if (elements.Count <= 7)
        {
            return "global::System.ValueTuple<"
                   + string.Join(", ", elements)
                   + ">";
        }
        return "global::System.ValueTuple<"
               + string.Join(", ", elements.Take(7))
               + ", "
               + ExplicitTupleType(elements.Skip(7).ToArray())
               + ">";
    }

    private static string TupleExpression(IReadOnlyList<string> elements)
        => elements.Count switch
        {
            0 => "default(global::System.ValueTuple)",
            1 => $"global::System.ValueTuple.Create({elements[0]})",
            _ => "(" + string.Join(", ", elements) + ")"
        };

    private static TypeMapping MapOptionalParameterType(
        GenerationContext generation,
        JsonElement type,
        bool optional,
        string member)
        => MapType(
            generation,
            OptionalParameterValueType(type, optional),
            optional: false,
            member);

    private static JsonElement OptionalParameterValueType(
        JsonElement type,
        bool optional)
    {
        if (!optional || Kind(type) != "union")
        {
            return type;
        }

        var types = type.GetProperty("types").EnumerateArray().ToArray();
        if (!types.Any(static candidate => Kind(candidate) == "undefined"))
        {
            return type;
        }

        var values = types
            .Where(static candidate => Kind(candidate) != "undefined")
            .ToArray();
        if (values.Length == 1)
        {
            return values[0];
        }
        using var normalized = JsonDocument.Parse(
            "{\"kind\":\"union\",\"types\":["
            + string.Join(
                ",",
                values.Select(static value => value.GetRawText()))
            + "]}");
        return normalized.RootElement.Clone();
    }

    private static TypeMapping MapOptionalModelProperty(TypeMapping value)
    {
        var publicType =
            "global::WebScene.JavaScript.Interop.JavaScriptOptional<"
            + value.CSharpType
            + ">";
        if (!value.RequiresWireConversion)
        {
            return new TypeMapping(publicType, IsBinding: false);
        }

        var wireType =
            "global::WebScene.JavaScript.Interop.JavaScriptOptional<"
            + value.EffectiveWireCSharpType
            + ">";
        return new TypeMapping(
            publicType,
            IsBinding: false,
            WireCSharpType: wireType,
            FromWireTemplate:
                "__VALUE__ is { HasValue: true, Value: var optionalValue } ? "
                + "new "
                + publicType
                + "("
                + value.ConvertFromWire(
                    "optionalValue!",
                    "__INVOKER__")
                + ") : default");
    }

    private static string? ResolveReference(
        GenerationContext generation,
        JsonElement type)
    {
        var name = type.GetProperty("name").GetString()!;
        var qualifiedName = type.TryGetProperty("qualifiedName", out var qualified)
                            && qualified.ValueKind == JsonValueKind.String
            ? qualified.GetString()
            : null;
        if (name is "Date")
        {
            return "global::System.DateTimeOffset";
        }
        if (name is "Function")
        {
            return "global::WebScene.JavaScript.Interop.JavaScriptFunctionReference";
        }
        if (name is "HTMLElement" or "Element" or "EventTarget" or "Window"
            or "Document" or "Node" or "Event" or "ClipboardEvent"
            or "KeyboardEvent" or "MouseEvent" or "Worker" or "RegExp"
            or "ArrayBuffer" or "SharedArrayBuffer" or "DataView"
            or "Int8Array" or "Uint8Array" or "Uint8ClampedArray"
            or "Int16Array" or "Uint16Array" or "Int32Array" or "Uint32Array"
            or "Float32Array" or "Float64Array" or "BigInt64Array"
            or "BigUint64Array")
        {
            return "global::WebScene.JavaScript.Interop.JavaScriptObjectReference";
        }
        if (name is "Object")
        {
            return "global::System.Text.Json.JsonElement";
        }
        if (name is "Nominal" or "Brand" or "Readonly" or "Partial" or "Required"
            && type.TryGetProperty("typeArguments", out var transparentArguments)
            && transparentArguments.GetArrayLength() > 0)
        {
            return MapType(
                generation,
                transparentArguments[0],
                optional: false,
                $"type argument for '{name}'").CSharpType;
        }
        if (name is "Map" or "ReadonlyMap")
        {
            return "global::WebScene.JavaScript.Interop.JavaScriptObjectReference";
        }
        if (name is "Record"
            && type.TryGetProperty("typeArguments", out var dictionaryArguments)
            && dictionaryArguments.GetArrayLength() >= 2)
        {
            var key = MapType(
                generation,
                dictionaryArguments[0],
                optional: false,
                $"key for '{name}'").CSharpType;
            var value = MapType(
                generation,
                dictionaryArguments[1],
                optional: false,
                $"value for '{name}'").CSharpType;
            return $"global::System.Collections.Generic.IReadOnlyDictionary<{key}, {value}>";
        }
        if (name is "Set" or "ReadonlySet")
        {
            return "global::WebScene.JavaScript.Interop.JavaScriptObjectReference";
        }
        if (qualifiedName is not null
            && generation.BindingNames.TryGetValue(qualifiedName, out var binding))
        {
            return WithTypeArguments(generation, type, binding);
        }
        if (generation.BindingNames.TryGetValue(name, out binding))
        {
            return WithTypeArguments(generation, type, binding);
        }
        if (qualifiedName is not null
            && generation.ModelNames.TryGetValue(qualifiedName, out var model))
        {
            return WithTypeArguments(generation, type, model);
        }
        if (generation.ModelNames.TryGetValue(name, out model))
        {
            return WithTypeArguments(generation, type, model);
        }
        if (qualifiedName is not null
            && generation.AdapterNames.TryGetValue(qualifiedName, out var adapter))
        {
            return WithTypeArguments(generation, type, adapter);
        }
        if (generation.AdapterNames.TryGetValue(name, out adapter))
        {
            return WithTypeArguments(generation, type, adapter);
        }
        if (generation.TypeMappings.TryGetValue(name, out var mapping))
        {
            return mapping;
        }
        if (qualifiedName is not null
            && generation.TypeMappings.TryGetValue(qualifiedName, out mapping))
        {
            return mapping;
        }
        if (type.TryGetProperty("display", out var display)
            && generation.TypeMappings.TryGetValue(
                display.GetString()!,
                out mapping))
        {
            return mapping;
        }
        var sourceName = qualifiedName ?? name;
        if (TryGetType(generation.Types, sourceName, name, out var sourceType))
        {
            if (sourceType.TryGetProperty("callSignatures", out var callSignatures)
                && callSignatures.GetArrayLength() > 0
                && sourceType.GetProperty("methods").GetArrayLength() == 0
                && sourceType.GetProperty("properties").GetArrayLength() == 0)
            {
                return "global::WebScene.JavaScript.Interop.JavaScriptFunctionReference";
            }
            if (Kind(sourceType) == "typeAlias"
                && sourceType.TryGetProperty("aliasTarget", out var aliasTarget))
            {
                return MapType(
                    generation,
                    aliasTarget,
                    optional: false,
                    $"alias '{sourceName}'").CSharpType;
            }
        }
        return null;
    }

    private static string? ResolveUnion(
        GenerationContext generation,
        JsonElement type,
        string member,
        bool ignoreUndefinedNullability = false)
    {
        var types = FlattenUnionTypes(type).ToArray();
        var candidates = types
            .Where(candidate => Kind(candidate) is not ("undefined" or "null"))
            .ToArray();
        var nullable = types.Any(candidate =>
            Kind(candidate) == "null"
            || !ignoreUndefinedNullability && Kind(candidate) == "undefined");
        if (candidates.Length == 0
            && types.Length > 0
            && types.All(candidate =>
                Kind(candidate) is "null" or "undefined"))
        {
            return "global::WebScene.JavaScript.Interop.JavaScriptNullish";
        }
        if (candidates.Length == 1)
        {
            var mapped = MapType(
                generation,
                candidates[0],
                optional: false,
                member).CSharpType;
            return nullable ? Nullable(mapped) : mapped;
        }
        if (candidates.Length > 0
            && candidates.All(candidate =>
                Kind(candidate) == "literal"
                && candidate.GetProperty("value").ValueKind == JsonValueKind.String))
        {
            return nullable ? "string?" : "string";
        }
        if (candidates.Length > 1)
        {
            var mappedCandidates = candidates
                .Select(candidate => MapType(
                    generation,
                    candidate,
                    optional: false,
                    member).CSharpType)
                .ToArray();
            nullable = nullable
                       || mappedCandidates.Any(
                           static candidate =>
                               candidate.EndsWith("?", StringComparison.Ordinal));
            var mapped = mappedCandidates
                .Select(static candidate => candidate.TrimEnd('?'))
                .Distinct(StringComparer.Ordinal)
                .ToArray();
            if (mapped.Length == 1)
            {
                return nullable ? Nullable(mapped[0]) : mapped[0];
            }
            var union = JavaScriptUnionType(mapped);
            return nullable ? union + "?" : union;
        }
        return null;
    }

    private static IEnumerable<JsonElement> FlattenUnionTypes(JsonElement type)
    {
        var kind = Kind(type);
        if (kind == "promise")
        {
            foreach (var candidate in FlattenUnionTypes(
                         type.GetProperty("result")))
            {
                yield return candidate;
            }
            yield break;
        }
        if (kind == "union")
        {
            foreach (var nested in type.GetProperty("types").EnumerateArray())
            {
                foreach (var candidate in FlattenUnionTypes(nested))
                {
                    yield return candidate;
                }
            }
            yield break;
        }
        yield return type;
    }

    private static string? ResolveIntersection(
        GenerationContext generation,
        JsonElement type,
        string member)
    {
        foreach (var candidate in type.GetProperty("types").EnumerateArray())
        {
            if (Kind(candidate) is "string" or "number" or "boolean" or "bigint"
                or "typeParameter"
                or "array" or "tuple" or "reference")
            {
                return MapType(
                    generation,
                    candidate,
                    optional: false,
                    member).CSharpType;
            }
        }
        return null;
    }

    private static string ResolveTuple(
        GenerationContext generation,
        JsonElement type,
        string member)
    {
        var elements = type.GetProperty("elements")
            .EnumerateArray()
            .Select(element => MapType(
                generation,
                element,
                optional: false,
                member).CSharpType)
            .ToArray();
        if (elements.Length == 0)
        {
            return "global::System.ValueTuple";
        }
        if (elements.Length <= 7)
        {
            return "(" + string.Join(", ", elements) + ")";
        }
        return "global::System.Text.Json.JsonElement";
    }

    private static string ResolveInlineObject(
        GenerationContext generation,
        JsonElement type,
        string member)
    {
        var structural = generation.GetOrAddStructuralModel(type, member);
        if (structural.TypeParameters.Length == 0)
        {
            return structural.Name;
        }
        return structural.Name + "<" + string.Join(
            ", ",
            structural.TypeParameters.Select(EscapeIdentifier)) + ">";
    }

    private static bool IsPromiseLikeReturn(JsonElement type)
    {
        if (Kind(type) == "promise")
        {
            return true;
        }
        return Kind(type) == "union"
               && type.GetProperty("types").EnumerateArray().Any(IsPromiseLikeReturn);
    }

    private static bool IsVoidLikeReturn(JsonElement type)
    {
        var kind = Kind(type);
        if (kind is "void" or "undefined" or "null")
        {
            return true;
        }
        if (kind == "promise")
        {
            return IsVoidLikeReturn(type.GetProperty("result"));
        }
        return kind == "union"
               && type.GetProperty("types").EnumerateArray().All(IsVoidLikeReturn);
    }

    private static string WithTypeArguments(
        GenerationContext generation,
        JsonElement type,
        string name)
    {
        if (!type.TryGetProperty("typeArguments", out var arguments)
            || arguments.GetArrayLength() == 0)
        {
            var sourceName = type.TryGetProperty("qualifiedName", out var qualifiedName)
                ? qualifiedName.GetString()
                : type.TryGetProperty("name", out var simpleName)
                    ? simpleName.GetString()
                    : null;
            if (sourceName is not null
                && TryGetType(
                    generation.Types,
                    sourceName,
                    LastSegment(sourceName),
                    out var sourceType)
                && sourceType.TryGetProperty("typeParameters", out var parameters)
                && parameters.GetArrayLength() > 0)
            {
                return name + "<" + string.Join(
                    ", ",
                    Enumerable.Repeat(
                        "global::System.Text.Json.JsonElement",
                        parameters.GetArrayLength())) + ">";
            }
            return name;
        }
        return name + "<" + string.Join(
            ", ",
            arguments.EnumerateArray().Select(argument => MapType(
                generation,
                argument,
                optional: false,
                $"generic argument for '{name}'").CSharpType)) + ">";
    }

    private static string ArgumentArray(IEnumerable<Parameter> parameters)
        => "new global::WebScene.JavaScript.Interop.JavaScriptArgument[] { "
           + string.Join(
               ", ",
               parameters.Select(parameter =>
                   parameter.Optional
                       ? "@" + parameter.Name + ".ToArgument()"
                       : parameter.Rest
                           ? "global::WebScene.JavaScript.Interop.JavaScriptArgument.FromRest(@"
                             + parameter.Name + ")"
                       : "global::WebScene.JavaScript.Interop.JavaScriptArgument.From(@"
                         + parameter.Name
                         + (parameter.IsBinding && !parameter.IsNullable
                             ? ".__WebSceneReference)"
                             : ")")))
           + " }";

    private static bool CanEmitBinaryType(
        GenerationContext generation,
        JsonElement type)
    {
        var kind = Kind(type);
        if (kind is "string" or "number" or "boolean" or "null"
            or "undefined")
        {
            return true;
        }
        if (kind == "literal")
        {
            return type.GetProperty("value").ValueKind is
                JsonValueKind.String or JsonValueKind.Number
                or JsonValueKind.True or JsonValueKind.False;
        }
        if (kind == "array")
        {
            return CanEmitBinaryType(generation, type.GetProperty("element"));
        }
        if (kind == "promise")
        {
            return CanEmitBinaryType(generation, type.GetProperty("result"));
        }
        if (kind == "inlineObject")
        {
            var structural = generation.GetOrAddStructuralModel(
                type,
                "binary inline object");
            return structural.TypeParameters.Length == 0
                   && type.GetProperty("properties").EnumerateArray().All(
                       property => CanEmitBinaryType(
                           generation,
                           property.GetProperty("type")))
                   && (!type.TryGetProperty(
                           "indexSignatures",
                           out var inlineIndexes)
                       || inlineIndexes.GetArrayLength() == 0);
        }
        if (kind == "union")
        {
            return TryGetBinaryUnionPayloadType(type, out var payload)
                   && CanEmitBinaryType(generation, payload);
        }
        if (kind != "reference")
        {
            return false;
        }

        var mapping = MapType(generation, type, optional: false, "binary codec");
        if (mapping.IsBinding
            || mapping.IsObjectReference
            || mapping.IsFunctionReference)
        {
            return true;
        }
        var shortName = type.GetProperty("name").GetString()!;
        var sourceName = type.TryGetProperty("qualifiedName", out var qualified)
            ? qualified.GetString()!
            : shortName;
        if (!generation.ModelNames.ContainsKey(sourceName)
            && !generation.ModelNames.ContainsKey(shortName))
        {
            return false;
        }
        return TryGetType(generation.Types, sourceName, shortName, out var sourceType)
               && IsObjectModelType(sourceType)
               && (!sourceType.TryGetProperty("typeParameters", out var parameters)
                   || parameters.GetArrayLength() == 0)
               && sourceType.GetProperty("properties").EnumerateArray().All(
                   property => CanEmitBinaryType(
                       generation,
                       property.GetProperty("type")))
               && (!sourceType.TryGetProperty("indexSignatures", out var indexes)
                   || indexes.GetArrayLength() == 0);
    }

    private static string EmitBinaryWriteValue(
        StringBuilder source,
        GenerationContext generation,
        JsonElement type,
        string valueExpression,
        string indent)
    {
        var kind = Kind(type);
        var result = generation.NextLocal("binaryValue");
        switch (kind)
        {
            case "string":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = writer.WriteString(")
                    .Append(valueExpression).AppendLine(");");
                return result;
            case "number":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = writer.WriteNumber(")
                    .Append(valueExpression).AppendLine(");");
                return result;
            case "boolean":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = writer.WriteBoolean(")
                    .Append(valueExpression).AppendLine(");");
                return result;
            case "null":
                source.Append(indent).Append("var ").Append(result)
                    .AppendLine(" = writer.WriteNull();");
                return result;
            case "undefined":
                source.Append(indent).Append("var ").Append(result)
                    .AppendLine(" = writer.WriteUndefined();");
                return result;
            case "literal":
                return EmitBinaryWriteValue(
                    source,
                    generation,
                    LiteralPrimitiveType(type),
                    valueExpression,
                    indent);
            case "promise":
                return EmitBinaryWriteValue(
                    source,
                    generation,
                    type.GetProperty("result"),
                    valueExpression,
                    indent);
            case "array":
            {
                var index = generation.NextLocal("binaryIndex");
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = writer.BeginArray(").Append(valueExpression)
                    .AppendLine(".Count);")
                    .Append(indent).Append("for (var ").Append(index)
                    .Append(" = 0; ").Append(index).Append(" < ")
                    .Append(valueExpression).Append(".Count; ").Append(index)
                    .AppendLine("++)")
                    .Append(indent).AppendLine("{");
                var item = EmitBinaryWriteValue(
                    source,
                    generation,
                    type.GetProperty("element"),
                    valueExpression + "[" + index + "]",
                    indent + "    ");
                source.Append(indent).Append("    writer.SetArrayItem(")
                    .Append(result).Append(", ").Append(index).Append(", ")
                    .Append(item).AppendLine(");")
                    .Append(indent).AppendLine("}");
                return result;
            }
            case "inlineObject":
            {
                var mapping = MapType(
                    generation,
                    type,
                    optional: false,
                    "binary inline object");
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = ").Append(
                        QualifyGeneratedBinaryType(
                            generation,
                            mapping.NonNullableCSharpType))
                    .Append(".__WebSceneWriteBinary(ref writer, ")
                    .Append(valueExpression).AppendLine(");");
                return result;
            }
            case "union":
            {
                if (!TryGetBinaryUnionPayloadType(type, out var payload))
                {
                    throw new InvalidOperationException(
                        "Unsupported generated binary union.");
                }
                var nullable = FlattenUnionTypes(type).Any(
                    static candidate =>
                        Kind(candidate) is "null" or "undefined");
                if (!nullable)
                {
                    return EmitBinaryWriteValue(
                        source,
                        generation,
                        payload,
                        valueExpression,
                        indent);
                }
                source.Append(indent).Append("uint ").Append(result)
                    .AppendLine(";")
                    .Append(indent).Append("if (").Append(valueExpression)
                    .AppendLine(" is null)")
                    .Append(indent).AppendLine("{")
                    .Append(indent).Append("    ").Append(result)
                    .AppendLine(" = writer.WriteNull();")
                    .Append(indent).AppendLine("}")
                    .Append(indent).AppendLine("else")
                    .Append(indent).AppendLine("{");
                var mapping = MapType(
                    generation,
                    payload,
                    optional: false,
                    "binary union");
                var concreteExpression = IsNonNullableValueType(
                    mapping.CSharpType)
                    ? valueExpression + ".Value"
                    : valueExpression;
                var child = EmitBinaryWriteValue(
                    source,
                    generation,
                    payload,
                    concreteExpression,
                    indent + "    ");
                source.Append(indent).Append("    ").Append(result)
                    .Append(" = ").Append(child).AppendLine(";")
                    .Append(indent).AppendLine("}");
                return result;
            }
            case "reference":
            {
                var mapping = MapType(
                    generation,
                    type,
                    optional: false,
                    "binary reference");
                if (mapping.IsBinding)
                {
                    source.Append(indent).Append("var ").Append(result)
                        .Append(" = writer.WriteHandle(")
                        .Append(valueExpression)
                        .AppendLine(".__WebSceneReference);");
                }
                else if (mapping.IsFunctionReference)
                {
                    source.Append(indent).Append("var ").Append(result)
                        .Append(" = writer.WriteHandle(")
                        .Append(valueExpression).AppendLine(".Reference);");
                }
                else if (mapping.IsObjectReference)
                {
                    source.Append(indent).Append("var ").Append(result)
                        .Append(" = writer.WriteHandle(")
                        .Append(valueExpression).AppendLine(");");
                }
                else
                {
                    source.Append(indent).Append("var ").Append(result)
                        .Append(" = ").Append(
                            QualifyGeneratedBinaryType(
                                generation,
                                mapping.NonNullableCSharpType))
                        .Append(".__WebSceneWriteBinary(ref writer, ")
                        .Append(valueExpression).AppendLine(");");
                }
                return result;
            }
            default:
                throw new InvalidOperationException(
                    $"Unsupported generated binary type '{kind}'.");
        }
    }

    private static string EmitBinaryReadValue(
        StringBuilder source,
        GenerationContext generation,
        JsonElement type,
        string valueExpression,
        string invokerExpression,
        string indent)
    {
        var kind = Kind(type);
        var result = generation.NextLocal("binaryResult");
        switch (kind)
        {
            case "string":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = ").Append(valueExpression)
                    .AppendLine(".GetString();");
                return result;
            case "number":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = ").Append(valueExpression)
                    .AppendLine(".GetNumber();");
                return result;
            case "boolean":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = ").Append(valueExpression)
                    .AppendLine(".GetBoolean();");
                return result;
            case "null":
            case "undefined":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = ").Append(valueExpression)
                    .AppendLine(".Kind == global::WebScene.JavaScript.Interop.JavaScriptBinaryValueKind.Undefined")
                    .Append(indent)
                    .AppendLine("    ? global::WebScene.JavaScript.Interop.JavaScriptNullish.Undefined")
                    .Append(indent)
                    .AppendLine("    : global::WebScene.JavaScript.Interop.JavaScriptNullish.Null;");
                return result;
            case "literal":
                return EmitBinaryReadValue(
                    source,
                    generation,
                    LiteralPrimitiveType(type),
                    valueExpression,
                    invokerExpression,
                    indent);
            case "promise":
                return EmitBinaryReadValue(
                    source,
                    generation,
                    type.GetProperty("result"),
                    valueExpression,
                    invokerExpression,
                    indent);
            case "array":
            {
                var elementType = type.GetProperty("element");
                var elementMapping = MapType(
                    generation,
                    elementType,
                    optional: false,
                    "binary array");
                var index = generation.NextLocal("binaryIndex");
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = new ").Append(elementMapping.CSharpType)
                    .Append('[').Append(valueExpression).AppendLine(".Count];")
                    .Append(indent).Append("for (var ").Append(index)
                    .Append(" = 0; ").Append(index).Append(" < ")
                    .Append(result).Append(".Length; ").Append(index)
                    .AppendLine("++)")
                    .Append(indent).AppendLine("{");
                var child = EmitBinaryReadValue(
                    source,
                    generation,
                    elementType,
                    valueExpression + ".GetArrayItem(" + index + ")",
                    invokerExpression,
                    indent + "    ");
                source.Append(indent).Append("    ").Append(result)
                    .Append('[').Append(index).Append("] = ")
                    .Append(child).AppendLine(";")
                    .Append(indent).AppendLine("}");
                return result;
            }
            case "inlineObject":
            {
                var mapping = MapType(
                    generation,
                    type,
                    optional: false,
                    "binary inline object");
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = ").Append(
                        QualifyGeneratedBinaryType(
                            generation,
                            mapping.NonNullableCSharpType))
                    .Append(".__WebSceneReadBinary(")
                    .Append(valueExpression).Append(", ")
                    .Append(invokerExpression).AppendLine(");");
                return result;
            }
            case "union":
            {
                if (!TryGetBinaryUnionPayloadType(type, out var payload))
                {
                    throw new InvalidOperationException(
                        "Unsupported generated binary union.");
                }
                var nullable = FlattenUnionTypes(type).Any(
                    static candidate =>
                        Kind(candidate) is "null" or "undefined");
                if (!nullable)
                {
                    return EmitBinaryReadValue(
                        source,
                        generation,
                        payload,
                        valueExpression,
                        invokerExpression,
                        indent);
                }
                var mapping = MapType(
                    generation,
                    type,
                    optional: false,
                    "binary union");
                source.Append(indent).Append(mapping.CSharpType).Append(' ')
                    .Append(result).AppendLine(";")
                    .Append(indent).Append("if (").Append(valueExpression)
                    .AppendLine(".Kind is global::WebScene.JavaScript.Interop.JavaScriptBinaryValueKind.Null or global::WebScene.JavaScript.Interop.JavaScriptBinaryValueKind.Undefined)")
                    .Append(indent).AppendLine("{")
                    .Append(indent).Append("    ").Append(result)
                    .AppendLine(" = null;")
                    .Append(indent).AppendLine("}")
                    .Append(indent).AppendLine("else")
                    .Append(indent).AppendLine("{");
                var child = EmitBinaryReadValue(
                    source,
                    generation,
                    payload,
                    valueExpression,
                    invokerExpression,
                    indent + "    ");
                source.Append(indent).Append("    ").Append(result)
                    .Append(" = ").Append(child).AppendLine(";")
                    .Append(indent).AppendLine("}");
                return result;
            }
            case "reference":
            {
                var mapping = MapType(
                    generation,
                    type,
                    optional: false,
                    "binary reference");
                if (mapping.IsBinding)
                {
                    source.Append(indent).Append("var ").Append(result)
                        .Append(" = new ").Append(mapping.NonNullableCSharpType)
                        .Append('(').Append(invokerExpression).Append(", ")
                        .Append(valueExpression).AppendLine(".GetHandle());");
                }
                else if (mapping.IsFunctionReference)
                {
                    source.Append(indent).Append("var ").Append(result)
                        .Append(" = new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(")
                        .Append(invokerExpression).Append(", ")
                        .Append(valueExpression).AppendLine(".GetHandle());");
                }
                else if (mapping.IsObjectReference)
                {
                    source.Append(indent).Append("var ").Append(result)
                        .Append(" = ").Append(valueExpression)
                        .AppendLine(".GetHandle();");
                }
                else
                {
                    source.Append(indent).Append("var ").Append(result)
                        .Append(" = ").Append(
                            QualifyGeneratedBinaryType(
                                generation,
                                mapping.NonNullableCSharpType))
                        .Append(".__WebSceneReadBinary(")
                        .Append(valueExpression).Append(", ")
                        .Append(invokerExpression).AppendLine(");");
                }
                return result;
            }
            default:
                throw new InvalidOperationException(
                    $"Unsupported generated binary type '{kind}'.");
        }
    }

    private static bool TryGetBinaryUnionPayloadType(
        JsonElement type,
        out JsonElement payload)
    {
        var concrete = FlattenUnionTypes(type)
            .Where(static candidate =>
                Kind(candidate) is not ("null" or "undefined"))
            .ToArray();
        if (concrete.Length == 1)
        {
            payload = concrete[0];
            return true;
        }
        if (concrete.Length == 0)
        {
            payload = default;
            return false;
        }

        string? primitiveKind = null;
        foreach (var candidate in concrete)
        {
            var candidateKind = Kind(candidate);
            if (candidateKind == "literal")
            {
                candidateKind = candidate.GetProperty("value").ValueKind switch
                {
                    JsonValueKind.String => "string",
                    JsonValueKind.Number => "number",
                    JsonValueKind.True or JsonValueKind.False => "boolean",
                    _ => string.Empty
                };
            }
            if (candidateKind is not ("string" or "number" or "boolean")
                || primitiveKind is not null
                && !string.Equals(
                    primitiveKind,
                    candidateKind,
                    StringComparison.Ordinal))
            {
                payload = default;
                return false;
            }
            primitiveKind = candidateKind;
        }

        using var document = JsonDocument.Parse(
            "{\"kind\":\"" + primitiveKind + "\"}");
        payload = document.RootElement.Clone();
        return true;
    }

    private static bool IsNonNullableValueType(string type)
        => type is "bool" or "double" or "int" or "long"
            or "global::System.Numerics.BigInteger"
           || type.StartsWith("(", StringComparison.Ordinal)
           && !type.EndsWith("?", StringComparison.Ordinal);

    private static string QualifyGeneratedBinaryType(
        GenerationContext generation,
        string type)
        => type.StartsWith("global::", StringComparison.Ordinal)
            ? type
            : "global::" + generation.Namespace + "." + type;

    private static JsonElement LiteralPrimitiveType(JsonElement literal)
    {
        var kind = literal.GetProperty("value").ValueKind switch
        {
            JsonValueKind.String => "string",
            JsonValueKind.Number => "number",
            JsonValueKind.True or JsonValueKind.False => "boolean",
            _ => throw new InvalidOperationException(
                "Unsupported binary literal.")
        };
        using var document = JsonDocument.Parse(
            "{\"kind\":\"" + kind + "\"}");
        return document.RootElement.Clone();
    }

    private static Dictionary<string, string> ReadStringMap(
        JsonElement value,
        string property)
    {
        if (!value.TryGetProperty(property, out var map)
            || map.ValueKind != JsonValueKind.Object)
        {
            return new Dictionary<string, string>(StringComparer.Ordinal);
        }
        return map.EnumerateObject().ToDictionary(
            static item => item.Name,
            static item => item.Value.GetString()!,
            StringComparer.Ordinal);
    }

    private static void RequireSchema(JsonElement value, string path)
    {
        if (!value.TryGetProperty("schemaVersion", out var version)
            || version.GetString() != "1.0")
        {
            throw new InvalidDataException(
                $"Interop input '{path}' must use schemaVersion '1.0'.");
        }
    }

    private static string RequiredString(
        JsonElement value,
        string property,
        string path)
    {
        if (!value.TryGetProperty(property, out var result)
            || result.ValueKind != JsonValueKind.String
            || string.IsNullOrWhiteSpace(result.GetString()))
        {
            throw new InvalidDataException(
                $"Interop input '{path}' requires string property '{property}'.");
        }
        return result.GetString()!;
    }

    private static string? OptionalString(JsonElement value, string property)
        => value.TryGetProperty(property, out var result)
           && result.ValueKind == JsonValueKind.String
            ? result.GetString()
            : null;

    private static string Kind(JsonElement type)
        => type.GetProperty("kind").GetString()!;

    private static string PascalCase(string value)
    {
        var result = new StringBuilder(value.Length);
        var uppercase = true;
        foreach (var character in value)
        {
            if (!char.IsLetterOrDigit(character) && character != '_')
            {
                uppercase = true;
                continue;
            }
            result.Append(uppercase ? char.ToUpperInvariant(character) : character);
            uppercase = character == '_';
        }
        return result.Replace("_", string.Empty).ToString();
    }

    private static string Literal(string value)
        => Microsoft.CodeAnalysis.CSharp.SymbolDisplay.FormatLiteral(
            value,
            quote: true);

    private static string Nullable(string type)
        => type is "bool" or "double" or "int" or "long"
            ? type + "?"
            : type.EndsWith("?", StringComparison.Ordinal) ? type : type + "?";

    private static string LastSegment(string value)
    {
        var index = value.LastIndexOf('.');
        return index < 0 ? value : value.Substring(index + 1);
    }

    private static string Sanitize(string name)
        => new(name.Select(static character =>
            char.IsLetterOrDigit(character) ? character : '_').ToArray());

    private static bool IsStringLiteralUnion(JsonElement type)
        => Kind(type) == "union"
           && type.GetProperty("types").EnumerateArray().Any()
           && type.GetProperty("types").EnumerateArray().All(candidate =>
               Kind(candidate) == "literal"
               && candidate.GetProperty("value").ValueKind == JsonValueKind.String);

    private static bool IsObjectModelType(JsonElement type)
        => Kind(type) is "interface" or "class"
           || Kind(type) == "typeAlias"
           && type.TryGetProperty("aliasTarget", out var aliasTarget)
           && Kind(aliasTarget) == "inlineObject";

    private static bool IsValueType(string type)
        => type is "bool" or "double" or "int" or "long"
            or "global::System.Numerics.BigInteger"
           || type.StartsWith("(", StringComparison.Ordinal)
           || type.EndsWith("?", StringComparison.Ordinal);

    private static string SuggestedTypeName(string value)
        => value.Length > 1
           && value[0] == 'I'
           && char.IsUpper(value[1])
            ? value.Substring(1)
            : value;

    private static string BindingTypeName(Binding binding)
    {
        if (!binding.Root.TryGetProperty("typeParameters", out var parameters)
            || parameters.GetArrayLength() == 0)
        {
            return binding.Name;
        }
        return binding.Name + "<" + string.Join(
            ", ",
            parameters.EnumerateArray().Select(static item =>
                EscapeIdentifier(item.GetString()!))) + ">";
    }

    private static string EscapeIdentifier(string value)
    {
        var sanitized = new string(value.Select(static character =>
            char.IsLetterOrDigit(character) || character == '_'
                ? character
                : '_').ToArray());
        if (string.IsNullOrEmpty(sanitized))
        {
            return "_";
        }
        if (!char.IsLetter(sanitized[0]) && sanitized[0] != '_')
        {
            sanitized = "_" + sanitized;
        }
        return Microsoft.CodeAnalysis.CSharp.SyntaxFacts.GetKeywordKind(sanitized)
               != Microsoft.CodeAnalysis.CSharp.SyntaxKind.None
            ? "@" + sanitized
            : sanitized;
    }

    private static bool TryGetType(
        IReadOnlyDictionary<string, JsonElement> types,
        string qualifiedName,
        string shortName,
        out JsonElement type)
    {
        if (types.TryGetValue(qualifiedName, out type))
        {
            return true;
        }
        var matches = types
            .Where(pair => string.Equals(
                pair.Value.GetProperty("name").GetString(),
                shortName,
                StringComparison.Ordinal))
            .Select(static pair => pair.Value)
            .Take(2)
            .ToArray();
        if (matches.Length == 1)
        {
            type = matches[0];
            return true;
        }
        type = default;
        return false;
    }

    private readonly record struct InputFile(string? Path, string? Content);

    private sealed record Binding(
        string Source,
        string Name,
        JsonElement Policy,
        JsonElement Root);

    private sealed record Model(
        string Source,
        string Name,
        JsonElement Policy,
        JsonElement Type);

    private sealed record ObjectModelProperty(
        string JavaScriptName,
        string CSharpName,
        bool Optional,
        TypeMapping Mapping,
        JsonElement Type);

    private sealed record ObjectModelIndex(
        string KeyType,
        bool NumericKey,
        TypeMapping ValueMapping);

    private sealed record Adapter(
        string Source,
        string Name,
        JsonElement Policy,
        JsonElement Type);

    private sealed record AdapterMethod(
        string SourceName,
        string Name,
        JsonElement Method);

    private sealed record GlobalFunction(
        string Source,
        string GlobalName,
        string Name,
        JsonElement Policy,
        JsonElement Method);

    private sealed record GlobalProperty(
        string Source,
        string GlobalName,
        string Name,
        JsonElement Property);

    private sealed record GenerationContext(
        SourceProductionContext Context,
        string Namespace,
        IReadOnlyDictionary<string, string> BindingNames,
        IReadOnlyDictionary<string, string> ModelNames,
        IReadOnlyDictionary<string, string> AdapterNames,
        IReadOnlyDictionary<string, JsonElement> Types,
        IReadOnlyDictionary<string, string> TypeMappings)
    {
        private readonly Dictionary<string, StructuralModel> _structuralByShape =
            new(StringComparer.Ordinal);
        private int _nextLocal;

        public IReadOnlyDictionary<string, TypeMapping>?
            ActiveTypeParameterMappings { get; set; }

        public List<StructuralModel> StructuralModels { get; } = [];

        public string NextLocal(string prefix)
            => "__webScene" + prefix + _nextLocal++;

        public StructuralModel GetOrAddStructuralModel(JsonElement type, string member)
        {
            var key = type.GetRawText();
            if (_structuralByShape.TryGetValue(key, out var existing))
            {
                return existing;
            }

            var baseName = PascalCase(member) + "Shape";
            if (string.IsNullOrEmpty(baseName)
                || !char.IsLetter(baseName[0])
                && baseName[0] != '_')
            {
                baseName = "Anonymous" + baseName;
            }
            if (baseName.Length > 96)
            {
                baseName = baseName.Substring(0, 96);
            }
            var candidate = baseName;
            var suffix = 2;
            while (IsGeneratedNameUsed(candidate))
            {
                candidate = baseName + suffix++;
            }

            var parameters = new List<string>();
            CollectTypeParameters(type, parameters);
            var structural = new StructuralModel(
                candidate,
                type.Clone(),
                parameters.ToArray());
            _structuralByShape.Add(key, structural);
            StructuralModels.Add(structural);
            return structural;
        }

        private bool IsGeneratedNameUsed(string name)
            => BindingNames.Values.Contains(name, StringComparer.Ordinal)
               || ModelNames.Values.Contains(name, StringComparer.Ordinal)
               || AdapterNames.Values.Contains(name, StringComparer.Ordinal)
               || StructuralModels.Any(model =>
                   string.Equals(model.Name, name, StringComparison.Ordinal));

        private static void CollectTypeParameters(JsonElement value, List<string> result)
        {
            if (value.ValueKind == JsonValueKind.Object)
            {
                if (value.TryGetProperty("kind", out var kind)
                    && kind.GetString() == "typeParameter"
                    && value.TryGetProperty("name", out var name))
                {
                    var parameter = name.GetString()!;
                    if (!result.Contains(parameter, StringComparer.Ordinal))
                    {
                        result.Add(parameter);
                    }
                }
                foreach (var property in value.EnumerateObject())
                {
                    CollectTypeParameters(property.Value, result);
                }
            }
            else if (value.ValueKind == JsonValueKind.Array)
            {
                foreach (var item in value.EnumerateArray())
                {
                    CollectTypeParameters(item, result);
                }
            }
        }
    }

    private sealed record StructuralModel(
        string Name,
        JsonElement Type,
        string[] TypeParameters);

    private readonly record struct TypeMapping(
        string CSharpType,
        bool IsBinding,
        bool IsCallbackWrapper = false,
        bool IsObjectReference = false,
        bool IsFunctionReference = false,
        string? WireCSharpType = null,
        string? FromWireTemplate = null,
        string? WirePatternCSharpType = null)
    {
        public bool IsNullable => CSharpType.EndsWith("?", StringComparison.Ordinal);

        public string NonNullableCSharpType => CSharpType.TrimEnd('?');

        public bool RequiresWireConversion => FromWireTemplate is not null;

        public string EffectiveWireCSharpType => WireCSharpType ?? CSharpType;

        public string EffectiveWirePatternCSharpType
            => WirePatternCSharpType ?? EffectiveWireCSharpType;

        public string ConvertFromWire(string value, string invoker)
            => (FromWireTemplate ?? value)
                .Replace("__VALUE__", value)
                .Replace("__INVOKER__", invoker);
    }

    private readonly record struct Parameter(
        string Name,
        bool IsBinding,
        bool IsNullable,
        bool Optional,
        bool Rest);

    private readonly record struct ConstructorParameter(
        string Name,
        string CSharpType,
        bool IsBinding,
        bool IsNullable,
        bool Optional,
        bool Rest,
        TypeMapping? BinaryMapping,
        JsonElement BinaryType);

    private readonly record struct MappedParameter(
        string Name,
        TypeMapping Mapping,
        bool Optional,
        bool Rest,
        JsonElement Type);
}
