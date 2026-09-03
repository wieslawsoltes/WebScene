import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'package:ffi/ffi.dart';

enum WebSceneRuntimeState { unloaded, loading, ready, failed, disposed }

final class WebSceneDiagnosticContext {
  const WebSceneDiagnosticContext({
    this.generation = 0,
    this.sequence = 0,
    this.timestamp,
    this.documentUrl = '',
    this.frameId = 0,
    this.source = '',
    this.line = 0,
    this.column = 0,
    this.truncated = false,
  });
  factory WebSceneDiagnosticContext.fromJson(
    Map<String, dynamic> value,
    int generation,
  ) =>
      WebSceneDiagnosticContext(
        generation: generation,
        sequence: value['sequence'] as int? ?? 0,
        timestamp: DateTime.fromMillisecondsSinceEpoch(
          value['timestamp'] as int? ?? 0,
          isUtc: true,
        ),
        documentUrl: value['documentUrl'] as String? ?? '',
        frameId: value['frameId'] as int? ?? 0,
        source: value['source'] as String? ?? '',
        line: value['line'] as int? ?? 0,
        column: value['column'] as int? ?? 0,
        truncated: value['truncated'] as bool? ?? false,
      );
  final int generation, sequence, frameId, line, column;
  final DateTime? timestamp;
  final String documentUrl, source;
  final bool truncated;
}

final class WebSceneJavaScriptException {
  const WebSceneJavaScriptException(
    this.message, {
    this.stack = '',
    this.isUnhandledPromiseRejection = false,
    this.context = const WebSceneDiagnosticContext(),
  });
  final String message, stack;
  final bool isUnhandledPromiseRejection;
  final WebSceneDiagnosticContext context;
}

final class WebSceneConsoleArgument {
  const WebSceneConsoleArgument(this.type, this.value);
  final String type, value;
}

final class WebSceneConsoleMessage {
  const WebSceneConsoleMessage(
    this.level,
    this.message, {
    this.stack = '',
    this.arguments = const [],
    this.context = const WebSceneDiagnosticContext(),
  });
  final String level, message, stack;
  final List<WebSceneConsoleArgument> arguments;
  final WebSceneDiagnosticContext context;
}

final class WebSceneRuntimeFailure {
  const WebSceneRuntimeFailure(
    this.message, {
    this.stack = '',
    this.stage = 'application',
    this.context = const WebSceneDiagnosticContext(),
  });
  final String message, stack, stage;
  final WebSceneDiagnosticContext context;
}

typedef _Signal = Void Function(Pointer<Void>);
typedef _ConfigureNative = Void Function(
  Pointer<Void>,
  Uint32,
  Pointer<NativeFunction<_Signal>>,
  Pointer<Void>,
);
typedef _Configure = void Function(
  Pointer<Void>,
  int,
  Pointer<NativeFunction<_Signal>>,
  Pointer<Void>,
);
typedef _TakeNative = Size Function(Pointer<Void>, Pointer<Uint8>, Size);
typedef _Take = int Function(Pointer<Void>, Pointer<Uint8>, int);

/// NativeCallable posts to the Dart isolate; authored handlers never run in V8.
/// No console polling takes place on frame ticks.
final class NativeRuntimeDiagnostics {
  NativeRuntimeDiagnostics(DynamicLibrary library, this.engine, this.receive) {
    if (!library.providesSymbol('webscene_engine_configure_diagnostics') ||
        !library.providesSymbol('webscene_engine_take_diagnostic')) {
      return;
    }
    _configure = library.lookupFunction<_ConfigureNative, _Configure>(
      'webscene_engine_configure_diagnostics',
    );
    _take = library.lookupFunction<_TakeNative, _Take>(
      'webscene_engine_take_diagnostic',
    );
    _signal = NativeCallable<_Signal>.listener((Pointer<Void> _) => _drain());
  }
  final Pointer<Void> engine;
  void Function(Map<String, dynamic>) receive;
  _Configure? _configure;
  _Take? _take;
  NativeCallable<_Signal>? _signal;
  bool _disposed = false;
  void configure(int flags, {bool required = false}) {
    if (_disposed) return;
    if (_configure == null) {
      if (flags != 0 || required) {
        throw UnsupportedError(
          'Upgrade the native WebScene runtime to enable runtime diagnostics.',
        );
      }
      return;
    }
    _configure!(engine, flags, _signal!.nativeFunction, nullptr);
  }

  void _drain() {
    if (_disposed) return;
    for (var count = 0; count < 64; count++) {
      final size = _take!(engine, nullptr, 0);
      if (size == 0) return;
      final buffer = calloc<Uint8>(size);
      Map<String, dynamic>? record;
      try {
        final written = _take!(engine, buffer, size);
        if (written > 0 && written <= size) {
          record = jsonDecode(utf8.decode(buffer.asTypedList(written - 1)))
              as Map<String, dynamic>;
        }
      } finally {
        calloc.free(buffer);
      }
      if (record != null) receive(record);
      if (_disposed) return;
    }
    // Yield after a bounded batch instead of monopolising an animation frame.
    Timer.run(_drain);
  }

  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _configure?.call(engine, 0, nullptr, nullptr);
    _signal?.close();
  }
}
