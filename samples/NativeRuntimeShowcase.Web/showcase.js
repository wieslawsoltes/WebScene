document.addEventListener("DOMContentLoaded", () => {
  const status = document.getElementById("status");
  const container = document.getElementById("editor");
  let fileName = "GeneratedMonacoApi.cs";

  const initialValue = [
    "using HtmlML.JavaScript.Interop;",
    "using NativeRuntimeShowcase.Interop;",
    "",
    "var invoker = new NativeJavaScriptInvoker(view.EvaluateJsonAsync);",
    "var reference = await invoker.GetGlobalObjectAsync(",
    "    \"__htmlMlMonacoEditor\");",
    "await using var editor = MonacoEditor.FromReference(invoker, reference);",
    "",
    "// MonacoEditor is generated from MonacoApi.d.ts at build time.",
    "await editor.SetValueAsync(await File.ReadAllTextAsync(path));",
    "string editedText = await editor.GetValueAsync();"
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
    globalThis.__htmlMlShowcaseSetFileName = name => {
      fileName = name || "Untitled";
      updateStatus();
    };
    editor.layout();
    editor.focus();
    updateStatus();
    globalThis.__htmlMlMonacoEditor = editor;
    globalThis.__htmlMlComponentReady = true;
  } catch (error) {
    status.textContent = `Monaco failed: ${error.message || error}`;
    console.error(error);
  }
});
