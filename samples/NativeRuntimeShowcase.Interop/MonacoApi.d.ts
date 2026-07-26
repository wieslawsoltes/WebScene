// Deliberately small, application-owned slice of Monaco's public API.
// The source generator turns this TypeScript contract into the .NET facade
// consumed by both native showcase hosts.
declare namespace monaco.editor {
  interface ITextModel {
    getValue(): string;
    setValue(newValue: string): void;
  }

  interface IStandaloneCodeEditor {
    getValue(): string;
    setValue(newValue: string): void;
    getModel(): ITextModel;
    focus(): void;
    layout(): void;
  }

  function setModelLanguage(model: ITextModel, languageId: string): void;
}
