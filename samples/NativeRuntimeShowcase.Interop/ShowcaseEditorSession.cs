using WebScene.JavaScript.Interop;

namespace NativeRuntimeShowcase.Interop;

public sealed class ShowcaseEditorSession : IAsyncDisposable
{
    private readonly Func<string, string, CancellationToken, Task<string>>
        _evaluateJsonAsync;
    private readonly NativeJavaScriptInvoker _invoker;
    private MonacoEditor? _editor;

    public ShowcaseEditorSession(
        Func<string, string, CancellationToken, Task<string>> evaluateJsonAsync)
    {
        _evaluateJsonAsync = evaluateJsonAsync
            ?? throw new ArgumentNullException(nameof(evaluateJsonAsync));
        _invoker = new NativeJavaScriptInvoker(evaluateJsonAsync);
    }

    public async Task InitializeAsync(
        CancellationToken cancellationToken = default)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(30);
        while (DateTime.UtcNow < deadline)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var ready = await _evaluateJsonAsync(
                "Boolean(globalThis.__webSceneComponentReady)",
                "webscene-showcase-editor-ready.js",
                cancellationToken);
            if (string.Equals(ready, "true", StringComparison.Ordinal))
            {
                var reference = await _invoker.GetGlobalObjectAsync(
                    "__webSceneMonacoEditor",
                    cancellationToken);
                _editor = MonacoEditor.FromReference(_invoker, reference);
                await using var model =
                    await _editor.GetModelAsync(cancellationToken);
                await MonacoApi.SetModelLanguageAsync(
                    _invoker,
                    model,
                    "csharp",
                    cancellationToken);
                await _editor.LayoutAsync(cancellationToken);
                await _editor.FocusAsync(cancellationToken);
                return;
            }
            await Task.Delay(16, cancellationToken);
        }

        throw new TimeoutException(
            "Monaco did not publish its generated .NET API target within 30 seconds.");
    }

    public async Task OpenAsync(
        string fileName,
        string content,
        CancellationToken cancellationToken = default)
    {
        var editor = RequireEditor();
        await editor.SetValueAsync(content, cancellationToken);
        await using var model = await editor.GetModelAsync(cancellationToken);
        await MonacoApi.SetModelLanguageAsync(
            _invoker,
            model,
            LanguageFor(fileName),
            cancellationToken);
        await _invoker.InvokeGlobalVoidAsync(
            "__webSceneShowcaseSetFileName",
            [JavaScriptArgument.From(fileName)],
            cancellationToken);
        await editor.LayoutAsync(cancellationToken);
        await editor.FocusAsync(cancellationToken);
    }

    public Task<string> ReadAsync(
        CancellationToken cancellationToken = default)
        => RequireEditor().GetValueAsync(cancellationToken).AsTask();

    public async ValueTask DisposeAsync()
    {
        if (_editor is not null)
        {
            await _editor.DisposeAsync();
            _editor = null;
        }
    }

    private MonacoEditor RequireEditor()
        => _editor ?? throw new InvalidOperationException(
            "The Monaco generated .NET API is not initialized.");

    private static string LanguageFor(string fileName)
        => Path.GetExtension(fileName).ToLowerInvariant() switch
        {
            ".cs" or ".csx" => "csharp",
            ".js" or ".mjs" or ".cjs" => "javascript",
            ".ts" or ".tsx" => "typescript",
            ".json" => "json",
            ".html" or ".htm" => "html",
            ".css" => "css",
            ".xml" or ".axaml" or ".xaml" => "xml",
            ".md" or ".markdown" => "markdown",
            ".py" => "python",
            ".sh" or ".zsh" or ".bash" => "shell",
            ".cpp" or ".cc" or ".cxx" or ".h" or ".hpp" => "cpp",
            ".java" => "java",
            ".yaml" or ".yml" => "yaml",
            _ => "plaintext"
        };
}
