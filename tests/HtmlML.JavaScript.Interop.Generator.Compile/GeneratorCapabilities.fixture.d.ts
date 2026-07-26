// Library-independent declaration shapes used to compile-test generated C#.
declare namespace GeneratorCapabilities {
  type Brand<T, Name extends string> = T & { readonly __brand: void };

  interface IWidget {
    label: string;
  }

  interface Formatter {
    (value: number): string;
  }

  type WidgetHandle = Widget;
  type WidgetList = readonly Widget[];
  type Disposer = () => void;
  type MaybeDisposer = Disposer | undefined;

  class Controller {
    constructor(name: string);
    constructor(id: number, options?: WidgetOptions);
    static fromId(id: number): Controller;
    static readonly current: Controller;
    static readonly ready: Promise<Controller>;
    static readonly version: string;
    close(): void;
  }

  interface Widget {
    readonly marker: void;
    peer?: Widget;
    disposer?: Disposer;
    readonly ready: Promise<Widget>;
    readonly readyDisposer: Promise<Disposer>;
    readonly children: readonly Widget[];
    readonly readyChildren: Promise<readonly Widget[]>;
    attach(widget: Widget, parent?: Widget): void;
    map<T>(value: T): T;
    describe(): { text: string };
    choose(): Variant1 | Variant2 | Variant3 | Variant4 | Variant5
      | Variant6 | Variant7 | Variant8 | Variant9;
    chooseWide(): Variant1 | Variant2 | Variant3 | Variant4 | Variant5
      | Variant6 | Variant7 | Variant8 | Variant9 | Variant10 | Variant11
      | Variant12 | Variant13 | Variant14 | Variant15 | Variant16 | Variant17;
    element(): HTMLElement;
    maybeElement(): HTMLElement | undefined;
    bytes(): Promise<Uint8Array>;
    maybeBytes(): Promise<Uint8Array | undefined>;
    maybeWidget(): Widget | undefined;
    maybeWidgetAsync(): Promise<Widget | undefined>;
    aliasedWidget(): WidgetHandle;
    aliasedWidgets(): WidgetList;
    maybeAliasedWidget(): WidgetHandle | undefined;
    createDisposer(): Disposer;
    maybeDisposer(): MaybeDisposer;
    createDisposerAsync(): Promise<Disposer>;
    maybeDisposerAsync(): Promise<MaybeDisposer>;
    widgets(): readonly Widget[];
    widgetsAsync(): Promise<readonly Widget[]>;
    maybeWidgets(): readonly Widget[] | undefined;
    disposers(): readonly Disposer[];
    widgetOrLabel(): Widget | string;
    widgetsOrLabel(): readonly Widget[] | string;
    disposerOrLabel(): Disposer | string;
    tuple(): [string, number];
    widgetTuple(): [string, Widget];
    singleWidgetTuple(): [Widget];
    longTuple(): [string, number, boolean, string, number, boolean, string, Widget];
    acceptWidgetTuple(value: [string, Widget]): void;
    snapshot(): WidgetSnapshot;
    anonymousSnapshot(): {
      title: string;
      widget: Widget;
    };
    widgetEnvelope(): Envelope<Widget>;
    widgetRecord(): Record<string, Widget>;
    widgetDictionary(): WidgetDictionary;
    genericWidgetDictionary(): DictionaryEnvelope<Widget>;
    numericWidgetDictionary(): NumericWidgetDictionary;
    mixedWidgetDictionary(): MixedWidgetDictionary;
    acceptWidgetDictionary(value: WidgetDictionary): void;
    acceptGenericWidgetDictionary(value: DictionaryEnvelope<Widget>): void;
    intersection(): NamedTimestamp;
    getMap(): ReadonlyMap<string, number>;
    maybeAsync(): string | Promise<string>;
    configure(value: string, enabled?: boolean): void;
    listen(handler?: (value: string) => void): void;
    consumeWideCallback(
      handler: (a: string, b: number, c: boolean, d: string, e: number) => void
    ): void;
    render(): void;
    renderAsync(): Promise<void>;
    update(value: string): void;
    update(value: number): void;
  }

  interface WidgetOptions {
    WidgetOptions: string;
    pattern?: RegExp;
    formatter: (value: number) => string;
    namedFormatter: Formatter;
    nested?: {
      enabled: boolean;
    };
  }

  interface WidgetSnapshot {
    title: string;
    widget: Widget;
    maybeWidget?: Widget;
    widgets: readonly Widget[];
    tuple: [string, Widget];
    dispose: Disposer;
  }

  interface Envelope<T> {
    value: T;
    values: readonly T[];
  }

  interface WidgetDictionary {
    [name: string]: Widget;
  }

  interface DictionaryEnvelope<T> {
    [name: string]: T;
  }

  interface NumericWidgetDictionary {
    [id: number]: Widget;
  }

  interface MixedWidgetDictionary {
    primary: Widget;
    [name: string]: Widget;
  }

  interface Named {
    name: string;
  }

  interface Timestamped {
    timestamp: number;
  }

  type NamedTimestamp = Named & Timestamped;

  interface Variant1 { kind: 1; }
  interface Variant2 { kind: 2; }
  interface Variant3 { kind: 3; }
  interface Variant4 { kind: 4; }
  interface Variant5 { kind: 5; }
  interface Variant6 { kind: 6; }
  interface Variant7 { kind: 7; }
  interface Variant8 { kind: 8; }
  interface Variant9 { kind: 9; }
  interface Variant10 { kind: 10; }
  interface Variant11 { kind: 11; }
  interface Variant12 { kind: 12; }
  interface Variant13 { kind: 13; }
  interface Variant14 { kind: 14; }
  interface Variant15 { kind: 15; }
  interface Variant16 { kind: 16; }
  interface Variant17 { kind: 17; }

  function createWidget(options: WidgetOptions): Widget;
  function loadWidget(
    name: string,
    configure?: (options: WidgetOptions) => void
  ): Promise<Widget>;
  function createDisposer(): Disposer;
  function loadDisposer(): Promise<Disposer>;
  function maybeDisposer(): MaybeDisposer;
  function listWidgets(): readonly Widget[];
  function loadWidgets(): Promise<readonly Widget[]>;
  function widgetOrLabel(): Widget | string;
  const normalizeLabel: (value: string) => string;
  const currentController: Controller;
  const readyController: Promise<Controller>;
  const widgets: readonly Widget[];
  const libraryVersion: string;
}
