using System.Diagnostics;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Product-independent reproduction for React concurrent-root scheduling in a virtual iframe.
/// The exact React 18.2.0 production UMD files are supplied externally so the
/// benchmark does not copy or depend on a product bundle.
/// </summary>
internal static class V8ReactSchedulerProbe
{
    internal static int Run(string[] args)
    {
        BenchmarkApp.EnsureInitialized();
        var fixtureRoot = ParseString(args, "--react-root")
                          ?? Environment.GetEnvironmentVariable("WEBSCENE_REACT_REPRO_ROOT")
                          ?? "/tmp/webscene-react-repro";
        var reactPath = Path.Combine(fixtureRoot, "react", "umd", "react.production.min.js");
        var reactDomPath = Path.Combine(fixtureRoot, "react-dom", "umd", "react-dom.production.min.js");
        var itemCount = Math.Max(0, ParseInt(args, "--items", 180));
        var syncNestedRoots = args.Contains("--sync-nested-roots", StringComparer.OrdinalIgnoreCase);
        var dynamicReactRuntime = args.Contains("--dynamic-react-runtime", StringComparer.OrdinalIgnoreCase);
        var benchmarkRoot = Path.Combine(BenchmarkPaths.RepoRoot, "benchmarks", "JavaScript.Avalonia.Benchmarks");
        if (!File.Exists(reactPath) || !File.Exists(reactDomPath))
        {
            Console.Error.WriteLine(
                "React scheduler fixture missing. Supply React 18.2.0 with " +
                $"--react-root <directory>; expected '{reactPath}' and '{reactDomPath}'.");
            return 2;
        }

        var window = new Window
        {
            Width = 800,
            Height = 480,
            Content = new CssLayoutPanel { ClipToBounds = true }
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window)
        {
            ScriptBaseDirectory = benchmarkRoot
        };
        using var runtime = new ClearScriptV8Runtime(
            host,
            new ClearScriptV8RuntimeOptions
            {
                EnableTrustedSameOriginContextSharing = true
            });

        try
        {
            var react = File.ReadAllText(reactPath);
            var reactDom = File.ReadAllText(reactDomPath);
            var bootstrap = $$"""
                (function () {
                  const sequence = window.__webSceneReactResourceSequence = [];
                  window.__webSceneReactProbeItemCount = {{itemCount}};
                  window.__webSceneReactSyncNestedRoots = {{syncNestedRoots.ToString().ToLowerInvariant()}};
                  window.__webSceneDomListenerOrder = [];
                  document.addEventListener('DOMContentLoaded', function () {
                    window.__webSceneDomListenerOrder.push('listener-1');
                    Promise.resolve().then(function () { window.__webSceneDomListenerOrder.push('microtask-1'); });
                  });
                  document.addEventListener('DOMContentLoaded', function () {
                    window.__webSceneDomListenerOrder.push('listener-2');
                  });
                  function mountLifecycleRoot(eventName) {
                    sequence.push(eventName);
                    const mount = document.createElement('div');
                    mount.className = 'lifecycle-probe-root';
                    document.body.appendChild(mount);
                    function LifecycleProbe() {
                      const state = React.useState(0);
                      React.useEffect(function () {
                        Promise.resolve().then(function () { state[1](1); });
                      }, []);
                      return React.createElement('div', { 'data-lifecycle': eventName }, eventName + ':' + state[0]);
                    }
                    const element = React.createElement(LifecycleProbe);
                    if (typeof ReactDOM.createRoot === 'function') ReactDOM.createRoot(mount).render(element);
                    else ReactDOM.render(element, mount);
                  }
                  document.addEventListener('DOMContentLoaded', function () { mountLifecycleRoot('dom-content-loaded'); });
                  window.addEventListener('load', function () { mountLifecycleRoot('window-load'); });
                  const stylesheet = document.createElement('link');
                  stylesheet.rel = 'stylesheet';
                  stylesheet.onload = function () {
                    sequence.push('style-load');
                    Promise.resolve().then(function () {
                      try {
                        sequence.push('style-promise');
                        const script = document.createElement('script');
                        script.onload = function () { sequence.push('script-load'); };
                        script.onerror = function (event) { sequence.push('script-error:' + String(event && event.message || '')); };
                        script.src = './Fixtures/v8-react-app.js';
                        document.head.appendChild(script);
                        sequence.push('script-appended');
                      } catch (error) {
                        sequence.push('exception:' + String(error && error.stack || error));
                      }
                    });
                  };
                  stylesheet.onerror = function (event) {
                    sequence.push('style-error:' + String(event && event.message || ''));
                  };
                  stylesheet.href = './Fixtures/v8-react-app.css';
                  document.head.appendChild(stylesheet);
                })();
                """;
            if (dynamicReactRuntime)
            {
                var appPath = Path.Combine(benchmarkRoot, "Fixtures", "v8-react-app.js");
                bootstrap = $$"""
                    (function () {
                      const sequence = window.__webSceneReactResourceSequence = [];
                      window.__webSceneReactProbeItemCount = {{itemCount}};
                      window.__webSceneReactSyncNestedRoots = {{syncNestedRoots.ToString().ToLowerInvariant()}};
                      function load(source, label, completed) {
                        const script = document.createElement('script');
                        script.onload = function () { sequence.push(label + '-load'); completed(); };
                        script.onerror = function (event) { sequence.push(label + '-error:' + String(event && event.message || '')); };
                        script.src = source;
                        document.head.appendChild(script);
                        sequence.push(label + '-appended');
                      }
                      load({{JsonSerializer.Serialize(reactPath)}}, 'react', function () {
                        load({{JsonSerializer.Serialize(reactDomPath)}}, 'react-dom', function () {
                          load({{JsonSerializer.Serialize(appPath)}}, 'app', function () {});
                        });
                      });
                    })();
                    """;
            }
            runtime.Execute($$"""
                globalThis.__webSceneReactFixture = {{JsonSerializer.Serialize(react)}};
                globalThis.__webSceneReactDomFixture = {{JsonSerializer.Serialize(reactDom)}};
                globalThis.__webSceneReactBootstrapFixture = {{JsonSerializer.Serialize(bootstrap)}};
                globalThis.__webSceneOwnerRead = function(index) { return 'owner-' + index; };
                globalThis.__webSceneOwnerSchedule = function(callback) {
                  setTimeout(callback, 0);
                };
                const iframe = document.createElement('iframe');
                iframe.style.width = '800px';
                iframe.style.height = '480px';
                document.body.appendChild(iframe);
                const markup = {{(dynamicReactRuntime
                    ? "'<!doctype html><html><body><script>' + __webSceneReactBootstrapFixture + '</' + 'script></body></html>'"
                    : "'<!doctype html><html><body>' + '<script>' + __webSceneReactFixture + '</' + 'script>' + '<script>' + __webSceneReactDomFixture + '</' + 'script>' + '<script>' + __webSceneReactBootstrapFixture + '</' + 'script>' + '</body></html>'")}};
                iframe.src = URL.createObjectURL(new Blob([markup], { type: 'text/html' }));
                window.__webSceneReactFrame = iframe;
                """, "v8-react-probe-owner.js");

            var started = Stopwatch.StartNew();
            VirtualIframeDomDocument? frameDocument = null;
            while (started.Elapsed < TimeSpan.FromSeconds(8))
            {
                Thread.Sleep(8);
                Dispatcher.UIThread.RunJobs();
                var iframe = host.Document.querySelector("iframe") as AvaloniaDomElement;
                frameDocument = iframe?.contentDocument as VirtualIframeDomDocument;
                if (frameDocument is not null && IsReady(frameDocument, requireLifecycleRoots: !dynamicReactRuntime))
                {
                    break;
                }
            }

            if (frameDocument is null)
            {
                Console.Error.WriteLine("V8 React scheduler repro failed: virtual iframe did not initialize.");
                return 1;
            }

            var buttons = frameDocument.querySelectorAll("button")
                .OfType<AvaloniaDomElement>()
                .ToArray();
            var labels = buttons.Select(button => button.textContent ?? string.Empty).ToArray();
            var roots = frameDocument.querySelectorAll(".probe-root").Length;
            var resourceState = Convert.ToString(runtime.Engine.Evaluate("""
                JSON.stringify({
                  sequence: window.__webSceneReactFrame.contentWindow.__webSceneReactResourceSequence || [],
                  scripts: window.__webSceneReactFrame.contentDocument.querySelectorAll('script').length,
                  chunkEvaluations: window.__webSceneReactFrame.contentWindow.__webSceneReactChunkEvaluationCount || 0,
                  chunkOrder: window.__webSceneReactFrame.contentWindow.__webSceneReactChunkOrder || [],
                  launcher: window.__webSceneReactFrame.contentDocument.querySelector('#launcher-ready').textContent,
                  lifecycle: Array.from(window.__webSceneReactFrame.contentDocument.querySelectorAll('[data-lifecycle]')).map(function(node) { return node.textContent; }),
                  listenerOrder: window.__webSceneReactFrame.contentWindow.__webSceneDomListenerOrder || [],
                  rootDir: typeof window.__webSceneReactFrame.contentDocument.documentElement.dir + ':' + String(window.__webSceneReactFrame.contentDocument.documentElement.dir),
                  buttonDir: typeof window.__webSceneReactFrame.contentDocument.querySelector('button').dir + ':' + String(window.__webSceneReactFrame.contentDocument.querySelector('button').dir),
                  readyState: window.__webSceneReactFrame.contentDocument.readyState
                })
                """));
            Console.WriteLine(
                $"V8 React scheduler repro: roots={roots}, buttons={buttons.Length}, " +
                $"labels=[{string.Join(", ", labels)}], resources={resourceState}, " +
                $"elapsed={started.Elapsed.TotalMilliseconds:F1} ms");
            var listenerOrder = Convert.ToString(runtime.Engine.Evaluate(
                "window.__webSceneReactFrame.contentWindow.__webSceneDomListenerOrder.join(',')"));
            return roots == 7 && buttons.Length == 7 && labels.All(label => label.EndsWith(":31", StringComparison.Ordinal))
                && string.Equals(
                    (frameDocument.querySelector("#launcher-ready") as AvaloniaDomElement)?.textContent,
                    "launcher:11",
                    StringComparison.Ordinal)
                && string.Equals(listenerOrder, "listener-1,listener-2,microtask-1", StringComparison.Ordinal)
                && (dynamicReactRuntime || frameDocument.querySelectorAll("[data-lifecycle]").OfType<AvaloniaDomElement>()
                    .Select(element => element.textContent ?? string.Empty)
                    .OrderBy(value => value, StringComparer.Ordinal)
                    .SequenceEqual(["dom-content-loaded:1", "window-load:1"]))
                ? 0
                : 1;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"V8 React scheduler repro failed: {exception}");
            return 1;
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static bool IsReady(VirtualIframeDomDocument document, bool requireLifecycleRoots)
    {
        var buttons = document.querySelectorAll("button").OfType<AvaloniaDomElement>().ToArray();
        return buttons.Length == 7
               && buttons.All(button => (button.textContent ?? string.Empty).EndsWith(":31", StringComparison.Ordinal))
               && string.Equals(
                   (document.querySelector("#launcher-ready") as AvaloniaDomElement)?.textContent,
                   "launcher:11",
                   StringComparison.Ordinal)
               && (!requireLifecycleRoots || document.querySelectorAll("[data-lifecycle]").OfType<AvaloniaDomElement>()
                   .Select(element => element.textContent ?? string.Empty)
                   .OrderBy(value => value, StringComparer.Ordinal)
                   .SequenceEqual(["dom-content-loaded:1", "window-load:1"]));
    }

    private static string? ParseString(string[] args, string name)
    {
        var index = Array.FindIndex(args, item => string.Equals(item, name, StringComparison.OrdinalIgnoreCase));
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }

    private static int ParseInt(string[] args, string name, int fallback)
    {
        var value = ParseString(args, name);
        return int.TryParse(value, out var parsed) ? parsed : fallback;
    }
}
