using WebScene.Core;
using WebScene.Backends.Avalonia.Native;
using JavaScript.Avalonia;

var hostType = typeof(AvaloniaBrowserHost);
if (!string.Equals(hostType.Assembly.GetName().Name, "WebScene.Backend.Avalonia", StringComparison.Ordinal))
{
    Console.Error.WriteLine($"Backend package smoke: implementation assembly was '{hostType.Assembly.GetName().Name}'.");
    return 1;
}
if (hostType.GetProperty(nameof(AvaloniaBrowserHost.Backend))?.PropertyType != typeof(IWebSceneBackendHost))
{
    Console.Error.WriteLine("Backend package smoke: AvaloniaBrowserHost does not expose IWebSceneBackendHost.");
    return 1;
}
if (typeof(NativeSceneSurface).Assembly != hostType.Assembly
    || !typeof(INativeWebSceneRenderDiagnostics).IsAssignableFrom(typeof(NativeSceneSurface)))
{
    Console.Error.WriteLine(
        "Backend package smoke: the reusable native scene host is missing from WebScene.Backend.Avalonia.");
    return 1;
}

Console.WriteLine(
    $"Backend package smoke: pass; host={hostType.Assembly.GetName().Name}, " +
    $"contract={typeof(IWebSceneBackendHost).Assembly.GetName().Name}");
return 0;
