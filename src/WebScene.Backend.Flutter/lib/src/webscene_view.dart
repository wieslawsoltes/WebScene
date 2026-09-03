import 'dart:async';
import 'dart:convert';
import 'dart:ffi' show nullptr;
import 'dart:ui' as ui;

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';

import 'controller.dart';
import 'webscene_engine.dart';
import 'runtime_configuration.dart';
import 'scene_projector.dart';
import 'runtime_diagnostics.dart';
export 'runtime_diagnostics.dart' show WebSceneConsoleMessage;

typedef WebSceneHostRequestHandler = FutureOr<void> Function(
  WebSceneController controller,
  Map<String, dynamic> request,
);

/// A Flutter surface backed by the native WebScene V8/DOM/scene runtime.
class WebSceneView extends StatefulWidget {
  const WebSceneView({
    required this.documentUrl,
    required this.runtime,
    this.controller,
    this.initializationScripts = const [],
    this.onHostRequest,
    this.onReady,
    this.onConsoleMessage,
    this.onError,
    this.onScenePresented,
    this.onJavaScriptException,
    this.onRuntimeFailed,
    this.showRuntimeFailure = false,
    this.runtimeFailureBuilder,
    this.firstSceneTimeout = const Duration(seconds: 30),
    super.key,
  });

  final String documentUrl;
  final WebSceneRuntimeConfiguration runtime;
  final WebSceneController? controller;
  final List<WebSceneScript> initializationScripts;
  final WebSceneHostRequestHandler? onHostRequest;
  final VoidCallback? onReady;
  final ValueChanged<WebSceneConsoleMessage>? onConsoleMessage;
  final ValueChanged<Object>? onError;
  final ValueChanged<int>? onScenePresented;
  final ValueChanged<WebSceneJavaScriptException>? onJavaScriptException;
  final ValueChanged<WebSceneRuntimeFailure>? onRuntimeFailed;
  final bool showRuntimeFailure;
  final Widget Function(BuildContext, WebSceneRuntimeFailure)?
      runtimeFailureBuilder;
  final Duration? firstSceneTimeout;

  @override
  State<WebSceneView> createState() => _WebSceneViewState();
}

class _WebSceneViewState extends State<WebSceneView>
    with TickerProviderStateMixin, WidgetsBindingObserver {
  final _projector = WebSceneSceneProjector();
  final _focusNode = FocusNode(debugLabel: 'WebScene');
  late WebSceneController _controller;
  WebSceneEngine? _engine;
  Ticker? _ticker;
  Size _lastSize = Size.zero;
  double _lastScale = 0;
  int _buttons = 0;
  int _generation = 0;
  Timer? _firstSceneTimer;
  WebSceneRuntimeFailure? _failure;
  bool _ready = false;
  MouseCursor _cursor = SystemMouseCursors.basic;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _controller = widget.controller ?? WebSceneController();
    unawaited(_start());
  }

  @override
  void didUpdateWidget(covariant WebSceneView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.controller != widget.controller) {
      final engine = _engine;
      final nextController = widget.controller ?? WebSceneController();
      if (engine != null) {
        nextController.attach(engine, reportFatal: _reportFatal);
        _controller.detach(engine);
      }
      _controller = nextController;
    }
    if (oldWidget.documentUrl != widget.documentUrl ||
        oldWidget.runtime.runtimeLibraryPath !=
            widget.runtime.runtimeLibraryPath ||
        oldWidget.runtime.bridgeLibraryPath !=
            widget.runtime.bridgeLibraryPath ||
        oldWidget.runtime.compilationCacheDirectory !=
            widget.runtime.compilationCacheDirectory ||
        oldWidget.initializationScripts != widget.initializationScripts) {
      unawaited(_restart());
    } else if (_engine case final engine?) {
      _configureDiagnostics(engine, _generation);
    }
  }

  Future<void> _restart() async {
    _stopEngine();
    _projector.reset();
    _ready = false;
    _lastSize = Size.zero;
    _lastScale = 0;
    await _start();
  }

  Future<void> _start() async {
    final generation = ++_generation;
    _failure = null;
    _controller.setRuntimeState(WebSceneRuntimeState.loading);
    try {
      widget.runtime.validate();
      await Future<void>.delayed(Duration.zero);
      final engine = WebSceneEngine.create(
        runtimeLibrary: widget.runtime.runtimeLibraryPath,
        bridgeLibrary: widget.runtime.bridgeLibraryPath,
        cacheDirectory: widget.runtime.compilationCacheDirectory,
      );
      if (!mounted || generation != _generation) {
        engine.dispose();
        return;
      }
      _engine = engine;
      _controller.attach(engine, reportFatal: _reportFatal);
      _configureDiagnostics(engine, generation);
      engine
        ..requestCheckpoint()
        ..load(widget.documentUrl);
      for (final script in widget.initializationScripts) {
        engine.executeScript(script.source, script.documentName);
      }
      _ticker = createTicker(_onFrame)..start();
      if (widget.firstSceneTimeout case final timeout?) {
        _firstSceneTimer = Timer(timeout, () {
          if (mounted && generation == _generation && !_ready) {
            _fail(
              WebSceneRuntimeFailure(
                'The first document scene did not arrive in time.',
                stage: 'first-scene',
                context: WebSceneDiagnosticContext(
                  generation: generation,
                  documentUrl: widget.documentUrl,
                ),
              ),
            );
          }
        });
      }
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) _focusNode.requestFocus();
      });
    } catch (error, stack) {
      if (mounted && generation == _generation) {
        _fail(
          WebSceneRuntimeFailure(
            error.toString(),
            stack: stack.toString(),
            stage: 'load',
            context: WebSceneDiagnosticContext(
              generation: generation,
              documentUrl: widget.documentUrl,
            ),
          ),
        );
        _notify(() => widget.onError?.call(error));
      }
    }
  }

  void _onFrame(Duration elapsed) {
    if (!mounted) return;
    final engine = _engine;
    if (engine == null || engine.isDisposed) return;
    final size = context.size ?? Size.zero;
    if (size.isEmpty) return;

    _serviceHostRequests(engine);
    final scale = View.of(context).devicePixelRatio;
    final timestamp = elapsed.inMicroseconds / 1000;
    if (size != _lastSize || scale != _lastScale) {
      _lastSize = size;
      _lastScale = scale;
      engine.enqueueResizeFrame(
        width: size.width,
        height: size.height,
        scale: scale,
        timestampMilliseconds: timestamp,
      );
    } else {
      engine.enqueue(kind: webSceneFrame, x: timestamp);
    }

    var repaint = false;
    for (var count = 0; count < 4; count++) {
      final scene = engine.acquireNextScene();
      if (scene == nullptr) break;
      try {
        final result = _projector.apply(scene);
        if (!result.accepted || !engine.acknowledge(scene)) {
          engine.requestCheckpoint();
          break;
        }
        repaint = true;
        widget.onScenePresented?.call(result.revision);
        if (engine != _engine || engine.isDisposed) return;
        if (result.ready && !_ready) {
          _ready = true;
          _firstSceneTimer?.cancel();
          _controller.setRuntimeState(WebSceneRuntimeState.ready);
          widget.onReady?.call();
          if (engine != _engine || engine.isDisposed) return;
        }
      } catch (error, stack) {
        debugPrint('[WebScene scene] $error\n$stack');
        widget.onError?.call(error);
        engine.requestCheckpoint();
        break;
      } finally {
        engine.release(scene);
      }
    }

    final nextCursor = _mouseCursor(engine.cursor);
    if (nextCursor != _cursor) {
      _cursor = nextCursor;
      repaint = true;
    }
    if (repaint && mounted) setState(() {});
  }

  void _serviceHostRequests(WebSceneEngine engine) {
    for (final requestJson in engine.takeHostRequests()) {
      try {
        final decoded = jsonDecode(requestJson);
        if (decoded is! Map<String, dynamic>) {
          throw const FormatException('Host request must be a JSON object.');
        }
        final handler = widget.onHostRequest;
        if (handler != null) {
          unawaited(
            Future<void>.sync(() => handler(_controller, decoded)).catchError((
              Object error,
              StackTrace stack,
            ) {
              debugPrint('[WebScene host request] $error\n$stack');
              widget.onError?.call(error);
            }),
          );
        }
      } catch (error, stack) {
        debugPrint('[WebScene host request] $error\n$stack');
        widget.onError?.call(error);
      }
    }
  }

  void _pointer(int kind, PointerEvent event, {int? changedButton}) {
    final engine = _engine;
    if (engine == null) return;
    final buttonIndex = switch (changedButton) {
      kPrimaryMouseButton => 0,
      kMiddleMouseButton => 1,
      kSecondaryMouseButton => 2,
      _ => -1,
    };
    engine.enqueue(
      kind: kind,
      flags: _buttons |
          (buttonIndex < 0 ? 0 : (buttonIndex + 1) << 8) |
          (_modifierFlags() << 16),
      x: event.localPosition.dx,
      y: event.localPosition.dy,
    );
  }

  void _onPointerDown(PointerDownEvent event) {
    _buttons = event.buttons;
    _pointer(webScenePointerDown, event, changedButton: event.buttons);
    _focusNode.requestFocus();
  }

  void _onPointerUp(PointerUpEvent event) {
    final released = _buttons & ~event.buttons;
    _buttons = event.buttons;
    _pointer(webScenePointerUp, event, changedButton: released);
  }

  void _onPointerCancel(PointerCancelEvent event) {
    final released = _buttons;
    _buttons = 0;
    _pointer(webScenePointerUp, event, changedButton: released);
  }

  void _onPointerSignal(PointerSignalEvent event) {
    if (event is! PointerScrollEvent) return;
    _engine?.enqueue(
      kind: webSceneWheel,
      flags: _modifierFlags() << 16,
      x: event.localPosition.dx,
      y: event.localPosition.dy,
      deltaX: event.scrollDelta.dx,
      deltaY: event.scrollDelta.dy,
    );
  }

  KeyEventResult _onKeyEvent(FocusNode node, KeyEvent event) {
    final engine = _engine;
    if (engine == null) return KeyEventResult.ignored;
    final down = event is KeyDownEvent || event is KeyRepeatEvent;
    engine.enqueue(
      kind: down ? webSceneKeyDown : webSceneKeyUp,
      flags: _modifierFlags() | (event is KeyRepeatEvent ? 1 << 4 : 0),
      x: _domKeyCode(event.logicalKey).toDouble(),
    );
    if (down &&
        event.character != null &&
        !HardwareKeyboard.instance.isControlPressed &&
        !HardwareKeyboard.instance.isMetaPressed) {
      for (final scalar in event.character!.runes) {
        engine.enqueue(kind: webSceneText, x: scalar.toDouble());
      }
    }
    return KeyEventResult.handled;
  }

  int _modifierFlags() {
    final keyboard = HardwareKeyboard.instance;
    return (keyboard.isShiftPressed ? 1 : 0) |
        (keyboard.isControlPressed ? 2 : 0) |
        (keyboard.isAltPressed ? 4 : 0) |
        (keyboard.isMetaPressed ? 8 : 0);
  }

  static int _domKeyCode(LogicalKeyboardKey key) {
    if (key == LogicalKeyboardKey.backspace) return 8;
    if (key == LogicalKeyboardKey.tab) return 9;
    if (key == LogicalKeyboardKey.enter) return 13;
    if (key == LogicalKeyboardKey.escape) return 27;
    if (key == LogicalKeyboardKey.space) return 32;
    if (key == LogicalKeyboardKey.pageUp) return 33;
    if (key == LogicalKeyboardKey.pageDown) return 34;
    if (key == LogicalKeyboardKey.end) return 35;
    if (key == LogicalKeyboardKey.home) return 36;
    if (key == LogicalKeyboardKey.arrowLeft) return 37;
    if (key == LogicalKeyboardKey.arrowUp) return 38;
    if (key == LogicalKeyboardKey.arrowRight) return 39;
    if (key == LogicalKeyboardKey.arrowDown) return 40;
    if (key == LogicalKeyboardKey.delete) return 46;
    final label = key.keyLabel;
    return label.length == 1 ? label.toUpperCase().codeUnitAt(0) : 0;
  }

  static MouseCursor _mouseCursor(int cursor) => switch (cursor) {
        1 => SystemMouseCursors.click,
        2 => SystemMouseCursors.text,
        3 => SystemMouseCursors.precise,
        4 => SystemMouseCursors.wait,
        5 => SystemMouseCursors.move,
        6 => SystemMouseCursors.forbidden,
        7 => SystemMouseCursors.help,
        _ => SystemMouseCursors.basic,
      };

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _engine?.setVisible(state == AppLifecycleState.resumed);
  }

  @override
  void didHaveMemoryPressure() {
    _engine?.requestLowMemory();
  }

  void _stopEngine() {
    _generation++;
    _firstSceneTimer?.cancel();
    _ticker?.dispose();
    _ticker = null;
    final engine = _engine;
    _engine = null;
    if (engine != null) {
      _controller.detach(engine);
      engine.dispose();
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _stopEngine();
    _controller.setRuntimeState(WebSceneRuntimeState.disposed);
    _focusNode.dispose();
    _projector.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) =>
      _failure != null && widget.showRuntimeFailure
          ? widget.runtimeFailureBuilder?.call(context, _failure!) ??
              SingleChildScrollView(
                padding: const EdgeInsets.all(16),
                child: Column(
                  children: [
                    Text(_failure!.message),
                    ExpansionTile(
                      title: const Text('Error details'),
                      children: [SelectableText(_failure!.stack)],
                    ),
                  ],
                ),
              )
          : Focus(
              focusNode: _focusNode,
              autofocus: true,
              onKeyEvent: _onKeyEvent,
              child: MouseRegion(
                cursor: _cursor,
                child: Listener(
                  behavior: HitTestBehavior.opaque,
                  onPointerHover: (event) =>
                      _pointer(webScenePointerMove, event),
                  onPointerMove: (event) {
                    _buttons = event.buttons;
                    _pointer(webScenePointerMove, event);
                  },
                  onPointerDown: _onPointerDown,
                  onPointerUp: _onPointerUp,
                  onPointerCancel: _onPointerCancel,
                  onPointerSignal: _onPointerSignal,
                  child: RepaintBoundary(
                    child: CustomPaint(
                      painter: _WebScenePainter(_projector),
                      child: const SizedBox.expand(),
                    ),
                  ),
                ),
              ),
            );

  void _configureDiagnostics(WebSceneEngine engine, int generation) {
    engine.configureDiagnostics(
      (widget.onJavaScriptException != null ? 1 : 0) |
          (widget.onConsoleMessage != null ? 2 : 0),
      (record) {
        if (!mounted || generation != _generation || engine != _engine) return;
        final context = WebSceneDiagnosticContext.fromJson(record, generation);
        switch (record['kind']) {
          case 'dropped':
            _controller.recordDiagnosticLoss(record['droppedCount'] as int);
          case 'exception':
            final error = WebSceneJavaScriptException(
              record['message'] as String,
              stack: record['stack'] as String,
              isUnhandledPromiseRejection: record['promiseRejection'] as bool,
              context: context,
            );
            _notify(() => widget.onJavaScriptException?.call(error));
          case 'console':
            final message = WebSceneConsoleMessage(
              record['level'] as String,
              record['message'] as String,
              stack: record['stack'] as String,
              context: context,
              arguments: List.unmodifiable(
                (record['arguments'] as List).map(
                  (value) => WebSceneConsoleArgument(
                    value['type'] as String,
                    value['value'] as String,
                  ),
                ),
              ),
            );
            _notify(() => widget.onConsoleMessage?.call(message));
          case 'failure':
            _fail(
              WebSceneRuntimeFailure(
                record['message'] as String,
                stack: record['stack'] as String,
                stage: record['stage'] as String,
                context: context,
              ),
            );
        }
      },
      required: widget.onRuntimeFailed != null || widget.showRuntimeFailure,
    );
  }

  void _notify(VoidCallback callback) {
    try {
      callback();
    } catch (error, stack) {
      FlutterError.reportError(
        FlutterErrorDetails(
          exception: error,
          stack: stack,
          library: 'WebScene diagnostic subscriber',
        ),
      );
    }
  }

  void _reportFatal(String message, String stack) => _fail(
        WebSceneRuntimeFailure(
          message,
          stack: stack,
          context: WebSceneDiagnosticContext(
            generation: _generation,
            documentUrl: widget.documentUrl,
          ),
        ),
      );
  void _fail(WebSceneRuntimeFailure failure) {
    if (!mounted || _failure != null) return;
    _failure = failure;
    _controller.setRuntimeState(WebSceneRuntimeState.failed, failure: failure);
    _stopEngine();
    _projector.reset();
    setState(() {});
    _notify(() => widget.onRuntimeFailed?.call(failure));
  }
}

final class _WebScenePainter extends CustomPainter {
  _WebScenePainter(this.projector) : super(repaint: projector);

  final WebSceneSceneProjector projector;

  @override
  void paint(ui.Canvas canvas, ui.Size size) => projector.paint(canvas, size);

  @override
  bool shouldRepaint(covariant _WebScenePainter oldDelegate) => true;
}
