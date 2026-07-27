import 'package:meta/meta.dart';

import 'webscene_engine.dart';

/// Imperative access to the engine owned by an [WebSceneView].
final class WebSceneController {
  WebSceneEngine? _engine;

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
  void attach(WebSceneEngine engine) {
    if (_engine != null && _engine != engine) {
      throw StateError('The WebScene controller is already attached.');
    }
    _engine = engine;
  }

  @internal
  void detach(WebSceneEngine engine) {
    if (_engine == engine) _engine = null;
  }
}
