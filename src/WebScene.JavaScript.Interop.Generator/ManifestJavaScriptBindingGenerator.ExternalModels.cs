using System.Globalization;
using System.Text;
using System.Text.Json;
using Microsoft.CodeAnalysis;

namespace WebScene.JavaScript.Interop.Generator;

public sealed partial class ManifestJavaScriptBindingGenerator
{
    private sealed record ExternalOptionalProperty(string ClrType, string? MissingExpression);

    private sealed record ExternalModelCodec(
        string Name,
        string ClrType,
        IReadOnlyList<ObjectModelProperty> Properties,
        IReadOnlyList<string>? ConstructorProperties,
        bool IsValueType);

    private static bool TryPrepareExternalCodec(
        GenerationContext generation,
        string sourceName,
        string mapping,
        out string reason)
    {
        reason = string.Empty;
        if (generation.ExternalCodecs.ContainsKey(sourceName))
        {
            return true;
        }
        if (generation.ExternalCodecFailures.TryGetValue(sourceName, out reason))
        {
            return false;
        }
        if (!generation.PreparingExternalCodecs.Add(sourceName))
        {
            reason = "recursive external contracts are not supported";
            return false;
        }
        try
        {
            if (BuildExternalCodec(generation, sourceName, mapping, out var codec, out reason))
            {
                generation.ExternalCodecs.Add(sourceName, codec!);
                return true;
            }
            generation.ExternalCodecFailures[sourceName] = reason;
            return false;
        }
        finally
        {
            generation.PreparingExternalCodecs.Remove(sourceName);
        }
    }

    private static bool BuildExternalCodec(
        GenerationContext generation,
        string sourceName,
        string mapping,
        out ExternalModelCodec? codec,
        out string reason)
    {
        codec = null;
        reason = "the TypeScript object schema was not discovered";
        if (!TryGetType(generation.Types, sourceName, LastSegment(sourceName), out var schema)
            || !IsObjectModelType(schema))
        {
            return false;
        }
        reason = "generic external contracts are not supported";
        if (schema.TryGetProperty("typeParameters", out var parameters) && parameters.GetArrayLength() > 0)
        {
            return false;
        }
        if (Kind(schema) == "typeAlias")
        {
            schema = schema.GetProperty("aliasTarget");
        }
        reason = "index signatures are not supported for external contracts";
        if (schema.TryGetProperty("indexSignatures", out var indexes) && indexes.GetArrayLength() > 0)
        {
            return false;
        }

        var metadataName = mapping.StartsWith("global::", StringComparison.Ordinal)
            ? mapping.Substring(8) : mapping;
        var symbol = generation.Compilation.GetTypeByMetadataName(metadataName);
        reason = "the CLR type must be an accessible, non-abstract, non-generic class or struct";
        if (symbol is null || symbol.TypeKind is not (TypeKind.Class or TypeKind.Struct) || symbol.IsAbstract
            || symbol.IsStatic || symbol.IsGenericType || symbol.IsRefLikeType
            || !generation.Compilation.IsSymbolAccessibleWithin(symbol, generation.Compilation.Assembly))
        {
            return false;
        }

        var renames = new Dictionary<string, string>(StringComparer.Ordinal);
        var optionalPolicies = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        if (generation.Policy.TryGetProperty("models", out var models))
        {
            foreach (var model in models.EnumerateArray())
            {
                var modelSource = OptionalString(model, "source");
                if (modelSource == sourceName || modelSource == LastSegment(sourceName))
                {
                    renames = ReadStringMap(model, "propertyMappings");
                    if (model.TryGetProperty("optionalProperties", out var optionalSettings))
                    {
                        reason = "optionalProperties must be an object keyed by TypeScript property name";
                        if (optionalSettings.ValueKind != JsonValueKind.Object) return false;
                        optionalPolicies = optionalSettings.EnumerateObject().ToDictionary(
                            item => item.Name, item => item.Value, StringComparer.Ordinal);
                    }
                    break;
                }
            }
        }
        var schemaProperties = schema.GetProperty("properties").EnumerateArray().ToArray();
        reason = "propertyMappings contains a property absent from the TypeScript schema";
        if (renames.Keys.Any(name => !schemaProperties.Any(property => property.GetProperty("name").GetString() == name)))
        {
            return false;
        }

        reason = "optionalProperties must name optional properties in the TypeScript schema";
        if (optionalPolicies.Keys.Any(name => !schemaProperties.Any(property =>
                property.GetProperty("name").GetString() == name && property.GetProperty("optional").GetBoolean())))
        {
            return false;
        }

        var properties = new List<ObjectModelProperty>();
        var symbols = new List<IPropertySymbol>();
        var usedNames = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in schemaProperties)
        {
            var jsName = property.GetProperty("name").GetString()!;
            var clrName = renames.TryGetValue(jsName, out var renamed) ? renamed : PascalCase(jsName);
            reason = $"property '{jsName}' must map to a distinct public readable CLR property '{clrName}'";
            var member = FindExternalProperty(symbol, clrName);
            if (!usedNames.Add(clrName) || member is null || member.IsStatic || member.IsIndexer
                || member.GetMethod?.DeclaredAccessibility != Accessibility.Public
                || member.ReturnsByRef || member.ReturnsByRefReadonly)
            {
                return false;
            }
            var propertyType = property.GetProperty("type");
            var ordinaryOptional = optionalPolicies.TryGetValue(jsName, out var optionalPolicy);
            if (ordinaryOptional) propertyType = OptionalParameterValueType(propertyType, optional: true);
            reason = $"property '{jsName}' has no supported ABI 3 representation";
            // Do this before mapping so recursive references cannot be accepted
            // merely because their CLR names resolve.
            if (!CanEmitBinaryType(generation, propertyType))
            {
                return false;
            }
            var optional = property.GetProperty("optional").GetBoolean() && !ordinaryOptional;
            var propertyMapping = MapOptionalParameterType(generation, propertyType, optional, sourceName + "." + jsName);
            if (optional)
            {
                propertyMapping = MapOptionalModelProperty(propertyMapping);
            }
            var expected = propertyMapping.CSharpType;
            var actual = ExternalTypeName(member.Type);
            // A required array property may use T[]: readers already create the
            // final array, while writers access it through IReadOnlyList<T>.
            var arrayCompatible = !optional && Kind(propertyType) == "array"
                && member.Type is IArrayTypeSymbol { Rank: 1 } array
                && array.NullableAnnotation != NullableAnnotation.Annotated
                && expected == "global::System.Collections.Generic.IReadOnlyList<" + ExternalTypeName(array.ElementType) + ">";
            reason = $"property '{clrName}' has CLR type '{actual}', expected '{expected}'";
            var int64Compatible = !optional && (actual == "long" && expected == "double"
                || actual == "long?" && expected == "double?");
            if (actual != expected && !arrayCompatible && !int64Compatible)
            {
                return false;
            }
            ExternalOptionalProperty? externalOptional = null;
            if (ordinaryOptional)
            {
                if (!TryReadExternalOptionalPolicy(optionalPolicy, actual, jsName, out var missing, out reason)) return false;
                externalOptional = new ExternalOptionalProperty(actual, missing);
            }
            properties.Add(new ObjectModelProperty(jsName, clrName, optional, propertyMapping, propertyType.Clone(),
                int64Compatible ? actual : null, externalOptional));
            symbols.Add(member);
        }

        var requiredMembers = ExternalMembers(symbol).Where(member =>
            member is IPropertySymbol { IsRequired: true } or IFieldSymbol { IsRequired: true }).ToArray();
        var canInitialize = symbol.InstanceConstructors.Any(ctor =>
                ctor.Parameters.Length == 0 && generation.Compilation.IsSymbolAccessibleWithin(ctor, generation.Compilation.Assembly))
            && symbols.All(property => property.SetMethod is { } setter
                && generation.Compilation.IsSymbolAccessibleWithin(setter, generation.Compilation.Assembly))
            && requiredMembers.All(member => symbols.Any(property => SymbolEqualityComparer.Default.Equals(property, member)));
        IReadOnlyList<string>? constructorProperties = null;
        if (!canInitialize || symbol.IsValueType)
        {
            var constructors = symbol.InstanceConstructors.Where(ctor =>
                generation.Compilation.IsSymbolAccessibleWithin(ctor, generation.Compilation.Assembly)
                && ctor.Parameters.Length == symbols.Count
                && ctor.Parameters.Select(parameter => parameter.Name).Distinct(StringComparer.OrdinalIgnoreCase).Count() == symbols.Count
                && ctor.Parameters.All(parameter => parameter.RefKind == RefKind.None
                    && symbols.Count(property => string.Equals(property.Name, parameter.Name, StringComparison.OrdinalIgnoreCase)
                        && SymbolEqualityComparer.IncludeNullability.Equals(property.Type, parameter.Type)) == 1)
                && (requiredMembers.Length == 0 || ctor.GetAttributes().Any(attribute =>
                    attribute.AttributeClass?.ToDisplayString() == "System.Diagnostics.CodeAnalysis.SetsRequiredMembersAttribute")))
                .ToArray();
            reason = "the CLR type needs an accessible parameterless constructor and writable properties, or one unambiguous constructor matching all mapped properties by name and type";
            if (constructors.Length != 1 && !canInitialize)
            {
                return false;
            }
            if (constructors.Length == 1)
            {
                constructorProperties = constructors[0].Parameters.Select(parameter => symbols.Single(property =>
                    string.Equals(property.Name, parameter.Name, StringComparison.OrdinalIgnoreCase)).Name).ToArray();
            }
        }
        codec = new ExternalModelCodec(
            generation.NextExternalCodecName(),
            symbol.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat), properties, constructorProperties, symbol.IsValueType);
        reason = string.Empty;
        return true;
    }

    private static bool TryReadExternalOptionalPolicy(
        JsonElement policy, string clrType, string propertyName, out string? missing, out string reason)
    {
        missing = string.Empty;
        reason = $"optionalProperties entry '{propertyName}' requires write: 'always' and read: 'reject', 'null', or 'default'";
        if (policy.ValueKind != JsonValueKind.Object
            || policy.EnumerateObject().Any(item => item.Name is not ("write" or "read" or "default"))
            || OptionalString(policy, "write") != "always") return false;
        var read = OptionalString(policy, "read");
        var hasDefault = policy.TryGetProperty("default", out var defaultValue);
        if (read != "default" && hasDefault) return false;
        if (read == "reject")
        {
            missing = null;
            return true;
        }
        if (read == "null")
        {
            reason = $"optionalProperties entry '{propertyName}' can use read: 'null' only with a nullable CLR property";
            missing = "null";
            return clrType.EndsWith("?", StringComparison.Ordinal);
        }
        if (read != "default" || !hasDefault) return false;
        reason = $"optionalProperties default for '{propertyName}' must be a compatible scalar (or null for a nullable property); Int64 defaults must be JavaScript safe integers";
        if (defaultValue.ValueKind == JsonValueKind.Null && clrType.EndsWith("?", StringComparison.Ordinal))
        {
            missing = "null";
            return true;
        }
        switch (clrType.TrimEnd('?'))
        {
            case "string" when defaultValue.ValueKind == JsonValueKind.String:
                missing = Literal(defaultValue.GetString()!);
                return true;
            case "bool" when defaultValue.ValueKind is JsonValueKind.True or JsonValueKind.False:
                missing = defaultValue.GetBoolean() ? "true" : "false";
                return true;
            case "double" when defaultValue.ValueKind == JsonValueKind.Number
                && defaultValue.TryGetDouble(out var number) && !double.IsInfinity(number) && !double.IsNaN(number):
                missing = number.ToString("R", CultureInfo.InvariantCulture) + "d";
                return true;
            case "long" when defaultValue.ValueKind == JsonValueKind.Number
                && defaultValue.TryGetInt64(out var integer) && integer >= -9007199254740991L && integer <= 9007199254740991L:
                missing = integer.ToString(CultureInfo.InvariantCulture) + "L";
                return true;
            default:
                return false;
        }
    }

    private static string EmitExternalOptionalRead(
        StringBuilder source, GenerationContext generation, ObjectModelProperty property)
    {
        var policy = property.ExternalOptional!;
        var local = generation.NextLocal("externalOptional");
        var node = generation.NextLocal("externalProperty");
        source.Append("        ").Append(policy.ClrType).Append(' ').Append(local).AppendLine(";")
            .Append("        if (value.TryGetProperty(").Append(Literal(property.JavaScriptName))
            .Append("u8, out var ").Append(node).AppendLine(")")
            .Append("            && ").Append(node)
            .AppendLine(".Kind != global::WebScene.JavaScript.Interop.JavaScriptBinaryValueKind.Undefined)")
            .AppendLine("        {");
        var child = EmitBinaryReadValue(source, generation, property.Type, node, "invoker", "            ");
        if (property.ExternalNumericType is not null) child = "__WebSceneReadInt64(" + child + ")";
        source.Append("            ").Append(local).Append(" = ").Append(child).AppendLine(";")
            .AppendLine("        }").AppendLine("        else").AppendLine("        {");
        if (policy.MissingExpression is null)
        {
            source.Append("            throw new global::System.InvalidOperationException(")
                .Append(Literal("Missing optional property '" + property.JavaScriptName
                    + "' is rejected by the external model policy.")).AppendLine(");");
        }
        else
        {
            source.Append("            ").Append(local).Append(" = ").Append(policy.MissingExpression).AppendLine(";");
        }
        source.AppendLine("        }");
        return local;
    }

    private static void EmitExternalInt64Conversions(StringBuilder source)
        => source.AppendLine("""

                private static double __WebSceneWriteInt64(long value)
                {
                    if (value < -9007199254740991L || value > 9007199254740991L)
                        throw new global::System.OverflowException("Int64 value is outside the JavaScript safe-integer range [-9007199254740991, 9007199254740991].");
                    return value;
                }

                private static double? __WebSceneWriteInt64(long? value)
                    => value.HasValue ? __WebSceneWriteInt64(value.Value) : null;

                private static long __WebSceneReadInt64(double value)
                {
                    if (value < -9007199254740991d || value > 9007199254740991d
                        || value != global::System.Math.Truncate(value))
                        throw new global::System.OverflowException("JavaScript number must be a finite integer in [-9007199254740991, 9007199254740991] to read an Int64 property.");
                    return checked((long)value);
                }

                private static long? __WebSceneReadInt64(double? value)
                    => value.HasValue ? __WebSceneReadInt64(value.Value) : null;
            """);

    private static string ExternalTypeName(ITypeSymbol symbol)
        => symbol.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat.WithMiscellaneousOptions(
            SymbolDisplayFormat.FullyQualifiedFormat.MiscellaneousOptions
            | SymbolDisplayMiscellaneousOptions.IncludeNullableReferenceTypeModifier));

    private static IEnumerable<ISymbol> ExternalMembers(INamedTypeSymbol symbol)
    {
        for (var current = symbol; current is not null; current = current.BaseType)
        {
            foreach (var member in current.GetMembers())
            {
                yield return member;
            }
        }
    }

    private static IPropertySymbol? FindExternalProperty(INamedTypeSymbol symbol, string name)
    {
        for (var current = symbol; current is not null; current = current.BaseType)
        {
            var members = current.GetMembers(name);
            if (members.Length > 0)
            {
                return members.Length == 1 ? members[0] as IPropertySymbol : null;
            }
        }
        return null;
    }
}
