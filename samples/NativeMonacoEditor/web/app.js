document.addEventListener("DOMContentLoaded", () => {
  const status = document.getElementById("status");
  const container = document.getElementById("editor");

  try {
    const editor = monaco.editor.create(container, {
      value: [
        "function greet(name) {",
        "  const message = `Hello, ${name}!`;",
        "  return message;",
        "}",
        "",
        "for (const name of [\"Avalonia\", \"WebScene\", \"Monaco\"]) {",
        "  console.log(greet(name));",
        "}"
      ].join("\n"),
      language: "javascript",
      theme: "vs-dark",
      automaticLayout: true,
      folding: true,
      foldingHighlight: true,
      glyphMargin: true,
      lineNumbers: "on",
      minimap: { enabled: false },
      fontFamily: "Menlo, Monaco, Consolas, monospace",
      fontSize: 14,
      lineHeight: 20,
      padding: { top: 12, bottom: 12 },
      scrollBeyondLastLine: false
    });

    editor.onDidChangeModelContent(() => {
      status.textContent =
        `${editor.getModel().getLineCount()} lines · editable · JavaScript`;
    });
    editor.layout();
    editor.focus();
    status.textContent = "8 lines · editable · JavaScript";
    globalThis.__webSceneMonacoEditor = editor;
    globalThis.__webSceneComponentReady = true;
  } catch (error) {
    status.textContent = `Monaco failed: ${error.message || error}`;
    console.error(error);
  }
});
