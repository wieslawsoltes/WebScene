namespace WebScene.JavaScript.Interop;

/// <summary>Marks a partial class as a generated proxy for a JavaScript object.</summary>
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class JavaScriptBindingAttribute : Attribute;

/// <summary>Maps a static partial factory method to a JavaScript constructor.</summary>
[AttributeUsage(AttributeTargets.Method, Inherited = false)]
public sealed class JavaScriptConstructorAttribute(string globalName) : Attribute
{
    public string GlobalName { get; } = globalName;
}

public enum JavaScriptResult
{
    Value,
    Promise
}

/// <summary>Maps an instance partial method to a method on the referenced JavaScript object.</summary>
[AttributeUsage(AttributeTargets.Method, Inherited = false)]
public sealed class JavaScriptMethodAttribute(
    string name,
    JavaScriptResult result = JavaScriptResult.Value) : Attribute
{
    public string Name { get; } = name;

    public JavaScriptResult Result { get; } = result;
}
