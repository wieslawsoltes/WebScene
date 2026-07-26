import 'package:flutter_test/flutter_test.dart';
import 'package:htmlml_flutter/htmlml_flutter.dart';

void main() {
  test('runtime configuration retains explicit native paths', () {
    const configuration = HtmlMlRuntimeConfiguration(
      runtimeLibraryPath: '/runtime/libhtmlml_native_engine.dylib',
      bridgeLibraryPath: '/runtime/libhtmlml_flutter_bridge.dylib',
      compilationCacheDirectory: '/cache/htmlml',
    );

    expect(
      configuration.runtimeLibraryPath,
      '/runtime/libhtmlml_native_engine.dylib',
    );
    expect(
      configuration.bridgeLibraryPath,
      '/runtime/libhtmlml_flutter_bridge.dylib',
    );
    expect(configuration.compilationCacheDirectory, '/cache/htmlml');
  });

  test('initialization script keeps source identity', () {
    const script = HtmlMlScript(
      'globalThis.ready = true',
      documentName: 'initialize.js',
    );

    expect(script.source, 'globalThis.ready = true');
    expect(script.documentName, 'initialize.js');
  });

  test('controller rejects commands while detached', () {
    final controller = HtmlMlController();

    expect(controller.isAttached, isFalse);
    expect(
      () => controller.executeScript(
        'globalThis.ready = true',
        documentName: 'initialize.js',
      ),
      throwsStateError,
    );
    expect(controller.requestSceneCheckpoint, returnsNormally);
  });
}
