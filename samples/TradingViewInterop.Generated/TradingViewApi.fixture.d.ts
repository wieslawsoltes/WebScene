// Shape-only fixture for the WebScene proof. The licensed TradingView declaration
// files are consumed locally and must not be copied into this repository.
declare namespace Charting_Library {
  type ResolutionString = string;

  interface SetSymbolOptions {
    doNotActivateChart?: boolean;
  }

  interface ChartingLibraryWidgetOptions {
    container: string;
    symbol: string;
    interval: ResolutionString;
    locale: string;
    autosize?: boolean;
    datafeed?: Datafeed.IDatafeedChartApi;
    broker_factory?: (host: Broker.IBrokerConnectionAdapterHost) => Broker.IBrokerTerminal;
  }

  interface IChartingLibraryWidget {
    activeChart(): IChartWidgetApi;
    getLanguage(): string;
    remove(): void;
  }

  class widget implements IChartingLibraryWidget {
    constructor(options: ChartingLibraryWidgetOptions);
    activeChart(): IChartWidgetApi;
    getLanguage(): string;
    remove(): void;
  }

  interface IChartWidgetApi {
    symbol(): string;
    resolution(): ResolutionString;
    setScrollEnabled(enabled: boolean): void;
    setZoomEnabled(enabled: boolean): void;
    setSymbol(
      symbol: string,
      options?: SetSymbolOptions | (() => void)
    ): Promise<boolean>;
    createOrderLine(): Promise<IOrderLineAdapter>;
    createPositionLine(): Promise<IPositionLineAdapter>;
    createExecutionShape(): Promise<IExecutionLineAdapter>;
    crosshairPrice(): IWatchedValue<number>;
    setVisibleStudies(...studyIds: string[]): void;
  }

  interface IWatchedValue<T> {
    value(): T;
    setValue(value: T): void;
    subscribe(callback: (value: T) => void): void;
  }

  interface IOrderLineAdapter {
    readonly id: string;
    price: number;
    setPrice(price: number): this;
    setQuantity(quantity: string): this;
    setText(text: string): this;
    onMove(callback: () => void): this;
    onCancel(callback: (text: string) => void): this;
    remove(): void;
  }

  interface IPositionLineAdapter {
    setPrice(price: number): IPositionLineAdapter;
    setQuantity(quantity: string): IPositionLineAdapter;
    setText(text: string): IPositionLineAdapter;
    onClose(callback: (text: string) => void): IPositionLineAdapter;
    remove(): void;
  }

  interface IExecutionLineAdapter {
    setPrice(price: number): IExecutionLineAdapter;
    setQuantity(quantity: string): IExecutionLineAdapter;
    setText(text: string): IExecutionLineAdapter;
    setTime(time: number): IExecutionLineAdapter;
    remove(): void;
  }
}

declare namespace Datafeed {
  interface DatafeedConfiguration {
    supported_resolutions: Charting_Library.ResolutionString[];
    exchanges?: Exchange[];
    supports_marks?: boolean;
    supports_timescale_marks?: boolean;
    supports_time?: boolean;
  }

  interface Exchange {
    value: string;
    name: string;
    desc: string;
  }

  interface LibrarySymbolInfo {
    name: string;
    ticker?: string;
    description: string;
    type: string;
    session: string;
    timezone: string;
    exchange: string;
    listed_exchange: string;
    minmov: number;
    pricescale: number;
    has_intraday?: boolean;
    supported_resolutions: Charting_Library.ResolutionString[];
    volume_precision?: number;
    data_status?: "streaming" | "endofday" | "pulsed" | "delayed_streaming";
  }

  interface PeriodParams {
    from: number;
    to: number;
    countBack: number;
    firstDataRequest: boolean;
  }

  interface Bar {
    time: number;
    open: number;
    high: number;
    low: number;
    close: number;
    volume?: number;
  }

  interface HistoryMetadata {
    noData: boolean;
    nextTime?: number;
  }

  interface SearchSymbolResultItem {
    symbol: string;
    full_name: string;
    description: string;
    exchange: string;
    ticker?: string;
    type: string;
  }

  interface QuoteData {
    s: "ok" | "error";
    n: string;
    v: {
      ch?: number;
      chp?: number;
      short_name?: string;
      exchange?: string;
      description?: string;
      lp?: number;
      ask?: number;
      bid?: number;
      volume?: number;
    };
  }

  type OnReadyCallback = (configuration: DatafeedConfiguration) => void;
  type ResolveCallback = (symbolInfo: LibrarySymbolInfo) => void;
  type ErrorCallback = (reason: string) => void;
  type HistoryCallback = (bars: Bar[], metadata?: HistoryMetadata) => void;
  type SubscribeBarsCallback = (bar: Bar) => void;
  type SearchSymbolsCallback = (items: SearchSymbolResultItem[]) => void;
  type QuotesCallback = (quotes: QuoteData[]) => void;

  interface IDatafeedChartApi {
    onReady(callback: OnReadyCallback): void;
    searchSymbols(
      userInput: string,
      exchange: string,
      symbolType: string,
      onResult: SearchSymbolsCallback
    ): void;
    resolveSymbol(
      symbolName: string,
      onResolve: ResolveCallback,
      onError: ErrorCallback
    ): void;
    getBars(
      symbolInfo: LibrarySymbolInfo,
      resolution: Charting_Library.ResolutionString,
      periodParams: PeriodParams,
      onResult: HistoryCallback,
      onError: ErrorCallback
    ): void;
    subscribeBars(
      symbolInfo: LibrarySymbolInfo,
      resolution: Charting_Library.ResolutionString,
      onRealtimeCallback: SubscribeBarsCallback,
      subscriberUID: string,
      onResetCacheNeededCallback: () => void
    ): void;
    unsubscribeBars(subscriberUID: string): void;
    getQuotes(symbols: string[], onDataCallback: QuotesCallback, onErrorCallback: ErrorCallback): void;
    subscribeQuotes(
      symbols: string[],
      fastSymbols: string[],
      onRealtimeCallback: QuotesCallback,
      listenerGuid: string
    ): void;
    unsubscribeQuotes(listenerGuid: string): void;
  }
}

declare namespace Broker {
  type OrderStatus = "placing" | "inactive" | "working" | "rejected" | "filled" | "cancelled";
  type Side = 1 | -1;

  interface Order {
    id: string;
    symbol: string;
    qty: number;
    side: Side;
    status: OrderStatus;
    limitPrice?: number;
    stopPrice?: number;
  }

  interface Position {
    id: string;
    symbol: string;
    qty: number;
    side: Side;
    avgPrice: number;
  }

  interface Execution {
    id: string;
    symbol: string;
    price: number;
    qty: number;
    side: Side;
    time: number;
  }

  interface PreOrder {
    symbol: string;
    qty: number;
    side: Side;
    limitPrice?: number;
    stopPrice?: number;
  }

  interface OrderResult {
    orderId: string;
  }

  interface AccountManagerInfo {
    accountTitle: string;
    summary: { accountBalance: number; equity: number };
  }

  interface IBrokerConnectionAdapterHost {
    orderUpdate(order: Order): void;
    positionUpdate(position: Position): void;
    executionUpdate(execution: Execution): void;
    showNotification(title: string, text: string, notificationType?: number): void;
  }

  interface IBrokerTerminal {
    orders(): Promise<Order[]>;
    positions(): Promise<Position[]>;
    executions(symbol: string): Promise<Execution[]>;
    placeOrder(order: PreOrder): Promise<OrderResult>;
    modifyOrder(order: Order): Promise<void>;
    cancelOrder(orderId: string): Promise<void>;
    closePosition(positionId: string): Promise<void>;
    reversePosition(positionId: string): Promise<void>;
    accountManagerInfo(): AccountManagerInfo;
  }
}
