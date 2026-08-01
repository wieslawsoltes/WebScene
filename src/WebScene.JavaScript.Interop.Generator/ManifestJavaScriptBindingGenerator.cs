using System.Text;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace WebScene.JavaScript.Interop.Generator;

[Generator(LanguageNames.CSharp)]
public sealed partial class ManifestJavaScriptBindingGenerator : IIncrementalGenerator
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
        bool IsObjectReferenceProvider = false,
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
