import 'dart:async';
import 'dart:convert';
import 'dart:math' as math;

import 'package:webscene_flutter/webscene_flutter.dart';

final class TradingViewExampleDatafeed {
  static const _resolutions = ['1', '5', '15', '60', '240', 'D'];

  List<WebSceneScript> get initializationScripts => [
        const WebSceneScript('''
(() => {
  const widget = globalThis.__tvWidget || globalThis.tvWidget;
  const theme = "dark";
  if (!widget && typeof globalThis.applyLoadingTheme === "function") {
    if (typeof currentTheme !== "undefined") currentTheme = theme;
    globalThis.applyLoadingTheme(theme);
  } else if (typeof globalThis.onThemeChanged === "function") {
    globalThis.onThemeChanged(theme);
  } else if (widget && typeof widget.changeTheme === "function") {
    widget.changeTheme(theme);
  }
})()
''', documentName: 'webscene-flutter-change-theme.js'),
        WebSceneScript('''
(() => {
  if (typeof globalThis.__webScenePrimeTradingView === "function"
      && (!globalThis.TradingView || typeof globalThis.TradingView.widget !== "function")) {
    globalThis.__webScenePrimeTradingView();
  }
  const markReady = () => {
    globalThis.__webSceneComponentReady = true;
  };
  const readyWidget = globalThis.__tvWidget || globalThis.tvWidget
    || (typeof widget !== "undefined" ? widget : null);
  if (readyWidget && typeof readyWidget.onChartReady === "function") {
    readyWidget.onChartReady(markReady);
  }
  // A published widget is not evidence that its frame rendered. Hosted builds
  // may publish it while the datafeed bridge is still starting, so only use
  // canvas creation as the fallback readiness signal.
  let readinessAttempts = 0;
  const awaitRenderedChart = () => {
    const hasChartCanvas = Array.from(document.querySelectorAll("iframe"))
      .some(frame => {
        try {
          return (frame.contentDocument?.querySelectorAll("canvas").length ?? 0) > 0;
        } catch (_) {
          return false;
        }
      });
    if (hasChartCanvas) {
      markReady();
      return;
    }
    if (++readinessAttempts < 120) setTimeout(awaitRenderedChart, 250);
  };
  setTimeout(awaitRenderedChart, 250);

  let symbolAttempts = 0;
  const applySymbol = () => {
    const publishedWidget = globalThis.__tvWidget || globalThis.tvWidget
      || (typeof widget !== "undefined" ? widget : null);
    if (typeof globalThis.onInstrumentChanged === "function") {
      globalThis.onInstrumentChanged(
        "AAPL",
        "Apple Inc. (deterministic Flutter example)",
        "NASDAQ",
        0.01,
        ${jsonEncode(_resolutions)});
      return;
    }
    if (publishedWidget && typeof publishedWidget.setSymbol === "function") {
      publishedWidget.setSymbol("NASDAQ:AAPL", "60", () => {});
      return;
    }
    const chart = publishedWidget
      && publishedWidget.activeChart
      && publishedWidget.activeChart();
    if (chart && typeof chart.setSymbol === "function") {
      chart.setSymbol("NASDAQ:AAPL", { interval: "60" });
      return;
    }
    if (++symbolAttempts < 40) setTimeout(applySymbol, 250);
  };
  applySymbol();
})()
''', documentName: 'webscene-flutter-set-symbol.js'),
      ];

  FutureOr<void> handleRequest(
    WebSceneController controller,
    Map<String, dynamic> request,
  ) {
    switch (request['kind']) {
      case 'getBars':
        _getBars(controller, request);
      case 'subscribeBars':
      case 'unsubscribeBars':
      case 'openExternalUrl':
        break;
      default:
        controller.executeScript(
          'console.warn(${jsonEncode('Unknown Flutter host request: ${request['kind']}')})',
          documentName: 'webscene-flutter-unknown-host-request.js',
        );
    }
  }

  void _getBars(WebSceneController controller, Map<String, dynamic> request) {
    try {
      final requestId = (request['requestId'] as num?)?.toInt() ?? 0;
      final from = (request['from'] as num?)?.toDouble() ?? 0;
      final to = (request['to'] as num?)?.toDouble() ??
          DateTime.now().millisecondsSinceEpoch / 1000;
      final resolution = request['resolution']?.toString() ?? '60';
      final seconds = _resolutionSeconds(resolution);
      final end = (to / seconds).floor() * seconds;
      final requestedStart = (from / seconds).floor() * seconds;
      final start = math.max(requestedStart, end - seconds * 800);
      final bars = <Map<String, num>>[];
      for (var timestamp = start;
          timestamp < end && bars.length < 800;
          timestamp += seconds) {
        final index = timestamp ~/ math.max(1, seconds);
        final trend = 185 + (index % 2400) / 180;
        final wave = math.sin(index / 9) * 4.8 + math.cos(index / 31) * 2.1;
        final open = trend + wave + math.sin(index * 0.73) * 0.9;
        final close = trend + wave + math.cos(index * 0.61) * 0.9;
        final high = math.max(open, close) +
            0.6 +
            math.sin(index.toDouble()).abs() * 0.8;
        final low = math.min(open, close) -
            0.6 -
            math.cos(index.toDouble()).abs() * 0.8;
        bars.add({
          'time': timestamp * 1000,
          'open': open,
          'high': high,
          'low': low,
          'close': close,
          'volume': 650000 + math.sin(index / 5).abs() * 900000,
        });
      }
      controller.executeScript('''
(() => {
  if (typeof globalThis.onHistoryResponse === "function") {
    globalThis.onHistoryResponse(${jsonEncode(bars)});
  }
  return { delivered: true, requestId: $requestId };
})()
''', documentName: 'webscene-flutter-history-response.js');
    } catch (error) {
      controller.executeScript('''
(() => {
  if (typeof globalThis.onHistoryError === "function") {
    globalThis.onHistoryError(${jsonEncode(error.toString())});
  }
})()
''', documentName: 'webscene-flutter-history-error.js');
    }
  }

  static int _resolutionSeconds(String resolution) {
    if (resolution.toUpperCase() == 'D') return 86400;
    if (resolution.toUpperCase() == 'W') return 604800;
    return (int.tryParse(resolution) ?? 60).clamp(1, 1440) * 60;
  }
}
