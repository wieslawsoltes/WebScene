// ignore_for_file: library_private_types_in_public_api

import 'dart:ffi';

import 'package:ffi/ffi.dart';

final class WebSceneInputEvent extends Struct {
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

final class WebSceneSceneHeader extends Struct {
  @Uint64()
  external int revision;

  @Uint64()
  external int baseRevision;

  @Uint64()
  external int consumedInputSequence;

  @Float()
  external double viewportWidth;

  @Float()
  external double viewportHeight;

  @Uint32()
  external int commandCount;

  @Uint32()
  external int canvasLayerCount;

  @Uint32()
  external int damageRectCount;

  @Uint32()
  external int flags;

  @Uint64()
  external int contentHash;
}

final class WebSceneSceneCommand extends Struct {
  @Uint32()
  external int kind;

  @Uint32()
  external int flags;

  @Float()
  external double x;

  @Float()
  external double y;

  @Float()
  external double width;

  @Float()
  external double height;

  @Uint32()
  external int rgba;

  @Uint32()
  external int nodeId;

  @Float()
  external double radiusTopLeft;

  @Float()
  external double radiusTopRight;

  @Float()
  external double radiusBottomRight;

  @Float()
  external double radiusBottomLeft;

  @Float()
  external double strokeWidth;
}

final class WebSceneCanvasLayer extends Struct {
  @Uint32()
  external int nodeId;

  @Uint32()
  external int flags;

  @Uint32()
  external int commandOffset;

  @Uint32()
  external int commandCount;

  @Uint32()
  external int stringOffset;

  @Uint32()
  external int stringCount;

  @Uint32()
  external int zOrder;

  @Float()
  external double x;

  @Float()
  external double y;

  @Float()
  external double width;

  @Float()
  external double height;

  @Uint32()
  external int bitmapWidth;

  @Uint32()
  external int bitmapHeight;

  @Uint64()
  external int generation;
}

final class WebSceneCanvasCommand extends Struct {
  @Uint32()
  external int kind;

  @Uint32()
  external int flags;

  @Uint32()
  external int resourceId;

  @Uint32()
  external int reserved;

  @Array(8)
  external Array<Double> values;
}

final class WebSceneSceneString extends Struct {
  @Uint32()
  external int byteOffset;

  @Uint32()
  external int byteLength;
}

final class WebSceneDamageRect extends Struct {
  @Float()
  external double x;

  @Float()
  external double y;

  @Float()
  external double width;

  @Float()
  external double height;
}

final class WebSceneSceneView extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int abiVersion;

  external WebSceneSceneHeader header;

  external Pointer<WebSceneSceneCommand> commands;
  external Pointer<WebSceneCanvasLayer> canvasLayers;
  external Pointer<WebSceneCanvasCommand> canvasCommands;
  external Pointer<WebSceneSceneString> strings;
  external Pointer<Uint8> stringBytes;
  external Pointer<WebSceneDamageRect> damageRects;
  external Pointer<Void> leaseToken;

  @Uint32()
  external int canvasCommandCount;

  @Uint32()
  external int stringCount;

  @Uint32()
  external int stringByteCount;

  @Uint32()
  external int reserved;
}

typedef _GetAbiNative = Uint32 Function();
typedef _GetAbiDart = int Function();
typedef _LoadUrlNative = Uint8 Function(Pointer<Void>, Pointer<Uint8>, IntPtr);
typedef _LoadUrlDart = int Function(Pointer<Void>, Pointer<Uint8>, int);
typedef _EnqueueNative = Uint8 Function(
    Pointer<Void>, Pointer<WebSceneInputEvent>);
typedef _EnqueueDart = int Function(Pointer<Void>, Pointer<WebSceneInputEvent>);
typedef _EnqueueResizeFrameNative = Uint8 Function(
  Pointer<Void>,
  Pointer<WebSceneInputEvent>,
  Pointer<WebSceneInputEvent>,
);
typedef _EnqueueResizeFrameDart = int Function(
  Pointer<Void>,
  Pointer<WebSceneInputEvent>,
  Pointer<WebSceneInputEvent>,
);
typedef _AcquireNative = Pointer<WebSceneSceneView> Function(Pointer<Void>);
typedef _AcquireDart = Pointer<WebSceneSceneView> Function(Pointer<Void>);
typedef _SceneAcknowledgeNative = Uint8 Function(Pointer<WebSceneSceneView>);
typedef _SceneAcknowledgeDart = int Function(Pointer<WebSceneSceneView>);
typedef _SceneReleaseNative = Void Function(Pointer<WebSceneSceneView>);
typedef _SceneReleaseDart = void Function(Pointer<WebSceneSceneView>);
typedef _RequestCheckpointNative = Uint8 Function(Pointer<Void>);
typedef _RequestCheckpointDart = int Function(Pointer<Void>);
typedef _GetCursorNative = Uint32 Function(Pointer<Void>);
typedef _GetCursorDart = int Function(Pointer<Void>);
typedef _SetVisibleNative = Uint8 Function(Pointer<Void>, Uint8);
typedef _SetVisibleDart = int Function(Pointer<Void>, int);
typedef _RequestLowMemoryNative = Uint8 Function(Pointer<Void>);
typedef _RequestLowMemoryDart = int Function(Pointer<Void>);
typedef _ExecuteScriptNative = Uint8 Function(
  Pointer<Void>,
  Pointer<Uint8>,
  IntPtr,
  Pointer<Uint8>,
  IntPtr,
);
typedef _ExecuteScriptDart = int Function(
    Pointer<Void>, Pointer<Uint8>, int, Pointer<Uint8>, int);
typedef _CopyTextNative = IntPtr Function(
    Pointer<Void>, Pointer<Uint8>, IntPtr);
typedef _CopyTextDart = int Function(Pointer<Void>, Pointer<Uint8>, int);

final class WebSceneNativeApi {
  WebSceneNativeApi(DynamicLibrary library)
      : getAbiVersion = library.lookupFunction<_GetAbiNative, _GetAbiDart>(
          'webscene_engine_get_abi_version',
        ),
        loadUrl = library.lookupFunction<_LoadUrlNative, _LoadUrlDart>(
          'webscene_engine_load_url',
        ),
        enqueue = library.lookupFunction<_EnqueueNative, _EnqueueDart>(
          'webscene_engine_enqueue',
        ),
        enqueueResizeFrame = library
            .lookupFunction<_EnqueueResizeFrameNative, _EnqueueResizeFrameDart>(
          'webscene_engine_enqueue_resize_frame',
        ),
        acquireNextScene = library.lookupFunction<_AcquireNative, _AcquireDart>(
          'webscene_engine_acquire_next_scene',
        ),
        acknowledge = library
            .lookupFunction<_SceneAcknowledgeNative, _SceneAcknowledgeDart>(
          'webscene_scene_acknowledge',
        ),
        release =
            library.lookupFunction<_SceneReleaseNative, _SceneReleaseDart>(
          'webscene_scene_release',
        ),
        requestCheckpoint = library
            .lookupFunction<_RequestCheckpointNative, _RequestCheckpointDart>(
          'webscene_engine_request_scene_checkpoint',
        ),
        getCursor = library.lookupFunction<_GetCursorNative, _GetCursorDart>(
          'webscene_engine_get_cursor',
        ),
        setVisible = library.lookupFunction<_SetVisibleNative, _SetVisibleDart>(
          'webscene_engine_set_visible',
        ),
        requestLowMemory = library
            .lookupFunction<_RequestLowMemoryNative, _RequestLowMemoryDart>(
          'webscene_engine_request_low_memory',
        ),
        executeScript =
            library.lookupFunction<_ExecuteScriptNative, _ExecuteScriptDart>(
          'webscene_engine_execute_script',
        ),
        takeHostRequest =
            library.lookupFunction<_CopyTextNative, _CopyTextDart>(
          'webscene_engine_take_host_request',
        ),
        takeConsoleMessage =
            library.lookupFunction<_CopyTextNative, _CopyTextDart>(
          'webscene_engine_take_console_message',
        ),
        copyLastError = library.lookupFunction<_CopyTextNative, _CopyTextDart>(
          'webscene_engine_copy_last_error',
        );

  final _GetAbiDart getAbiVersion;
  final _LoadUrlDart loadUrl;
  final _EnqueueDart enqueue;
  final _EnqueueResizeFrameDart enqueueResizeFrame;
  final _AcquireDart acquireNextScene;
  final _SceneAcknowledgeDart acknowledge;
  final _SceneReleaseDart release;
  final _RequestCheckpointDart requestCheckpoint;
  final _GetCursorDart getCursor;
  final _SetVisibleDart setVisible;
  final _RequestLowMemoryDart requestLowMemory;
  final _ExecuteScriptDart executeScript;
  final _CopyTextDart takeHostRequest;
  final _CopyTextDart takeConsoleMessage;
  final _CopyTextDart copyLastError;
}

typedef _BridgeCreateNative = Pointer<Void> Function(
    Pointer<Utf8>, Pointer<Utf8>);
typedef _BridgeCreateDart = Pointer<Void> Function(
    Pointer<Utf8>, Pointer<Utf8>);
typedef _BridgeDestroyNative = Void Function(Pointer<Void>);
typedef _BridgeDestroyDart = void Function(Pointer<Void>);
typedef _BridgeErrorNative = Pointer<Utf8> Function();
typedef _BridgeErrorDart = Pointer<Utf8> Function();

final class WebSceneBridgeApi {
  WebSceneBridgeApi(DynamicLibrary library)
      : create = library.lookupFunction<_BridgeCreateNative, _BridgeCreateDart>(
          'webscene_flutter_engine_create',
        ),
        destroy =
            library.lookupFunction<_BridgeDestroyNative, _BridgeDestroyDart>(
          'webscene_flutter_engine_destroy',
        ),
        lastError =
            library.lookupFunction<_BridgeErrorNative, _BridgeErrorDart>(
          'webscene_flutter_last_error',
        );

  final _BridgeCreateDart create;
  final _BridgeDestroyDart destroy;
  final _BridgeErrorDart lastError;
}
