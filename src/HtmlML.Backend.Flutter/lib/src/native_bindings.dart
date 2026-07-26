// ignore_for_file: library_private_types_in_public_api

import 'dart:ffi';

import 'package:ffi/ffi.dart';

final class HtmlMlInputEvent extends Struct {
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

final class HtmlMlSceneHeader extends Struct {
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

final class HtmlMlSceneCommand extends Struct {
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

final class HtmlMlCanvasLayer extends Struct {
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

final class HtmlMlCanvasCommand extends Struct {
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

final class HtmlMlSceneString extends Struct {
  @Uint32()
  external int byteOffset;

  @Uint32()
  external int byteLength;
}

final class HtmlMlDamageRect extends Struct {
  @Float()
  external double x;

  @Float()
  external double y;

  @Float()
  external double width;

  @Float()
  external double height;
}

final class HtmlMlSceneView extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int abiVersion;

  external HtmlMlSceneHeader header;

  external Pointer<HtmlMlSceneCommand> commands;
  external Pointer<HtmlMlCanvasLayer> canvasLayers;
  external Pointer<HtmlMlCanvasCommand> canvasCommands;
  external Pointer<HtmlMlSceneString> strings;
  external Pointer<Uint8> stringBytes;
  external Pointer<HtmlMlDamageRect> damageRects;
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
    Pointer<Void>, Pointer<HtmlMlInputEvent>);
typedef _EnqueueDart = int Function(Pointer<Void>, Pointer<HtmlMlInputEvent>);
typedef _EnqueueResizeFrameNative = Uint8 Function(
  Pointer<Void>,
  Pointer<HtmlMlInputEvent>,
  Pointer<HtmlMlInputEvent>,
);
typedef _EnqueueResizeFrameDart = int Function(
  Pointer<Void>,
  Pointer<HtmlMlInputEvent>,
  Pointer<HtmlMlInputEvent>,
);
typedef _AcquireNative = Pointer<HtmlMlSceneView> Function(Pointer<Void>);
typedef _AcquireDart = Pointer<HtmlMlSceneView> Function(Pointer<Void>);
typedef _SceneAcknowledgeNative = Uint8 Function(Pointer<HtmlMlSceneView>);
typedef _SceneAcknowledgeDart = int Function(Pointer<HtmlMlSceneView>);
typedef _SceneReleaseNative = Void Function(Pointer<HtmlMlSceneView>);
typedef _SceneReleaseDart = void Function(Pointer<HtmlMlSceneView>);
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

final class HtmlMlNativeApi {
  HtmlMlNativeApi(DynamicLibrary library)
      : getAbiVersion = library.lookupFunction<_GetAbiNative, _GetAbiDart>(
          'htmlml_engine_get_abi_version',
        ),
        loadUrl = library.lookupFunction<_LoadUrlNative, _LoadUrlDart>(
          'htmlml_engine_load_url',
        ),
        enqueue = library.lookupFunction<_EnqueueNative, _EnqueueDart>(
          'htmlml_engine_enqueue',
        ),
        enqueueResizeFrame = library
            .lookupFunction<_EnqueueResizeFrameNative, _EnqueueResizeFrameDart>(
          'htmlml_engine_enqueue_resize_frame',
        ),
        acquireNextScene = library.lookupFunction<_AcquireNative, _AcquireDart>(
          'htmlml_engine_acquire_next_scene',
        ),
        acknowledge = library
            .lookupFunction<_SceneAcknowledgeNative, _SceneAcknowledgeDart>(
          'htmlml_scene_acknowledge',
        ),
        release =
            library.lookupFunction<_SceneReleaseNative, _SceneReleaseDart>(
          'htmlml_scene_release',
        ),
        requestCheckpoint = library
            .lookupFunction<_RequestCheckpointNative, _RequestCheckpointDart>(
          'htmlml_engine_request_scene_checkpoint',
        ),
        getCursor = library.lookupFunction<_GetCursorNative, _GetCursorDart>(
          'htmlml_engine_get_cursor',
        ),
        setVisible = library.lookupFunction<_SetVisibleNative, _SetVisibleDart>(
          'htmlml_engine_set_visible',
        ),
        requestLowMemory = library
            .lookupFunction<_RequestLowMemoryNative, _RequestLowMemoryDart>(
          'htmlml_engine_request_low_memory',
        ),
        executeScript =
            library.lookupFunction<_ExecuteScriptNative, _ExecuteScriptDart>(
          'htmlml_engine_execute_script',
        ),
        takeHostRequest =
            library.lookupFunction<_CopyTextNative, _CopyTextDart>(
          'htmlml_engine_take_host_request',
        ),
        takeConsoleMessage =
            library.lookupFunction<_CopyTextNative, _CopyTextDart>(
          'htmlml_engine_take_console_message',
        ),
        copyLastError = library.lookupFunction<_CopyTextNative, _CopyTextDart>(
          'htmlml_engine_copy_last_error',
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

final class HtmlMlBridgeApi {
  HtmlMlBridgeApi(DynamicLibrary library)
      : create = library.lookupFunction<_BridgeCreateNative, _BridgeCreateDart>(
          'htmlml_flutter_engine_create',
        ),
        destroy =
            library.lookupFunction<_BridgeDestroyNative, _BridgeDestroyDart>(
          'htmlml_flutter_engine_destroy',
        ),
        lastError =
            library.lookupFunction<_BridgeErrorNative, _BridgeErrorDart>(
          'htmlml_flutter_last_error',
        );

  final _BridgeCreateDart create;
  final _BridgeDestroyDart destroy;
  final _BridgeErrorDart lastError;
}
