using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Input.Raw;
using Avalonia.Threading;
using WebScene.Core;
using System.Net;
using System.Net.Http.Headers;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class HostServiceSeamTests
{
    [Fact]
    public void HttpResourceFreshnessHonorsOriginPolicyAndBoundedValidatorHeuristics()
    {
        var now = DateTimeOffset.UtcNow;
        using var explicitResponse = new HttpResponseMessage(HttpStatusCode.OK);
        explicitResponse.Headers.Date = now - TimeSpan.FromSeconds(10);
        explicitResponse.Headers.CacheControl = new CacheControlHeaderValue
        {
            MaxAge = TimeSpan.FromMinutes(2)
        };
        var explicitPolicy = AvaloniaResourceLoader.ReadHttpCachePolicy(
            explicitResponse,
            now - TimeSpan.FromDays(30));

        Assert.True(explicitPolicy.IsCacheable);
        Assert.InRange(
            explicitPolicy.FreshUntil!.Value,
            now + TimeSpan.FromSeconds(100),
            now + TimeSpan.FromSeconds(115));

        using var noStoreResponse = new HttpResponseMessage(HttpStatusCode.OK);
        noStoreResponse.Headers.CacheControl = new CacheControlHeaderValue { NoStore = true };
        var noStorePolicy = AvaloniaResourceLoader.ReadHttpCachePolicy(noStoreResponse, now);
        Assert.False(noStorePolicy.IsCacheable);
        Assert.Null(noStorePolicy.FreshUntil);

        using var revalidateResponse = new HttpResponseMessage(HttpStatusCode.OK);
        revalidateResponse.Headers.CacheControl = new CacheControlHeaderValue { NoCache = true };
        var revalidatePolicy = AvaloniaResourceLoader.ReadHttpCachePolicy(revalidateResponse, now);
        Assert.True(revalidatePolicy.IsCacheable);
        Assert.Null(revalidatePolicy.FreshUntil);

        using var validatorOnlyResponse = new HttpResponseMessage(HttpStatusCode.OK);
        validatorOnlyResponse.Headers.Date = now;
        var heuristicPolicy = AvaloniaResourceLoader.ReadHttpCachePolicy(
            validatorOnlyResponse,
            now - TimeSpan.FromDays(30));
        Assert.True(heuristicPolicy.IsCacheable);
        Assert.InRange(
            heuristicPolicy.FreshUntil!.Value,
            now + TimeSpan.FromMinutes(59),
            now + TimeSpan.FromMinutes(61));
    }

    [AvaloniaFact]
    public void PublicHostConstructorExposesPortableServicesAndOpaqueHandles()
    {
        var root = new Canvas();
        var window = new Window { Width = 640, Height = 480, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        var element = HostTestUtilities.GetElement(host.Document.createElement("div"));

        Assert.Same(window, host.Services.RootHandle.GetRequired<TopLevel>());
        Assert.Same(element.Control, element.BackendHandle.GetRequired<Control>());
        Assert.Equal(element.__webSceneDomIdentity, element.DomNodeId.Value);
        Assert.NotEqual(default, element.DomNodeId);
        Assert.True(host.Services.Dispatcher.CheckAccess());
        Assert.Equal(640, host.Services.Viewport.Metrics.ClientSize.Width);
        Assert.Equal(480, host.Services.Viewport.Metrics.ClientSize.Height);
        Assert.Equal(window.RenderScaling, host.Services.Viewport.Metrics.DeviceScaleFactor);
        Assert.NotNull(host.Services.Input);

        window.Close();
    }

    [AvaloniaFact]
    public void ResourceLoadingUsesPortableContractWithoutChangingPublicResolutionApi()
    {
        var fixtureDirectory = Path.Combine(AppContext.BaseDirectory, "Fixtures");
        var window = new Window { Content = new Canvas() };
        using var host = new AvaloniaBrowserHost(window)
        {
            ScriptBaseDirectory = fixtureDirectory
        };

        var inline = host.Services.Resources.LoadText(
            new WebSceneResourceRequest(
                "data:text/plain,hello%20portable%20runtime",
                null,
                WebSceneResourceKind.Data));
        var script = host.ResolveExternalScript("module-mutation-observer.js");

        Assert.Equal("hello portable runtime", inline.Content);
        Assert.EndsWith("module-mutation-observer.js", script.FileName, StringComparison.Ordinal);
        Assert.Contains("MutationObserver", script.Content, StringComparison.Ordinal);
    }

    [AvaloniaFact]
    public void PortableResourceLoaderCoversRelativeBasePackagedAndFailureContracts()
    {
        var fixtureDirectory = Path.Combine(AppContext.BaseDirectory, "Fixtures");
        var window = new Window { Content = new Canvas() };
        using var host = new AvaloniaBrowserHost(window)
        {
            ScriptBaseDirectory = AppContext.BaseDirectory
        };

        var base64 = host.Services.Resources.LoadText(
            new WebSceneResourceRequest(
                "data:text/plain;base64,cG9ydGFibGU=",
                null,
                WebSceneResourceKind.Data));
        var extensionFallback = host.Services.Resources.LoadText(
            new WebSceneResourceRequest(
                Path.Combine(fixtureDirectory, "module-mutation-observer"),
                null,
                WebSceneResourceKind.Script));
        var rootedBase = host.Services.Resources.LoadText(
            new WebSceneResourceRequest(
                "module-mutation-observer.js",
                Path.Combine(fixtureDirectory, "base.js"),
                WebSceneResourceKind.Script));
        var relativeBase = host.Services.Resources.LoadText(
            new WebSceneResourceRequest(
                "module-mutation-observer.js",
                Path.Combine("Fixtures", "base.js"),
                WebSceneResourceKind.Script));
        var absoluteBase = host.Services.Resources.LoadText(
            new WebSceneResourceRequest(
                "module-mutation-observer.js",
                new Uri(Path.Combine(fixtureDirectory, "base.js")).AbsoluteUri,
                WebSceneResourceKind.Script));
        var packagedHttp = host.Services.Resources.LoadText(
            new WebSceneResourceRequest(
                "https://webscene.invalid/assets/module-mutation-observer.js",
                null,
                WebSceneResourceKind.Script));

        Assert.Equal("portable", base64.Content);
        Assert.Equal(extensionFallback.Content, rootedBase.Content);
        Assert.Equal(rootedBase.Content, relativeBase.Content);
        Assert.Equal(relativeBase.Content, absoluteBase.Content);
        Assert.Equal(absoluteBase.Content, packagedHttp.Content);
        Assert.Throws<ArgumentException>(
            () => host.Services.Resources.LoadText(
                new WebSceneResourceRequest(" ", null, WebSceneResourceKind.Data)));
        Assert.Throws<FormatException>(
            () => host.Services.Resources.LoadText(
                new WebSceneResourceRequest("data:text/plain", null, WebSceneResourceKind.Data)));
        Assert.Throws<FileNotFoundException>(
            () => host.Services.Resources.LoadText(
                new WebSceneResourceRequest("missing-resource.js", null, WebSceneResourceKind.Script)));
        Assert.Throws<NotSupportedException>(
            () => host.Services.Resources.LoadText(
                new WebSceneResourceRequest("ftp://webscene.invalid/file.js", null, WebSceneResourceKind.Script)));
    }

    [AvaloniaFact]
    public void PortableDispatcherPreservesPriorityPostingAndCancellation()
    {
        var window = new Window { Content = new Canvas() };
        using var host = new AvaloniaBrowserHost(window);
        var calls = new List<string>();

        host.Services.Dispatcher.Post(() => calls.Add("send"), WebSceneDispatchPriority.Send);
        using var canceled = host.Services.Dispatcher.Schedule(
            TimeSpan.FromMilliseconds(1),
            () => calls.Add("canceled"),
            WebSceneDispatchPriority.Background);
        canceled.Cancel();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal(new[] { "send" }, calls);
        Assert.True(canceled.IsCancellationRequested);
    }

    [AvaloniaFact]
    public void PortableDispatcherMapsAllPrioritiesAndRunsScheduledWorkOnce()
    {
        var window = new Window { Content = new Canvas() };
        using var host = new AvaloniaBrowserHost(window);
        var calls = new List<string>();

        host.Services.Dispatcher.VerifyAccess();
        host.Services.Dispatcher.Post(() => calls.Add("input"), WebSceneDispatchPriority.Input);
        host.Services.Dispatcher.Post(() => calls.Add("render"), WebSceneDispatchPriority.Render);
        host.Services.Dispatcher.Post(() => calls.Add("default"));
        using var scheduled = host.Services.Dispatcher.Schedule(
            TimeSpan.FromMilliseconds(-1),
            () => calls.Add("scheduled"));
        Dispatcher.UIThread.RunJobs();

        Assert.Contains("input", calls);
        Assert.Contains("render", calls);
        Assert.Contains("default", calls);
        Assert.Contains("scheduled", calls);
        Assert.False(scheduled.IsCancellationRequested);
        Assert.Throws<ArgumentNullException>(() => host.Services.Dispatcher.Post(null!));
        Assert.Throws<ArgumentNullException>(
            () => host.Services.Dispatcher.Schedule(TimeSpan.Zero, null!));
    }

    [AvaloniaFact]
    public void PortableViewportFramesClipboardAndInputKeepAdapterLifetimes()
    {
        var root = new Canvas();
        var window = new Window { Width = 320, Height = 240, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();
        var host = new AvaloniaBrowserHost(window);
        var services = host.Services;
        var viewportChanges = new List<WebSceneViewportChangedEventArgs>();
        var textInputs = new List<WebSceneTextInputEventArgs>();
        var keyboardInputs = new List<WebSceneKeyboardInputEventArgs>();
        var pointerInputs = new List<WebScenePointerInputEventArgs>();
        services.Viewport.Changed += (_, args) => viewportChanges.Add(args);
        services.Input.TextInput += (_, args) =>
        {
            textInputs.Add(args);
            args.Handled = true;
        };
        services.Input.Keyboard += (_, args) =>
        {
            keyboardInputs.Add(args);
            args.Handled = true;
        };
        services.Input.Pointer += (_, args) =>
        {
            pointerInputs.Add(args);
            args.Handled = true;
        };

        services.Clipboard.SetText("portable clipboard");
        Assert.Equal("portable clipboard", services.Clipboard.GetText());

        window.Width = 480;
        window.Height = 360;
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(480, services.Viewport.Metrics.ClientSize.Width);
        Assert.Equal(360, services.Viewport.Metrics.ClientSize.Height);
        Assert.NotEmpty(viewportChanges);

        var textArgs = new TextInputEventArgs
        {
            RoutedEvent = InputElement.TextInputEvent,
            Source = window,
            Text = "A"
        };
        window.RaiseEvent(textArgs);
        var keyArgs = new KeyEventArgs
        {
            RoutedEvent = InputElement.KeyDownEvent,
            Source = window,
            Key = Key.F,
            KeyModifiers = KeyModifiers.Control | KeyModifiers.Shift
        };
        window.RaiseEvent(keyArgs);
        using (var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true))
        {
            var pointerArgs = new PointerPressedEventArgs(
                window,
                pointer,
                window,
                new Point(12, 18),
                1,
                new PointerPointProperties(
                    RawInputModifiers.LeftMouseButton,
                    PointerUpdateKind.LeftButtonPressed),
                KeyModifiers.Alt);
            window.RaiseEvent(pointerArgs);
            Assert.True(pointerArgs.Handled);
        }

        Assert.True(textArgs.Handled);
        Assert.True(keyArgs.Handled);
        Assert.Equal("A", Assert.Single(textInputs).Text);
        Assert.Equal("keydown", Assert.Single(keyboardInputs).Type);
        Assert.True(keyboardInputs[0].ControlKey);
        Assert.True(keyboardInputs[0].ShiftKey);
        var normalizedPointer = Assert.Single(pointerInputs);
        Assert.Equal(WebScenePointerEventKind.Pressed, normalizedPointer.Kind);
        Assert.Equal(WebScenePointerType.Mouse, normalizedPointer.PointerType);
        Assert.Equal(new WebScenePoint(12, 18), normalizedPointer.Position);
        Assert.Equal(0, normalizedPointer.Button);
        Assert.Equal(1, normalizedPointer.Buttons);
        Assert.True(normalizedPointer.AltKey);

        var canceledFrame = services.Frames.RequestFrame(_ => Assert.Fail("Canceled frame executed."));
        Assert.False(canceledFrame.IsEmpty);
        Assert.True(services.Frames.CancelFrame(canceledFrame));
        Assert.False(services.Frames.CancelFrame(canceledFrame));

        host.Dispose();
        var countAfterDispose = textInputs.Count;
        window.RaiseEvent(new TextInputEventArgs
        {
            RoutedEvent = InputElement.TextInputEvent,
            Source = window,
            Text = "ignored"
        });
        Assert.Equal(countAfterDispose, textInputs.Count);
        Assert.Throws<ObjectDisposedException>(() => services.Frames.RequestFrame(_ => { }));
        window.Close();
    }
}
