using System.Diagnostics;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Product-free reproduction for React concurrent commit scheduling around
/// an auto-focused native control and a neighboring root's passive effects.
/// </summary>
internal static class V8ReactFocusProbe
{
    internal static int Run(string[] args)
    {
        BenchmarkApp.EnsureInitialized();
        var fixtureRoot = ParseString(args, "--react-root")
                          ?? Environment.GetEnvironmentVariable("WEBSCENE_REACT_REPRO_ROOT")
                          ?? "/tmp/webscene-react-repro";
        var development = args.Contains("--development", StringComparer.OrdinalIgnoreCase);
        var useAutoFocus = !args.Contains("--no-auto-focus", StringComparer.OrdinalIgnoreCase);
        var legacyFocusRoot = args.Contains("--legacy-focus-root", StringComparer.OrdinalIgnoreCase);
        var scheduleFromRender = args.Contains("--schedule-from-render", StringComparer.OrdinalIgnoreCase);
        var reactFile = development ? "react.development.js" : "react.production.min.js";
        var reactDomFile = development ? "react-dom.development.js" : "react-dom.production.min.js";
        var reactPath = Path.Combine(fixtureRoot, "react", "umd", reactFile);
        var reactDomPath = Path.Combine(fixtureRoot, "react-dom", "umd", reactDomFile);
        var benchmarkRoot = Path.Combine(BenchmarkPaths.RepoRoot, "benchmarks", "JavaScript.Avalonia.Benchmarks");
        var fixturePath = Path.Combine(benchmarkRoot, "Fixtures", "v8-react-focus.js");
        if (!File.Exists(reactPath) || !File.Exists(reactDomPath))
        {
            Console.Error.WriteLine($"React focus fixture missing: '{reactPath}' or '{reactDomPath}'.");
            return 2;
        }

        var window = new Window
        {
            Width = 480,
            Height = 240,
            Content = new CssLayoutPanel { ClipToBounds = true }
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window) { ScriptBaseDirectory = benchmarkRoot };
        using var runtime = new ClearScriptV8Runtime(
            host,
            new ClearScriptV8RuntimeOptions { EnableTrustedSameOriginContextSharing = true });

        try
        {
            var react = File.ReadAllText(reactPath);
            var reactDom = File.ReadAllText(reactDomPath);
            var fixture = File.ReadAllText(fixturePath);
            runtime.Execute($$"""
                globalThis.__webSceneReactFixture = {{JsonSerializer.Serialize(react)}};
                globalThis.__webSceneReactDomFixture = {{JsonSerializer.Serialize(reactDom)}};
                globalThis.__webSceneReactFocusFixture = {{JsonSerializer.Serialize(fixture)}};
                const iframe = document.createElement('iframe');
                iframe.style.width = '480px';
                iframe.style.height = '240px';
                document.body.appendChild(iframe);
                const bootstrap =
                  'window.__webSceneReactUseAutoFocus = {{useAutoFocus.ToString().ToLowerInvariant()}};' +
                  'window.__webSceneReactLegacyFocusRoot = {{legacyFocusRoot.ToString().ToLowerInvariant()}};' +
                  'window.__webSceneReactScheduleFromRender = {{scheduleFromRender.ToString().ToLowerInvariant()}};';
                const markup = '<!doctype html><html><body>' +
                  '<script>' + __webSceneReactFixture + '</' + 'script>' +
                  '<script>' + __webSceneReactDomFixture + '</' + 'script>' +
                  '<script>' + bootstrap + __webSceneReactFocusFixture + '</' + 'script>' +
                  '</body></html>';
                iframe.src = URL.createObjectURL(new Blob([markup], { type: 'text/html' }));
                window.__webSceneReactFocusFrame = iframe;
                """, "v8-react-focus-owner.js");

            var started = Stopwatch.StartNew();
            VirtualIframeDomDocument? frameDocument = null;
            while (started.Elapsed < TimeSpan.FromSeconds(5))
            {
                Thread.Sleep(8);
                Dispatcher.UIThread.RunJobs();
                var iframe = host.Document.querySelector("iframe") as AvaloniaDomElement;
                frameDocument = iframe?.contentDocument as VirtualIframeDomDocument;
                if (frameDocument is not null
                    && string.Equals(ReadText(frameDocument, "#passive-probe-state"), "passive:1", StringComparison.Ordinal)
                    && string.Equals(ReadText(frameDocument, "#focus-probe-state"), "focus:1", StringComparison.Ordinal))
                {
                    break;
                }
            }

            if (frameDocument is null)
            {
                Console.Error.WriteLine("V8 React focus repro failed: virtual iframe did not initialize.");
                return 1;
            }

            var state = Convert.ToString(runtime.Engine.Evaluate("""
                JSON.stringify({
                  probe: window.__webSceneReactFocusFrame.contentWindow.__webSceneReactFocusState || null,
                  activeId: window.__webSceneReactFocusFrame.contentDocument.activeElement &&
                    window.__webSceneReactFocusFrame.contentDocument.activeElement.id || '',
                  inputValue: window.__webSceneReactFocusFrame.contentDocument.querySelector('#focus-probe-input') &&
                    window.__webSceneReactFocusFrame.contentDocument.querySelector('#focus-probe-input').value || '',
                  readyState: window.__webSceneReactFocusFrame.contentDocument.readyState
                })
                """));
            var passiveText = ReadText(frameDocument, "#passive-probe-state");
            var focusText = ReadText(frameDocument, "#focus-probe-state");
            var contractPassed = Convert.ToBoolean(runtime.Engine.Evaluate("""
                (function () {
                  const frame = window.__webSceneReactFocusFrame;
                  const probe = frame.contentWindow.__webSceneReactFocusState;
                  const contract = probe && probe.contract;
                  const input = frame.contentDocument.querySelector('#focus-probe-input');
                  return Boolean(contract &&
                    contract.constructorType === 'function' &&
                    contract.prototypeType === 'object' &&
                    contract.prototypeIsNull === false &&
                    contract.hasOwnPropertyType === 'function' &&
                    contract.instanceOfInput === true &&
                    contract.prototypeMatches === true &&
                    contract.hasOwnValue === false &&
                    contract.valueDescriptor === 'function:function' &&
                    contract.mediaQuery === 'boolean:true:ok' &&
                    input && input.value === 'value-1');
                })()
                """));
            var focusOrderPassed = Convert.ToBoolean(runtime.Engine.Evaluate(
                (useAutoFocus, scheduleFromRender) switch
                {
                    (true, true) => "window.__webSceneReactFocusFrame.contentWindow.__webSceneReactFocusState.focusEvents.join(',') === 'document-capture,root-capture,render-microtask,microtask'",
                    (true, false) => "window.__webSceneReactFocusFrame.contentWindow.__webSceneReactFocusState.focusEvents.join(',') === 'document-capture,root-capture,microtask'",
                    (false, true) => "window.__webSceneReactFocusFrame.contentWindow.__webSceneReactFocusState.focusEvents.join(',') === 'render-microtask'",
                    _ => "window.__webSceneReactFocusFrame.contentWindow.__webSceneReactFocusState.focusEvents.length === 0"
                }));
            Console.WriteLine(
                $"V8 React focus repro: autoFocus={useAutoFocus}, legacyFocusRoot={legacyFocusRoot}, " +
                $"scheduleFromRender={scheduleFromRender}, " +
                $"passive={passiveText ?? "<missing>"}, focus={focusText ?? "<missing>"}, " +
                $"state={state}, elapsed={started.Elapsed.TotalMilliseconds:F1} ms");
            return string.Equals(passiveText, "passive:1", StringComparison.Ordinal)
                   && string.Equals(focusText, "focus:1", StringComparison.Ordinal)
                   && contractPassed
                   && focusOrderPassed
                ? 0
                : 1;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"V8 React focus repro failed: {exception}");
            return 1;
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static string? ReadText(VirtualIframeDomDocument document, string selector)
        => (document.querySelector(selector) as AvaloniaDomElement)?.textContent;

    private static string? ParseString(string[] args, string name)
    {
        var index = Array.FindIndex(args, item => string.Equals(item, name, StringComparison.OrdinalIgnoreCase));
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
