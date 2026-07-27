import 'dart:io';

import 'package:flutter/material.dart';
import 'package:webscene_flutter/webscene_flutter.dart';

import 'tradingview_datafeed.dart';

const _documentUrl = String.fromEnvironment(
  'WEBSCENE_DOCUMENT_URL',
  defaultValue: 'https://trading-terminal.tradingview-widget.com/',
);
const _runtimeLibrary = String.fromEnvironment('WEBSCENE_RUNTIME_LIBRARY');
const _bridgeLibrary = String.fromEnvironment('WEBSCENE_FLUTTER_BRIDGE');
const _cacheDirectory = String.fromEnvironment('WEBSCENE_CACHE_DIRECTORY');

void main() {
  runApp(const TradingViewExampleApp());
}

class TradingViewExampleApp extends StatelessWidget {
  const TradingViewExampleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'WebScene Flutter TradingView',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: const Color(0xff131722),
      ),
      home: const TradingViewExamplePage(),
    );
  }
}

class TradingViewExamplePage extends StatefulWidget {
  const TradingViewExamplePage({super.key});

  @override
  State<TradingViewExamplePage> createState() => _TradingViewExamplePageState();
}

class _TradingViewExamplePageState extends State<TradingViewExamplePage> {
  final _datafeed = TradingViewExampleDatafeed();
  Object? _error;
  bool _ready = false;
  int _revision = 0;

  WebSceneRuntimeConfiguration get _runtime => WebSceneRuntimeConfiguration(
        runtimeLibraryPath: _runtimeLibrary,
        bridgeLibraryPath: _bridgeLibrary,
        compilationCacheDirectory: _cacheDirectory.isEmpty
            ? '${Directory.systemTemp.path}/webscene-flutter-example'
            : _cacheDirectory,
      );

  void _setError(Object error) {
    if (!mounted) return;
    setState(() => _error = error);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Stack(
        children: [
          Positioned.fill(
            child: WebSceneView(
              documentUrl: _documentUrl,
              runtime: _runtime,
              initializationScripts: _datafeed.initializationScripts,
              onHostRequest: _datafeed.handleRequest,
              onReady: () {
                debugPrint('[WebScene example] TradingView ready');
                if (mounted) setState(() => _ready = true);
              },
              onScenePresented: (revision) {
                if (revision == _revision || !mounted) return;
                setState(() => _revision = revision);
              },
              onConsoleMessage: (message) {
                debugPrint('[WebScene ${message.level}] ${message.message}');
              },
              onError: _setError,
            ),
          ),
          Positioned(
            top: 12,
            right: 12,
            child: DecoratedBox(
              decoration: BoxDecoration(
                color: const Color(0xcc1e222d),
                borderRadius: BorderRadius.circular(6),
              ),
              child: Padding(
                padding: const EdgeInsets.symmetric(
                  horizontal: 10,
                  vertical: 7,
                ),
                child: Text(
                  _error != null
                      ? 'Error: $_error'
                      : _ready
                          ? 'TradingView ready · scene $_revision'
                          : 'Loading TradingView · scene $_revision',
                  style: TextStyle(
                    color: _error == null
                        ? const Color(0xffd1d4dc)
                        : const Color(0xffff6b6b),
                    fontSize: 12,
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
