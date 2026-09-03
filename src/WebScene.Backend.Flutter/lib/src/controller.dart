import 'package:meta/meta.dart';

import 'webscene_engine.dart';
import 'runtime_diagnostics.dart';

/// Imperative access to the engine owned by an [WebSceneView].
final class WebSceneController {
  WebSceneEngine? _engine;
  WebSceneRuntimeState _runtimeState = WebSceneRuntimeState.unloaded;
  WebSceneRuntimeFailure? _lastFailure;
  int _droppedDiagnosticCount = 0;
  WebSceneRuntimeState get runtimeState => _runtimeState;
  WebSceneRuntimeFailure? get lastFailure => _lastFailure;
  int get droppedDiagnosticCount => _droppedDiagnosticCount;

  @internal
  void setRuntimeState(WebSceneRuntimeState state,
      {WebSceneRuntimeFailure? failure}) {
    _runtimeState = state;
    if (state == WebSceneRuntimeState.loading) _lastFailure = null;
    if (failure != null) _lastFailure = failure;
  }

  @internal
  void recordDiagnosticLoss(int count) => _droppedDiagnosticCount += count;
  void Function(String, String)? _reportFatal;

  void reportFatalFailure(String message, {String stack = ''}) {
    final report = _reportFatal;
    if (report == null) {
      throw StateError('The WebScene controller is not attached.');
    }
    report(message, stack);
  }

  bool get isAttached => _engine != null && !_engine!.isDisposed;

  /// Queues JavaScript on the native engine worker.
  void executeScript(String source, {required String documentName}) {
    final engine = _engine;
    if (engine == null || engine.isDisposed) {
      throw StateError('The WebScene controller is not attached to a view.');
    }
    engine.executeScript(source, documentName);
  }

  /// Starts a new complete scene-diff chain for a recreated renderer.
  void requestSceneCheckpoint() {
    final engine = _engine;
    if (engine == null || engine.isDisposed) return;
    engine.requestCheckpoint();
  }

  @internal
  void attach(
    WebSceneEngine engine, {
    void Function(String, String)? reportFatal,
  }) {
    if (_engine != null && _engine != engine) {
      throw StateError('The WebScene controller is already attached.');
    }
    _engine = engine;
    _reportFatal = reportFatal;
  }

  @internal
  void detach(WebSceneEngine engine) {
    if (_engine == engine) {
      _engine = null;
      _reportFatal = null;
    }
  }
}
