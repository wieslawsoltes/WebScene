using System.Diagnostics;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using JavaScript.Avalonia;
using JavaScript.Avalonia.ClearScript;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Product-free contract spike for DOM wrapper identity across every path
/// used by React-style render and subscription code in owner and iframe realms.
/// </summary>
internal static class V8DomIdentityProbe
{
    internal const string MatrixScript = """
        (function () {
          const scopeName = String(globalThis.__webSceneIdentityScope || 'unknown');
          const container = document.createElement('div');
          container.id = 'identity-' + scopeName + '-container';
          const before = document.createElement('span');
          const target = document.createElement('button');
          const after = document.createElement('span');
          target.id = 'identity-' + scopeName + '-target';
          target.textContent = 'identity';

          const marker = { scope: scopeName };
          const symbol = Symbol('identity-' + scopeName);
          target.__webSceneIdentityMarker = marker;
          target[symbol] = marker;
          const state = {
            mutationDelivered: false,
            mutationTarget: false,
            mutationAdded: false,
            mutationMarker: false,
            syntheticEvent: false,
            nativeEvent: false
          };

          new MutationObserver(function (records) {
            records.forEach(function (record) {
              const added = Array.from(record.addedNodes || []);
              if (added.indexOf(target) >= 0) {
                state.mutationDelivered = true;
                state.mutationTarget = record.target === container;
                state.mutationAdded = added[added.indexOf(target)] === target;
                state.mutationMarker = added[added.indexOf(target)].__webSceneIdentityMarker === marker;
              }
            });
          }).observe(container, { childList: true });

          function recordEvent(event) {
            const valid = event.target === target &&
              event.currentTarget === target && this === target &&
              event.target.__webSceneIdentityMarker === marker;
            if (event.type === 'identity-synthetic') state.syntheticEvent = valid;
            if (event.type === 'click') state.nativeEvent = valid;
          }
          target.addEventListener('identity-synthetic', recordEvent);
          target.addEventListener('click', recordEvent);

          container.append(before, target, after);
          document.body.appendChild(container);
          target.dispatchEvent(new CustomEvent('identity-synthetic'));
          const computedStyle = getComputedStyle(target);

          globalThis.__webSceneIdentityTarget = target;
          globalThis.__snapshotDomIdentityMatrix = function () {
            const queried = document.querySelector('#' + target.id);
            const byId = document.getElementById(target.id);
            const descriptor = Object.getOwnPropertyDescriptor(target, '__webSceneIdentityMarker');
            const ownKeys = Object.getOwnPropertyNames(target);
            const ownSymbols = Object.getOwnPropertySymbols(target);
            return {
              querySelector: queried === target,
              getElementById: byId === target,
              parentElement: target.parentElement === container,
              parentNode: target.parentNode === container,
              children: container.children[1] === target,
              childNodes: Array.from(container.childNodes)[1] === target,
              previousSibling: target.previousElementSibling === before && before.nextElementSibling === target,
              nextSibling: target.nextElementSibling === after && after.previousElementSibling === target,
              ownerDocument: target.ownerDocument === document,
              closest: target.closest('#' + container.id) === container,
              contains: container.contains(target) && document.documentElement.contains(target),
              positionFollowing: (before.compareDocumentPosition(target) & 4) !== 0,
              positionPreceding: (target.compareDocumentPosition(before) & 2) !== 0,
              positionContainedBy: (container.compareDocumentPosition(target) & 16) !== 0,
              positionContains: (target.compareDocumentPosition(container) & 8) !== 0,
              computedStyleMethod: typeof computedStyle.getPropertyValue === 'function',
              computedStyleValue: typeof computedStyle.getPropertyValue === 'function' &&
                typeof computedStyle.getPropertyValue('display') === 'string',
              expandoThroughQuery: queried.__webSceneIdentityMarker === marker,
              ownStringKey: ownKeys.indexOf('__webSceneIdentityMarker') >= 0,
              ownSymbolKey: ownSymbols.indexOf(symbol) >= 0 && target[symbol] === marker,
              descriptor: !!descriptor && descriptor.value === marker && descriptor.writable === true &&
                descriptor.enumerable === true && descriptor.configurable === true,
              mutationDelivered: state.mutationDelivered,
              mutationTarget: state.mutationTarget,
              mutationAdded: state.mutationAdded,
              mutationMarker: state.mutationMarker,
              syntheticEvent: state.syntheticEvent,
              nativeEvent: state.nativeEvent
            };
          };
          globalThis.__snapshotDomIdentityMatrixJson = function () {
            return JSON.stringify(globalThis.__snapshotDomIdentityMatrix());
          };
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
            runtime.Execute("globalThis.__webSceneIdentityScope = 'owner';\n" + MatrixScript, "v8-owner-identity.js");
            var frameMarkup = "<!doctype html><html><body><script>" +
                              "globalThis.__webSceneIdentityScope = 'frame';\n" +
                              MatrixScript +
                              "</script></body></html>";
            runtime.Execute(
                "const iframe = document.createElement('iframe');" +
                "iframe.id = 'identity-frame';" +
                "document.body.appendChild(iframe);" +
                "iframe.src = URL.createObjectURL(new Blob([" + JsonSerializer.Serialize(frameMarkup) +
                "], { type: 'text/html' }));" +
                "window.__webSceneIdentityFrame = iframe;",
                "v8-frame-identity-owner.js");

            var timeout = Stopwatch.StartNew();
            VirtualIframeDomDocument? frameDocument = null;
            AvaloniaDomElement? frameTarget = null;
            while (timeout.Elapsed < TimeSpan.FromSeconds(5))
            {
                Thread.Sleep(4);
                Dispatcher.UIThread.RunJobs();
                var iframe = host.Document.querySelector("#identity-frame") as AvaloniaDomElement;
                frameDocument = iframe?.contentDocument as VirtualIframeDomDocument;
                frameTarget = frameDocument?.querySelector("#identity-frame-target") as AvaloniaDomElement;
                if (frameTarget is not null)
                {
                    break;
                }
            }

            var ownerTarget = host.Document.querySelector("#identity-owner-target") as AvaloniaDomElement;
            if (ownerTarget is null || frameDocument is null || frameTarget is null)
            {
                Console.Error.WriteLine("V8 DOM identity matrix failed: owner/frame targets did not initialize.");
                return 1;
            }

            RaiseNativeClick(ownerTarget.Control);
            RaiseNativeClick(frameTarget.Control);
            Dispatcher.UIThread.RunJobs();

            var ownerJson = Convert.ToString(runtime.Engine.Evaluate("window.__snapshotDomIdentityMatrixJson()")) ?? "{}";
            var frameJson = Convert.ToString(runtime.Engine.Evaluate(
                "window.__webSceneIdentityFrame.contentWindow.__snapshotDomIdentityMatrixJson()")) ?? "{}";
            var ownerPassed = MatrixPassed(ownerJson, out var ownerFailures);
            var framePassed = MatrixPassed(frameJson, out var frameFailures);
            Console.WriteLine(
                $"V8 owner DOM identity matrix: {(ownerPassed ? "pass" : "fail")}; " +
                $"failures=[{string.Join(",", ownerFailures)}]; {ownerJson}");
            Console.WriteLine(
                $"V8 iframe DOM identity matrix: {(framePassed ? "pass" : "fail")}; " +
                $"failures=[{string.Join(",", frameFailures)}]; {frameJson}");
            return ownerPassed && framePassed ? 0 : 1;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"V8 DOM identity matrix failed: {exception}");
            return 1;
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static bool MatrixPassed(string json, out string[] failures)
    {
        using var document = JsonDocument.Parse(json);
        failures = document.RootElement.EnumerateObject()
            .Where(property => property.Value.ValueKind != JsonValueKind.True)
            .Select(property => property.Name)
            .ToArray();
        return failures.Length == 0;
    }

    private static void RaiseNativeClick(Control target)
    {
        using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
        target.RaiseEvent(new PointerPressedEventArgs(
            target,
            pointer,
            target,
            new Point(1, 1),
            0,
            new PointerPointProperties(RawInputModifiers.LeftMouseButton, PointerUpdateKind.LeftButtonPressed),
            KeyModifiers.None));
        target.RaiseEvent(new PointerReleasedEventArgs(
            target,
            pointer,
            target,
            new Point(1, 1),
            1,
            new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.LeftButtonReleased),
            KeyModifiers.None,
            MouseButton.Left));
    }
}
