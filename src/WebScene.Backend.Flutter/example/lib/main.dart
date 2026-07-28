import 'dart:io';

import 'package:flutter/material.dart';
import 'package:webscene_flutter/webscene_flutter.dart';

import 'tradingview_datafeed.dart';

const _documentUrl = String.fromEnvironment(
  'WEBSCENE_DOCUMENT_URL',
  defaultValue: 'https://trading-terminal.tradingview-widget.com/?theme=dark',
);
const _monacoDocumentPath =
    String.fromEnvironment('WEBSCENE_MONACO_DOCUMENT_PATH');
const _initialDocument = String.fromEnvironment(
  'WEBSCENE_INITIAL_DOCUMENT',
  defaultValue: 'tradingview',
);
const _runtimeLibrary = String.fromEnvironment('WEBSCENE_RUNTIME_LIBRARY');
const _bridgeLibrary = String.fromEnvironment('WEBSCENE_FLUTTER_BRIDGE');
const _cacheDirectory = String.fromEnvironment('WEBSCENE_CACHE_DIRECTORY');

String _bundledPath(String configuredPath, String relativePath) {
  if (configuredPath.isNotEmpty) return configuredPath;
  final executableDirectory = File(Platform.resolvedExecutable).parent;
  return File(
    '${executableDirectory.parent.path}/$relativePath',
  ).absolute.path;
}

void main() {
  runApp(const NativeRuntimeShowcaseApp());
}

enum ShowcaseDocument { tradingView, monaco }

class NativeRuntimeShowcaseApp extends StatelessWidget {
  const NativeRuntimeShowcaseApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'WebScene Native Runtime Showcase',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: const Color(0xff131722),
      ),
      home: const NativeRuntimeShowcasePage(),
    );
  }
}

class NativeRuntimeShowcasePage extends StatefulWidget {
  const NativeRuntimeShowcasePage({super.key});

  @override
  State<NativeRuntimeShowcasePage> createState() =>
      _NativeRuntimeShowcasePageState();
}

class _NativeRuntimeShowcasePageState extends State<NativeRuntimeShowcasePage> {
  final _datafeed = TradingViewExampleDatafeed();
  late final List<WebSceneScript> _tradingViewScripts =
      _datafeed.initializationScripts;
  ShowcaseDocument _document = _initialDocument == 'monaco'
      ? ShowcaseDocument.monaco
      : ShowcaseDocument.tradingView;
  final Set<ShowcaseDocument> _mountedDocuments = {};
  final Map<ShowcaseDocument, Object> _errors = {};
  final Map<ShowcaseDocument, bool> _ready = {};
  final Map<ShowcaseDocument, int> _revisions = {};

  @override
  void initState() {
    super.initState();
    _mountedDocuments.add(_document);
  }

  WebSceneRuntimeConfiguration get _runtime => WebSceneRuntimeConfiguration(
        runtimeLibraryPath: _bundledPath(
          _runtimeLibrary,
          'Frameworks/libwebscene_native_engine.dylib',
        ),
        bridgeLibraryPath: _bundledPath(
          _bridgeLibrary,
          'Frameworks/libwebscene_flutter_bridge.dylib',
        ),
        compilationCacheDirectory: _cacheDirectory.isEmpty
            ? '${Directory.systemTemp.path}/webscene-flutter-example'
            : _cacheDirectory,
      );

  String _documentUrlFor(ShowcaseDocument document) {
    if (document == ShowcaseDocument.tradingView) return _documentUrl;
    final monacoDocumentPath = _bundledPath(
      _monacoDocumentPath,
      'Resources/webscene-monaco/index.html',
    );
    if (!File(monacoDocumentPath).existsSync()) {
      throw StateError(
        'The Monaco document was not found at $monacoDocumentPath. '
        'Provide WEBSCENE_MONACO_DOCUMENT_PATH or use the packaged release.',
      );
    }
    return File(monacoDocumentPath).absolute.uri.toString();
  }

  List<WebSceneScript> _scriptsFor(ShowcaseDocument document) =>
      document == ShowcaseDocument.tradingView ? _tradingViewScripts : const [];

  void _show(ShowcaseDocument document) {
    if (_document == document) return;
    setState(() {
      _document = document;
      _mountedDocuments.add(document);
    });
  }

  void _setError(ShowcaseDocument document, Object error) {
    if (!mounted) return;
    setState(() => _errors[document] = error);
  }

  Widget _buildDocumentView(ShowcaseDocument document) {
    return WebSceneView(
      key: ValueKey(document),
      documentUrl: _documentUrlFor(document),
      runtime: _runtime,
      initializationScripts: _scriptsFor(document),
      onHostRequest: document == ShowcaseDocument.tradingView
          ? _datafeed.handleRequest
          : null,
      onReady: () {
        debugPrint('[WebScene example] ${document.label} ready');
        if (mounted) setState(() => _ready[document] = true);
      },
      onScenePresented: (revision) {
        if (revision == (_revisions[document] ?? 0) || !mounted) return;
        setState(() => _revisions[document] = revision);
      },
      onConsoleMessage: (message) {
        debugPrint(
          '[WebScene ${document.label} ${message.level}] ${message.message}',
        );
      },
      onError: (error) => _setError(document, error),
    );
  }

  @override
  Widget build(BuildContext context) {
    final error = _errors[_document];
    final ready = _ready[_document] ?? false;
    final revision = _revisions[_document] ?? 0;
    return Scaffold(
      body: Column(
        children: [
          _ShowcaseToolbar(
            document: _document,
            onShow: _show,
          ),
          Expanded(
            child: Stack(
              children: [
                Positioned.fill(
                  child: IndexedStack(
                    index: _document.index,
                    sizing: StackFit.expand,
                    children: [
                      _mountedDocuments.contains(
                        ShowcaseDocument.tradingView,
                      )
                          ? _buildDocumentView(
                              ShowcaseDocument.tradingView,
                            )
                          : const SizedBox.expand(),
                      _mountedDocuments.contains(ShowcaseDocument.monaco)
                          ? _buildDocumentView(ShowcaseDocument.monaco)
                          : const SizedBox.expand(),
                    ],
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
                        error != null
                            ? 'Error: $error'
                            : ready
                                ? '${_document.label} ready · scene $revision'
                                : 'Loading ${_document.label} · scene $revision',
                        style: TextStyle(
                          color: error == null
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
          ),
        ],
      ),
    );
  }
}

extension on ShowcaseDocument {
  String get label => switch (this) {
        ShowcaseDocument.tradingView => 'TradingView',
        ShowcaseDocument.monaco => 'Monaco',
      };
}

class _ShowcaseToolbar extends StatelessWidget {
  const _ShowcaseToolbar({
    required this.document,
    required this.onShow,
  });

  final ShowcaseDocument document;
  final ValueChanged<ShowcaseDocument> onShow;

  @override
  Widget build(BuildContext context) {
    return ColoredBox(
      color: const Color(0xff181b21),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
        child: Row(
          children: [
            const Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'WebScene native runtime showcase',
                    style: TextStyle(
                      fontSize: 18,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  Text(
                    'Native V8 · retained scene rendering · Flutter',
                    style: TextStyle(
                      color: Color(0xffaeb5c2),
                      fontSize: 12,
                    ),
                  ),
                ],
              ),
            ),
            SegmentedButton<ShowcaseDocument>(
              segments: const [
                ButtonSegment(
                  value: ShowcaseDocument.tradingView,
                  label: Text('TradingView'),
                ),
                ButtonSegment(
                  value: ShowcaseDocument.monaco,
                  label: Text('Monaco editor'),
                ),
              ],
              selected: {document},
              showSelectedIcon: false,
              onSelectionChanged: (selection) => onShow(selection.single),
            ),
          ],
        ),
      ),
    );
  }
}
