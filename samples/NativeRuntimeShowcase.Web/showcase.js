document.addEventListener("DOMContentLoaded", () => {
  const status = document.getElementById("status");
  const container = document.getElementById("editor");
  let fileName = "GeneratedMonacoApi.cs";

  const initialValue = [
    "using WebScene.JavaScript.Interop;",
    "using NativeRuntimeShowcase.Interop;",
    "",
    "await using var session = new ShowcaseEditorSession(",
    "    view.CreateJavaScriptInvoker());",
    "await session.InitializeAsync();",
    "",
    "// MonacoEditor is generated from MonacoApi.d.ts at build time.",
    "await session.OpenAsync(path, await File.ReadAllTextAsync(path));",
    "string editedText = await session.ReadAsync();"
  ].join("\n");

  try {
    const editor = monaco.editor.create(container, {
      value: initialValue,
      language: "csharp",
      theme: "vs-dark",
      automaticLayout: true,
      folding: true,
      foldingHighlight: true,
      glyphMargin: true,
      lineNumbers: "on",
      minimap: { enabled: true },
      fontFamily: "Menlo, Monaco, Consolas, monospace",
      fontSize: 14,
      lineHeight: 20,
      padding: { top: 12, bottom: 12 },
      scrollBeyondLastLine: false
    });

    const updateStatus = () => {
      const lines = editor.getModel()?.getLineCount() ?? 0;
      status.textContent = `${fileName} · ${lines} lines · editable`;
    };
    editor.onDidChangeModelContent(updateStatus);
    globalThis.__webSceneShowcaseSetFileName = name => {
      fileName = name || "Untitled";
      updateStatus();
    };
    editor.layout();
    editor.focus();
    updateStatus();
    globalThis.__webSceneMonacoEditor = editor;
    globalThis.__webSceneComponentReady = true;
  } catch (error) {
    status.textContent = `Monaco failed: ${error.message || error}`;
    console.error(error);
  }
});
