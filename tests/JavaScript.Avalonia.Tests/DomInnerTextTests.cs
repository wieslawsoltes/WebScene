using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class DomInnerTextTests
{
    [AvaloniaFact]
    public void RenderedBlockBoundariesDifferFromRawTextContent()
    {
        var root = new CssLayoutPanel { Width = 320, Height = 120 };
        var window = new Window { Width = 320, Height = 120, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var body = HostTestUtilities.GetElement(document.body);
        var fixture = HostTestUtilities.GetElement(document.createElement("div"));
        fixture.innerHTML = """
            <div id="block-label"><div>Bars</div><div>Candles</div></div>
            <div id="inline-label"><span>Alpha</span><span>Beta</span></div>
            """;
        body.appendChild(fixture);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        var block = Assert.IsType<AvaloniaDomElement>(document.getElementById("block-label"));
        var inline = Assert.IsType<AvaloniaDomElement>(document.getElementById("inline-label"));
        Assert.Equal("BarsCandles", block.textContent);
        Assert.Equal("Bars\nCandles", block.innerText);
        Assert.Equal("AlphaBeta", inline.innerText);

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }
}
