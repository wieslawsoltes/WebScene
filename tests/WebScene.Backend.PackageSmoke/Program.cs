using WebScene.Backends.Avalonia;
using WebScene.Backends.Avalonia.Native;
using WebScene.Backends.Native;

var viewType = typeof(NativeWebSceneView);
if (!string.Equals(viewType.Assembly.GetName().Name, "WebScene.Backend.Avalonia", StringComparison.Ordinal))
{
    Console.Error.WriteLine($"Backend package smoke: implementation assembly was '{viewType.Assembly.GetName().Name}'.");
    return 1;
}
if (typeof(NativeSceneSurface).Assembly != viewType.Assembly
    || !typeof(INativeWebSceneRenderDiagnostics).IsAssignableFrom(typeof(NativeSceneSurface)))
{
    Console.Error.WriteLine(
        "Backend package smoke: the reusable native scene host is missing from WebScene.Backend.Avalonia.");
    return 1;
}
if (typeof(AvaloniaResourceLoader).Assembly != viewType.Assembly)
{
    Console.Error.WriteLine("Backend package smoke: the native resource loader is missing.");
    return 1;
}
if (viewType.GetMethod(
        nameof(NativeWebSceneView.LoadAsync),
        [typeof(NativeWebSceneLoadOptions), typeof(CancellationToken)]) is null)
{
    Console.Error.WriteLine(
        "Backend package smoke: document-start load options are missing.");
    return 1;
}

Console.WriteLine(
    $"Backend package smoke: pass; presenter={viewType.Assembly.GetName().Name}");
return 0;
