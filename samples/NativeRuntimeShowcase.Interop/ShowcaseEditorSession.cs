using WebScene.JavaScript.Interop;

namespace NativeRuntimeShowcase.Interop;

public sealed class ShowcaseEditorSession : IAsyncDisposable
{
    private readonly NativeJavaScriptInvoker _invoker;
    private MonacoEditor? _editor;

    private static readonly JavaScriptBinaryCallSite s_readyCallSite = new(
        JavaScriptBinaryOperation.GetGlobal,
        "__webSceneComponentReady",
        memberName: null,
        JavaScriptBinaryResultMode.Value);
    private static readonly JavaScriptBinaryCallSite s_editorCallSite = new(
        JavaScriptBinaryOperation.GetGlobal,
        "__webSceneMonacoEditor",
        memberName: null,
        JavaScriptBinaryResultMode.RetainedHandle);
    private static readonly JavaScriptBinaryCallSite s_setFileNameCallSite = new(
        JavaScriptBinaryOperation.InvokeGlobal,
        "__webSceneShowcaseSetFileName",
        memberName: null,
        JavaScriptBinaryResultMode.Void);

    public ShowcaseEditorSession(NativeJavaScriptInvoker invoker)
    {
        _invoker = invoker ?? throw new ArgumentNullException(nameof(invoker));
    }

    public async Task InitializeAsync(
        CancellationToken cancellationToken = default)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(30);
        while (DateTime.UtcNow < deadline)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var ready = await _invoker.InvokeBinaryAsync<
                JavaScriptBinaryVoid,
                bool,
                ReadyCodec>(
                s_readyCallSite,
                default,
                new JavaScriptBinaryVoid(),
                cancellationToken);
            if (ready)
            {
                var reference = await _invoker.InvokeBinaryAsync<
                    JavaScriptBinaryVoid,
                    JavaScriptObjectReference,
                    EditorCodec>(
                    s_editorCallSite,
                    default,
                    new JavaScriptBinaryVoid(),
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
        await _invoker.InvokeBinaryVoidAsync<FileNameArguments, FileNameCodec>(
            s_setFileNameCallSite,
            default,
            new FileNameArguments(fileName),
            cancellationToken);
        await editor.LayoutAsync(cancellationToken);
        await editor.FocusAsync(cancellationToken);
    }

    public Task<string> ReadAsync(
        CancellationToken cancellationToken = default)
        => RequireEditor().GetValueAsync(cancellationToken).AsTask();

    public async ValueTask DisposeAsync()
    {
        try
        {
            if (_editor is not null)
            {
                await _editor.DisposeAsync();
                _editor = null;
            }
        }
        finally
        {
            _invoker.Dispose();
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

    private readonly record struct FileNameArguments(string Value);

    private readonly struct ReadyCodec
        : IJavaScriptBinaryCodec<JavaScriptBinaryVoid, bool>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in JavaScriptBinaryVoid arguments)
            => writer.BeginArray(0);

        public static bool DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => value.Kind == JavaScriptBinaryValueKind.Boolean
               && value.GetBoolean();
    }

    private readonly struct EditorCodec
        : IJavaScriptBinaryCodec<
            JavaScriptBinaryVoid,
            JavaScriptObjectReference>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in JavaScriptBinaryVoid arguments)
            => writer.BeginArray(0);

        public static JavaScriptObjectReference DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => value.GetHandle();
    }

    private readonly struct FileNameCodec
        : IJavaScriptBinaryCodec<FileNameArguments, JavaScriptBinaryVoid>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in FileNameArguments arguments)
        {
            var root = writer.BeginArray(1);
            writer.SetArrayItem(root, 0, writer.WriteString(arguments.Value));
            return root;
        }

        public static JavaScriptBinaryVoid DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => new();
    }
}
