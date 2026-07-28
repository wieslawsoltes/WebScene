import 'dart:convert';
import 'dart:ffi';
import 'dart:ui' as ui;

import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:webscene_flutter/src/native_bindings.dart';
import 'package:webscene_flutter/src/scene_projector.dart';

void main() {
  test('DOM text does not rise above zero-height line boxes', () {
    expect(
      WebSceneSceneProjector.domTextPaintTop(
        boxTop: 120,
        boxHeight: 0,
        paintedLineHeight: 15,
        hasExplicitLineHeight: false,
      ),
      123,
    );
    expect(
      WebSceneSceneProjector.domTextPaintTop(
        boxTop: 340,
        boxHeight: 22,
        paintedLineHeight: 22,
        hasExplicitLineHeight: true,
      ),
      340,
    );
    expect(
      WebSceneSceneProjector.domTextPaintTop(
        boxTop: 10,
        boxHeight: 30,
        paintedLineHeight: 20,
        hasExplicitLineHeight: true,
      ),
      15,
    );
  });

  testWidgets('projects and paints complete SVG scene commands',
      (tester) async {
    const markup = '''
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">
  <rect x="0" y="0" width="24" height="24" fill="#ff0000"/>
</svg>
''';
    final resource = utf8.encode('0 0 24 24\t$markup');
    final scene = calloc<WebSceneSceneView>();
    final commands = calloc<WebSceneSceneCommand>();
    final strings = calloc<WebSceneSceneString>();
    final stringBytes = calloc<Uint8>(resource.length);
    final projector = WebSceneSceneProjector();
    try {
      stringBytes.asTypedList(resource.length).setAll(0, resource);
      strings.ref
        ..byteOffset = 0
        ..byteLength = resource.length;
      commands.ref
        ..kind = 6
        ..flags = 0
        ..x = 4
        ..y = 4
        ..width = 24
        ..height = 24;
      scene.ref
        ..structSize = sizeOf<WebSceneSceneView>()
        ..abiVersion = 2
        ..commands = commands
        ..canvasLayers = nullptr
        ..canvasCommands = nullptr
        ..strings = strings
        ..stringBytes = stringBytes
        ..damageRects = nullptr
        ..leaseToken = nullptr
        ..canvasCommandCount = 0
        ..stringCount = 1
        ..stringByteCount = resource.length;
      scene.ref.header
        ..revision = 1
        ..baseRevision = 0
        ..viewportWidth = 32
        ..viewportHeight = 32
        ..commandCount = 1
        ..canvasLayerCount = 0
        ..damageRectCount = 0
        ..flags = 3;

      final applied = projector.apply(scene);
      expect(applied.accepted, isTrue);
      expect(projector.fullSvgPlacementCount, 1);

      await tester.runAsync(projector.waitForPendingSvgLoads);
      expect(projector.cachedSvgPictureCount, 1);
      expect(projector.failedSvgPictureCount, 0);

      final recorder = ui.PictureRecorder();
      final canvas = ui.Canvas(recorder);
      projector.paint(canvas, const ui.Size(32, 32));
      final picture = recorder.endRecording();
      final image = await tester.runAsync(() => picture.toImage(32, 32));
      final pixels = await tester.runAsync(
        () => image!.toByteData(format: ui.ImageByteFormat.rawRgba),
      );
      final center = (16 * 32 + 16) * 4;
      expect(pixels!.getUint8(center), greaterThan(200));
      expect(pixels.getUint8(center + 3), greaterThan(200));
      image!.dispose();
      picture.dispose();
    } finally {
      projector.dispose();
      calloc
        ..free(stringBytes)
        ..free(strings)
        ..free(commands)
        ..free(scene);
    }
  });
}
