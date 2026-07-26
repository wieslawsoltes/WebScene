import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

final class InputEvent extends Struct {
  @Uint32()
  external int kind;

  @Uint32()
  external int flags;

  @Uint64()
  external int sequence;

  @Double()
  external double x;

  @Double()
  external double y;

  @Double()
  external double deltaX;

  @Double()
  external double deltaY;
}

typedef _MallocNative = Pointer<Void> Function(IntPtr);
typedef _MallocDart = Pointer<Void> Function(int);
typedef _FreeNative = Void Function(Pointer<Void>);
typedef _FreeDart = void Function(Pointer<Void>);
typedef _CreateNative = Pointer<Void> Function(Pointer<Uint8>, Pointer<Uint8>);
typedef _CreateDart = Pointer<Void> Function(Pointer<Uint8>, Pointer<Uint8>);
typedef _DestroyNative = Void Function(Pointer<Void>);
typedef _DestroyDart = void Function(Pointer<Void>);
typedef _LoadNative = Uint8 Function(Pointer<Void>, Pointer<Uint8>, IntPtr);
typedef _LoadDart = int Function(Pointer<Void>, Pointer<Uint8>, int);
typedef _EnqueueNative = Uint8 Function(Pointer<Void>, Pointer<InputEvent>);
typedef _EnqueueDart = int Function(Pointer<Void>, Pointer<InputEvent>);
typedef _AcquireNative = Pointer<Void> Function(Pointer<Void>);
typedef _AcquireDart = Pointer<Void> Function(Pointer<Void>);
typedef _AcknowledgeNative = Uint8 Function(Pointer<Void>);
typedef _AcknowledgeDart = int Function(Pointer<Void>);
typedef _ReleaseNative = Void Function(Pointer<Void>);
typedef _ReleaseDart = void Function(Pointer<Void>);
typedef _ResourceCountNative = Uint64 Function(Pointer<Void>);
typedef _ResourceCountDart = int Function(Pointer<Void>);

void main(List<String> arguments) async {
  if (arguments.length != 3) {
    stderr.writeln(
      'Usage: dart tool/runtime_smoke.dart '
      '<libhtmlml_native_engine.dylib> <libhtmlml_flutter_bridge.dylib> '
      '<document-url>',
    );
    exitCode = 64;
    return;
  }
  final process = DynamicLibrary.process();
  final malloc = process.lookupFunction<_MallocNative, _MallocDart>('malloc');
  final free = process.lookupFunction<_FreeNative, _FreeDart>('free');
  Pointer<Uint8> cString(String value) {
    final bytes = utf8.encode(value);
    final pointer = malloc(bytes.length + 1).cast<Uint8>();
    pointer.asTypedList(bytes.length + 1)
      ..setAll(0, bytes)
      ..[bytes.length] = 0;
    return pointer;
  }

  final runtimePath = File(arguments[0]).absolute.path;
  final bridgePath = File(arguments[1]).absolute.path;
  final runtime = DynamicLibrary.open(runtimePath);
  final bridge = DynamicLibrary.open(bridgePath);
  final create = bridge.lookupFunction<_CreateNative, _CreateDart>(
    'htmlml_flutter_engine_create',
  );
  final destroy = bridge.lookupFunction<_DestroyNative, _DestroyDart>(
    'htmlml_flutter_engine_destroy',
  );
  final resourceCount =
      bridge.lookupFunction<_ResourceCountNative, _ResourceCountDart>(
    'htmlml_flutter_resource_request_count',
  );
  final load = runtime.lookupFunction<_LoadNative, _LoadDart>(
    'htmlml_engine_load_url',
  );
  final enqueue = runtime.lookupFunction<_EnqueueNative, _EnqueueDart>(
    'htmlml_engine_enqueue',
  );
  final acquire = runtime.lookupFunction<_AcquireNative, _AcquireDart>(
    'htmlml_engine_acquire_next_scene',
  );
  final acknowledge =
      runtime.lookupFunction<_AcknowledgeNative, _AcknowledgeDart>(
    'htmlml_scene_acknowledge',
  );
  final release = runtime.lookupFunction<_ReleaseNative, _ReleaseDart>(
    'htmlml_scene_release',
  );

  final runtimeCString = cString(runtimePath);
  final cacheCString = cString(
    '${Directory.systemTemp.path}/htmlml-flutter-native-smoke',
  );
  final urlBytes = utf8.encode(arguments[2]);
  final url = malloc(urlBytes.length).cast<Uint8>()
    ..asTypedList(urlBytes.length).setAll(0, urlBytes);
  final input = malloc(sizeOf<InputEvent>()).cast<InputEvent>();
  Pointer<Void> engine = nullptr;
  try {
    engine = create(runtimeCString, cacheCString);
    if (engine == nullptr) {
      throw StateError('Bridge could not create an engine.');
    }
    if (load(engine, url, urlBytes.length) == 0) {
      throw StateError('The engine rejected the document URL.');
    }
    input.ref
      ..kind = 6
      ..sequence = 1
      ..x = 1200
      ..y = 800
      ..deltaX = 2;
    enqueue(engine, input);
    var scenes = 0;
    for (var attempt = 0;
        attempt < 120 && (scenes == 0 || resourceCount(engine) == 0);
        attempt++) {
      input.ref
        ..kind = 5
        ..sequence = attempt + 2
        ..x = attempt * 16.666;
      enqueue(engine, input);
      final scene = acquire(engine);
      if (scene != nullptr) {
        acknowledge(scene);
        release(scene);
        scenes++;
      }
      await Future<void>.delayed(const Duration(milliseconds: 250));
    }
    if (scenes == 0) {
      throw StateError('No native scene was published.');
    }
    if (resourceCount(engine) == 0) {
      throw StateError('The document did not load through the bridge.');
    }
    stdout.writeln(
      'Native Flutter bridge smoke passed '
      '($scenes scene, ${resourceCount(engine)} hosted resources).',
    );
  } finally {
    if (engine != nullptr) {
      destroy(engine);
    }
    free(input.cast());
    free(url.cast());
    free(cacheCString.cast());
    free(runtimeCString.cast());
  }
}
