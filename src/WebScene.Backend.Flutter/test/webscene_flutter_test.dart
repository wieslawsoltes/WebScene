import 'package:flutter_test/flutter_test.dart';
import 'package:webscene_flutter/webscene_flutter.dart';

void main() {
  test('runtime configuration retains explicit native paths', () {
    const configuration = WebSceneRuntimeConfiguration(
      runtimeLibraryPath: '/runtime/libwebscene_native_engine.dylib',
      bridgeLibraryPath: '/runtime/libwebscene_flutter_bridge.dylib',
      compilationCacheDirectory: '/cache/webscene',
    );

    expect(
      configuration.runtimeLibraryPath,
      '/runtime/libwebscene_native_engine.dylib',
    );
    expect(
      configuration.bridgeLibraryPath,
      '/runtime/libwebscene_flutter_bridge.dylib',
    );
    expect(configuration.compilationCacheDirectory, '/cache/webscene');
  });

  test('initialization script keeps source identity', () {
    const script = WebSceneScript(
      'globalThis.ready = true',
      documentName: 'initialize.js',
    );

    expect(script.source, 'globalThis.ready = true');
    expect(script.documentName, 'initialize.js');
  });

  test('controller rejects commands while detached', () {
    final controller = WebSceneController();

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
