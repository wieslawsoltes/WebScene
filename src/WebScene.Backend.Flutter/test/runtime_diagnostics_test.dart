import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart' hide Size;
import 'package:flutter_test/flutter_test.dart';
import 'package:webscene_flutter/webscene_flutter.dart';
import 'package:webscene_flutter/src/runtime_diagnostics.dart';

void main() {
  test('console constructor remains compatible and metadata is copied', () {
    const message = WebSceneConsoleMessage('log', 'hello');
    expect(message.arguments, isEmpty);
    final context = WebSceneDiagnosticContext.fromJson({
      'sequence': 8,
      'timestamp': 1234,
      'frameId': 2,
      'source': 'child.js',
      'line': 3,
      'column': 4,
      'truncated': true,
    }, 9);
    expect(context.generation, 9);
    expect(context.frameId, 2);
    expect(context.source, 'child.js');
    expect(context.truncated, isTrue);
  });

  testWidgets(
      'failed startup is reported once, retains details and uses custom fallback',
      (tester) async {
    final controller = WebSceneController();
    final failures = <WebSceneRuntimeFailure>[];
    await tester.pumpWidget(MaterialApp(
        home: WebSceneView(
      documentUrl: 'https://example.test/',
      runtime: const WebSceneRuntimeConfiguration(
          runtimeLibraryPath: '/missing/runtime.dylib',
          bridgeLibraryPath: '/missing/bridge.dylib'),
      controller: controller,
      onRuntimeFailed: failures.add,
      showRuntimeFailure: true,
      runtimeFailureBuilder: (_, failure) =>
          Text('Custom failure: ${failure.stage}'),
    )));
    await tester.pumpAndSettle();
    expect(failures, hasLength(1));
    expect(controller.runtimeState, WebSceneRuntimeState.failed);
    expect(controller.lastFailure, same(failures.single));
    expect(find.text('Custom failure: load'), findsOneWidget);
    await tester.pumpWidget(const SizedBox());
    expect(controller.runtimeState, WebSceneRuntimeState.disposed);
    expect(controller.lastFailure, isNotNull);
  });

  final libraryPath = Platform.environment['WEBSCENE_TEST_NATIVE_LIBRARY'];
  test(
      'native wakeup delivers console and page timer errors outside frame polling',
      () async {
    final library = DynamicLibrary.open(libraryPath!);
    final create = library.lookupFunction<Pointer<Void> Function(Uint32),
        Pointer<Void> Function(int)>('webscene_engine_create');
    final destroy = library.lookupFunction<Void Function(Pointer<Void>),
        void Function(Pointer<Void>)>('webscene_engine_destroy');
    final execute = library.lookupFunction<
        Uint8 Function(Pointer<Void>, Pointer<Utf8>, Size, Pointer<Utf8>, Size),
        int Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Utf8>,
            int)>('webscene_engine_execute_script');
    final engine = create(0);
    expect(engine, isNot(nullptr));
    final console = Completer<Map<String, dynamic>>();
    final error = Completer<Map<String, dynamic>>();
    final diagnostics = NativeRuntimeDiagnostics(library, engine, (record) {
      if (record['kind'] == 'console' && !console.isCompleted) {
        console.complete(record);
      }
      if (record['kind'] == 'exception' && !error.isCompleted) {
        error.complete(record);
      }
    });
    try {
      diagnostics.configure(3, required: true);
      const source =
          "console.info('Dart notification'); setTimeout(()=>{throw Error('page callback')},0)";
      const name = 'flutter-diagnostics.js';
      final sourceBytes = source.toNativeUtf8();
      final nameBytes = name.toNativeUtf8();
      try {
        expect(
            execute(engine, sourceBytes, source.length, nameBytes, name.length),
            1);
      } finally {
        calloc.free(sourceBytes);
        calloc.free(nameBytes);
      }
      expect(
          (await console.future
              .timeout(const Duration(seconds: 10)))['message'],
          'Dart notification');
      expect(
          (await error.future.timeout(const Duration(seconds: 10)))['message'],
          contains('page callback'));
    } finally {
      diagnostics.dispose();
      destroy(engine);
    }
  }, skip: libraryPath == null || !Platform.isMacOS);
}
