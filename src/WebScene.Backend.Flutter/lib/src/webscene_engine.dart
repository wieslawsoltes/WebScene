import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'native_bindings.dart';

const int webScenePointerMove = 1;
const int webScenePointerDown = 2;
const int webScenePointerUp = 3;
const int webSceneWheel = 4;
const int webSceneFrame = 5;
const int webSceneResize = 6;
const int webSceneKeyDown = 7;
const int webSceneKeyUp = 8;
const int webSceneText = 9;

final class WebSceneEngine {
  WebSceneEngine._(this._native, this._bridge, this._handle);

  final WebSceneNativeApi _native;
  final WebSceneBridgeApi _bridge;
  Pointer<Void> _handle;
  int _sequence = DateTime.now().microsecondsSinceEpoch;

  bool get isDisposed => _handle == nullptr;

  static WebSceneEngine create({
    required String runtimeLibrary,
    required String bridgeLibrary,
    String? cacheDirectory,
  }) {
    if (!Platform.isMacOS) {
      throw UnsupportedError(
        'WebScene.Backend.Flutter currently supports macOS only.',
      );
    }
    final runtimeFile = File(runtimeLibrary);
    final bridgeFile = File(bridgeLibrary);
    if (!runtimeFile.existsSync()) {
      throw StateError('WebScene runtime not found at ${runtimeFile.path}');
    }
    if (!bridgeFile.existsSync()) {
      throw StateError('Flutter native bridge not found at ${bridgeFile.path}');
    }

    final runtime = DynamicLibrary.open(runtimeFile.absolute.path);
    final native = WebSceneNativeApi(runtime);
    if (native.getAbiVersion() != 3) {
      throw StateError(
        'WebScene ABI ${native.getAbiVersion()} is incompatible; expected ABI 3.',
      );
    }
    final bridge = WebSceneBridgeApi(
      DynamicLibrary.open(bridgeFile.absolute.path),
    );
    final cache = Directory(
      cacheDirectory ??
          '${Directory.systemTemp.path}/webscene-flutter-runtime/v8-cache',
    )..createSync(recursive: true);
    final runtimePath = runtimeFile.absolute.path.toNativeUtf8();
    final cachePath = cache.path.toNativeUtf8();
    try {
      final handle = bridge.create(runtimePath, cachePath);
      if (handle == nullptr) {
        throw StateError(bridge.lastError().toDartString());
      }
      return WebSceneEngine._(native, bridge, handle);
    } finally {
      calloc.free(runtimePath);
      calloc.free(cachePath);
    }
  }

  void load(String url) {
    _ensureAlive();
    final bytes = utf8.encode(url);
    final address = calloc<Uint8>(bytes.length);
    try {
      address.asTypedList(bytes.length).setAll(0, bytes);
      if (_native.loadUrl(_handle, address, bytes.length) == 0) {
        throw StateError(lastError);
      }
    } finally {
      calloc.free(address);
    }
  }

  int nextSequence() => ++_sequence;

  bool enqueue({
    required int kind,
    int flags = 0,
    int? sequence,
    double x = 0,
    double y = 0,
    double deltaX = 0,
    double deltaY = 0,
  }) {
    if (isDisposed) return false;
    final event = calloc<WebSceneInputEvent>();
    try {
      event.ref
        ..kind = kind
        ..flags = flags
        ..sequence = sequence ?? nextSequence()
        ..x = x
        ..y = y
        ..deltaX = deltaX
        ..deltaY = deltaY;
      return _native.enqueue(_handle, event) != 0;
    } finally {
      calloc.free(event);
    }
  }

  bool enqueueResizeFrame({
    required double width,
    required double height,
    required double scale,
    required double timestampMilliseconds,
  }) {
    if (isDisposed || width <= 0 || height <= 0) return false;
    final resize = calloc<WebSceneInputEvent>();
    final frame = calloc<WebSceneInputEvent>();
    try {
      resize.ref
        ..kind = webSceneResize
        ..sequence = nextSequence()
        ..x = width
        ..y = height
        ..deltaX = scale;
      frame.ref
        ..kind = webSceneFrame
        ..sequence = nextSequence()
        ..x = timestampMilliseconds;
      return _native.enqueueResizeFrame(_handle, resize, frame) != 0;
    } finally {
      calloc.free(resize);
      calloc.free(frame);
    }
  }

  Pointer<WebSceneSceneView> acquireNextScene() {
    if (isDisposed) return nullptr;
    return _native.acquireNextScene(_handle);
  }

  bool acknowledge(Pointer<WebSceneSceneView> scene) =>
      _native.acknowledge(scene) != 0;

  void release(Pointer<WebSceneSceneView> scene) => _native.release(scene);

  void requestCheckpoint() {
    if (!isDisposed) _native.requestCheckpoint(_handle);
  }

  int get cursor => isDisposed ? 0 : _native.getCursor(_handle);

  void setVisible(bool visible) {
    if (!isDisposed) _native.setVisible(_handle, visible ? 1 : 0);
  }

  void requestLowMemory() {
    if (!isDisposed) _native.requestLowMemory(_handle);
  }

  void executeScript(String source, String documentName) {
    _ensureAlive();
    final sourceBytes = utf8.encode(source);
    final nameBytes = utf8.encode(documentName);
    final sourcePointer = calloc<Uint8>(sourceBytes.length);
    final namePointer = calloc<Uint8>(nameBytes.length);
    try {
      sourcePointer.asTypedList(sourceBytes.length).setAll(0, sourceBytes);
      namePointer.asTypedList(nameBytes.length).setAll(0, nameBytes);
      if (_native.executeScript(
            _handle,
            sourcePointer,
            sourceBytes.length,
            namePointer,
            nameBytes.length,
          ) ==
          0) {
        throw StateError(lastError);
      }
    } finally {
      calloc.free(sourcePointer);
      calloc.free(namePointer);
    }
  }

  List<String> takeHostRequests({int maximum = 8}) =>
      _drainTextQueue(_native.takeHostRequest, maximum: maximum);

  List<String> drainConsole() {
    return _drainTextQueue(_native.takeConsoleMessage);
  }

  List<String> _drainTextQueue(
    int Function(Pointer<Void>, Pointer<Uint8>, int) callback, {
    int maximum = 256,
  }) {
    if (isDisposed) return const [];
    final messages = <String>[];
    while (messages.length < maximum) {
      final size = callback(_handle, nullptr, 0);
      if (size == 0) break;
      final buffer = calloc<Uint8>(size);
      try {
        final copied = callback(_handle, buffer, size);
        if (copied == 0) break;
        final contentLength =
            copied > 0 && buffer[copied - 1] == 0 ? copied - 1 : copied;
        messages.add(utf8.decode(buffer.asTypedList(contentLength)));
      } finally {
        calloc.free(buffer);
      }
    }
    return messages;
  }

  String get lastError {
    if (isDisposed) return 'The WebScene engine is disposed.';
    final size = _native.copyLastError(_handle, nullptr, 0);
    if (size == 0) return 'Unknown native WebScene error.';
    final buffer = calloc<Uint8>(size);
    try {
      final copied = _native.copyLastError(_handle, buffer, size);
      return copied == 0
          ? 'Unknown native WebScene error.'
          : utf8.decode(buffer.asTypedList(copied));
    } finally {
      calloc.free(buffer);
    }
  }

  void dispose() {
    if (isDisposed) return;
    final handle = _handle;
    _handle = nullptr;
    _bridge.destroy(handle);
  }

  void _ensureAlive() {
    if (isDisposed) throw StateError('The WebScene engine is disposed.');
  }
}
