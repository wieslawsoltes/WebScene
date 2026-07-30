interface RealtimeChartBar {
  time: number;
  open: number;
  high: number;
  low: number;
  close: number;
  volume?: number;
}

interface RealtimeChartHost {
  status: number;
  onRealtimeUpdate(subscriberUid: string, bar: RealtimeChartBar): void;
  onHistoryResponse(requestId: string, bars: RealtimeChartBar[]): void;
  getHistory(): Promise<RealtimeChartBar[]>;
  updateCount(): number;
  lastClose(): number;
}

declare const realtimeChartHost: RealtimeChartHost;
