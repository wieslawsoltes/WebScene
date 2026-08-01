using System.Text;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace WebScene.JavaScript.Interop.Generator;

public sealed partial class ManifestJavaScriptBindingGenerator
{
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
        var isObjectReferenceProvider = generation.AdapterNames.Values.Any(
            adapterName =>
                string.Equals(
                    csharpType.TrimEnd('?'),
                    adapterName,
                    StringComparison.Ordinal)
                || csharpType.TrimEnd('?').StartsWith(
                    adapterName + "<",
                    StringComparison.Ordinal));
        if (referenceAliasMapping is { } resolvedAlias)
        {
            isObjectReference =
                isObjectReference || resolvedAlias.IsObjectReference;
            isFunctionReference =
                isFunctionReference || resolvedAlias.IsFunctionReference;
            isObjectReferenceProvider =
                isObjectReferenceProvider
                || resolvedAlias.IsObjectReferenceProvider;
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
            IsObjectReferenceProvider: isObjectReferenceProvider,
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
            or "undefined" or "object" or "any" or "unknown"
            or "callback")
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
            case "object":
            case "any":
            case "unknown":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = writer.WriteJsonElement(")
                    .Append(valueExpression).AppendLine(");");
                return result;
            case "callback":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = writer.WriteHandle(")
                    .Append(valueExpression).AppendLine(".Reference);");
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
                else if (mapping.IsObjectReferenceProvider)
                {
                    source.Append(indent).Append("var ").Append(result)
                        .Append(" = writer.WriteHandle(")
                        .Append(valueExpression)
                        .AppendLine(".JavaScriptReference);");
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
            case "object":
            case "any":
            case "unknown":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = ").Append(valueExpression)
                    .AppendLine(".GetJsonElement();");
                return result;
            case "callback":
                source.Append(indent).Append("var ").Append(result)
                    .Append(" = new global::WebScene.JavaScript.Interop.JavaScriptFunctionReference(")
                    .Append(invokerExpression).Append(", ")
                    .Append(valueExpression).AppendLine(".GetHandle());");
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

}
