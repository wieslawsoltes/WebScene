import * as monaco from "monaco-editor/editor/editor.api.js";
import "monaco-editor/features/codicon/register.js";
import "monaco-editor/languages/definitions/javascript/register.js";
import "monaco-editor/editor/contrib/bracketMatching/browser/bracketMatching.js";
import "monaco-editor/editor/contrib/folding/browser/folding.js";

globalThis.monaco = monaco;
