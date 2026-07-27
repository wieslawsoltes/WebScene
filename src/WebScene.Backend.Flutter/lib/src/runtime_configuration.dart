import 'dart:io';

/// Native libraries and cache settings used by an [WebSceneView].
final class WebSceneRuntimeConfiguration {
  const WebSceneRuntimeConfiguration({
    required this.runtimeLibraryPath,
    required this.bridgeLibraryPath,
    this.compilationCacheDirectory,
  });

  /// ABI-v2 WebScene native engine library.
  final String runtimeLibraryPath;

  /// Worker-safe Flutter host bridge library for the current platform.
  final String bridgeLibraryPath;

  /// Persistent V8 compilation cache. A temporary cache is used when omitted.
  final String? compilationCacheDirectory;

  void validate() {
    if (!Platform.isMacOS) {
      throw UnsupportedError(
        'WebScene.Backend.Flutter currently supports macOS only.',
      );
    }
    if (!File(runtimeLibraryPath).existsSync()) {
      throw StateError('WebScene runtime not found at $runtimeLibraryPath');
    }
    if (!File(bridgeLibraryPath).existsSync()) {
      throw StateError('WebScene Flutter bridge not found at $bridgeLibraryPath');
    }
  }
}

/// JavaScript queued after the document request.
final class WebSceneScript {
  const WebSceneScript(this.source, {required this.documentName});

  final String source;
  final String documentName;
}
