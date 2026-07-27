using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using WebScene.JavaScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class ResizeObserverBatchingTests
{
    [AvaloniaFact]
    public void SharedNativeResizeObserverDeliveryIsBatchedOncePerDocumentTurn()
    {
        var root = new StackPanel { Width = 320, Height = 180 };
        var (host, window) = HostTestUtilities.CreateHost(root);
        window.Width = 320;
        window.Height = 180;
        window.Show();
        Dispatcher.UIThread.RunJobs();

        try
        {
            var first = HostTestUtilities.GetElement(host.Document.createElement("div"));
            var second = HostTestUtilities.GetElement(host.Document.createElement("div"));
            HostTestUtilities.GetElement(host.Document.body).appendChild(first);
            HostTestUtilities.GetElement(host.Document.body).appendChild(second);
            first.Control.Width = 100;
            first.Control.Height = 40;
            second.Control.Width = 120;
            second.Control.Height = 40;
            Dispatcher.UIThread.RunJobs();

            var rectTarget = Assert.IsAssignableFrom<IWebSceneDomClientRectsTarget>(first);
            Assert.True(rectTarget.TryReadClientRect(out var fastRect));
            var publicRect = Assert.Single(first.getClientRects());
            Assert.Equal(publicRect.width, fastRect.Width);
            Assert.Equal(publicRect.height, fastRect.Height);

            var callback = new CountingCallback();
            first.__webSceneObserveResize(callback);
            second.__webSceneObserveResize(callback);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(1, callback.Count);

            callback.Reset();
            first.Control.Width = 101;
            second.Control.Width = 121;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(1, callback.Count);

            callback.Reset();
            first.Control.Width = 101;
            second.Control.Width = 121;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(0, callback.Count);

            first.__webSceneUnobserveResize(callback);
            second.__webSceneUnobserveResize(callback);
        }
        finally
        {
            host.Dispose();
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private sealed class CountingCallback : IExternalJavaScriptCallback
    {
        public int Count { get; private set; }

        public void Invoke(object? thisValue, params object?[] arguments) => Count++;

        public void Reset() => Count = 0;
    }
}
