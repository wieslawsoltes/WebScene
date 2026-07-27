using System.Diagnostics;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Product-free browser API contracts needed by lazy search and menu UI.
/// Runs the same checks in owner and virtual-iframe V8 realms.
/// </summary>
internal static class V8InteractionContractsProbe
{
    private const string IntersectionObserverScript = """
        (function () {
          const state = globalThis.__webSceneIntersectionState = {
            available: typeof IntersectionObserver === 'function',
            deliveries: 0,
            entered: false,
            exited: false,
            entryShape: false,
            observerShape: false,
            targetIdentity: false,
            movedX: -1,
            callbackMicrotask: false,
            inputRange: false,
            inputSelect: false,
            inputFocus: false,
            inputError: '',
            documentKeydown: 0,
            windowKeydown: 0,
            keyShape: false,
            done: false,
            error: ''
          };
          document.addEventListener('keydown', function (event) {
            state.documentKeydown++;
            state.keyShape = event.key === 'F' && event.code === 'KeyF' &&
              event.keyCode === 70 && event.which === 70 && event.charCode === 0 &&
              event.shiftKey === true;
          });
          window.addEventListener('keydown', function (event) {
            state.windowKeydown++;
          });
          if (!state.available) return;
          try {
            const target = document.createElement('div');
            target.id = 'intersection-target';
            target.style.position = 'absolute';
            target.style.left = '12px';
            target.style.top = '14px';
            target.style.width = '40px';
            target.style.height = '30px';
            document.body.appendChild(target);
            const observer = new IntersectionObserver(function (entries, currentObserver) {
              try {
                state.deliveries++;
                const entry = entries[entries.length - 1];
                state.targetIdentity = state.targetIdentity || entry.target === target;
                state.entryShape = state.entryShape ||
                  Number.isFinite(entry.time) &&
                  Number.isFinite(entry.boundingClientRect.width) &&
                  Number.isFinite(entry.intersectionRect.width) &&
                  typeof entry.intersectionRatio === 'number';
                if (entry.isIntersecting) {
                  state.entered = true;
                  target.style.left = '2000px';
                  state.movedX = target.getBoundingClientRect().x;
                } else if (state.entered) {
                  state.exited = true;
                  currentObserver.disconnect();
                  state.done = true;
                }
              } catch (error) {
                state.error = String(error && (error.stack || error.message) || error);
              }
            }, { threshold: [0, 0.5, 1] });
            state.observerShape = observer.root === null &&
              typeof observer.rootMargin === 'string' &&
              Array.isArray(observer.thresholds) && observer.thresholds.length === 3 &&
              typeof observer.takeRecords === 'function';
            observer.observe(target);
            setTimeout(function () {
              Promise.resolve().then(function () { state.callbackMicrotask = true; });
            }, 0);
            try {
              const input = document.createElement('input');
              input.id = 'interaction-input';
              input.value = 'NFLX';
              document.body.appendChild(input);
              input.focus();
              input.setSelectionRange(1, 3, 'backward');
              state.inputRange = input.selectionStart === 1 && input.selectionEnd === 3 &&
                input.selectionDirection === 'backward';
              input.select();
              state.inputSelect = input.selectionStart === 0 && input.selectionEnd === 4;
              state.inputFocus = document.activeElement === input;
            } catch (error) {
              state.inputError = String(error && (error.stack || error.message) || error);
            }
          } catch (error) {
            state.error = String(error && (error.stack || error.message) || error);
          }
        })();
        """;

    internal static int Run()
    {
        BenchmarkApp.EnsureInitialized();
        var window = new Window
        {
            Width = 420,
            Height = 260,
            Content = new CssLayoutPanel { ClipToBounds = true }
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(
            host,
            new ClearScriptV8RuntimeOptions { EnableTrustedSameOriginContextSharing = true });

        try
        {
            runtime.Execute(IntersectionObserverScript, "v8-owner-intersection-observer.js");
            var frameMarkup = "<!doctype html><html><body><script>" +
                              IntersectionObserverScript +
                              "</script></body></html>";
            runtime.Execute(
                "const iframe = document.createElement('iframe');" +
                "iframe.id = 'interaction-contract-frame';" +
                "iframe.style.width = '320px'; iframe.style.height = '180px';" +
                "document.body.appendChild(iframe);" +
                "iframe.src = URL.createObjectURL(new Blob([" + JsonSerializer.Serialize(frameMarkup) +
                "], { type: 'text/html' }));" +
                "window.__webSceneInteractionContractFrame = iframe;",
                "v8-interaction-contract-frame-owner.js");

            var timeout = Stopwatch.StartNew();
            while (timeout.Elapsed < TimeSpan.FromSeconds(5))
            {
                Thread.Sleep(8);
                Dispatcher.UIThread.RunJobs();
                var ownerDone = Convert.ToBoolean(runtime.Engine.Evaluate(
                    "Boolean(window.__webSceneIntersectionState && window.__webSceneIntersectionState.done)"));
                var frameDone = Convert.ToBoolean(runtime.Engine.Evaluate(
                    "Boolean(window.__webSceneInteractionContractFrame && " +
                    "window.__webSceneInteractionContractFrame.contentWindow && " +
                    "window.__webSceneInteractionContractFrame.contentWindow.__webSceneIntersectionState && " +
                    "window.__webSceneInteractionContractFrame.contentWindow.__webSceneIntersectionState.done)"));
                if (ownerDone && frameDone)
                {
                    break;
                }
            }

            var ownerInput = (AvaloniaDomElement)host.Document.getElementById("interaction-input")!;
            RaiseShiftF(ownerInput.Control);
            var iframe = (AvaloniaDomElement)host.Document.getElementById("interaction-contract-frame")!;
            var frameDocument = (VirtualIframeDomDocument)iframe.contentDocument!;
            var frameInput = (AvaloniaDomElement)frameDocument.getElementById("interaction-input")!;
            RaiseShiftF(frameInput.Control);
            Dispatcher.UIThread.RunJobs();

            var ownerJson = Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(window.__webSceneIntersectionState)")) ?? "{}";
            var frameJson = Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(window.__webSceneInteractionContractFrame.contentWindow.__webSceneIntersectionState)")) ?? "{}";
            var ownerPassed = IntersectionStatePassed(ownerJson);
            var framePassed = IntersectionStatePassed(frameJson);
            Console.WriteLine(
                $"V8 owner IntersectionObserver: {(ownerPassed ? "pass" : "fail")}; {ownerJson}");
            Console.WriteLine(
                $"V8 iframe IntersectionObserver: {(framePassed ? "pass" : "fail")}; {frameJson}");
            return ownerPassed && framePassed ? 0 : 1;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"V8 interaction contracts failed: {exception}");
            return 1;
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static bool IntersectionStatePassed(string json)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        return root.GetProperty("available").GetBoolean()
               && root.GetProperty("deliveries").GetInt32() >= 2
               && root.GetProperty("entered").GetBoolean()
               && root.GetProperty("exited").GetBoolean()
               && root.GetProperty("entryShape").GetBoolean()
               && root.GetProperty("observerShape").GetBoolean()
               && root.GetProperty("targetIdentity").GetBoolean()
               && root.GetProperty("callbackMicrotask").GetBoolean()
               && root.GetProperty("inputRange").GetBoolean()
               && root.GetProperty("inputSelect").GetBoolean()
               && root.GetProperty("inputFocus").GetBoolean()
               && string.IsNullOrEmpty(root.GetProperty("inputError").GetString())
               && root.GetProperty("documentKeydown").GetInt32() == 1
               && root.GetProperty("windowKeydown").GetInt32() == 1
               && root.GetProperty("keyShape").GetBoolean()
               && root.GetProperty("done").GetBoolean()
               && string.IsNullOrEmpty(root.GetProperty("error").GetString());
    }

    private static void RaiseShiftF(Control target)
    {
        target.RaiseEvent(new KeyEventArgs
        {
            RoutedEvent = InputElement.KeyDownEvent,
            Source = target,
            Key = Key.F,
            KeyModifiers = KeyModifiers.Shift
        });
    }
}
