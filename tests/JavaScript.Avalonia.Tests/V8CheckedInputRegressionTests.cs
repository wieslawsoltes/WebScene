using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8CheckedInputRegressionTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void ProgrammaticClickRunsCheckableActivationAndEventsInBrowserOrder()
    {
        using var fixture = CreateFixture();
        if (fixture.Runtime is null) return;

        fixture.Runtime.Execute("""
            const checkbox = document.createElement('input');
            checkbox.type = 'checkbox';
            document.body.appendChild(checkbox);
            const observed = [];
            checkbox.addEventListener('click', event => {
              observed.push(`click:${checkbox.checked}:${event.isTrusted}`);
            });
            checkbox.addEventListener('input', () => observed.push(`input:${checkbox.checked}`));
            checkbox.addEventListener('change', () => observed.push(`change:${checkbox.checked}`));
            checkbox.click();

            const canceled = document.createElement('input');
            canceled.type = 'checkbox';
            const canceledObserved = [];
            canceled.addEventListener('click', event => {
              canceledObserved.push(`click:${canceled.checked}:${event.isTrusted}`);
              event.preventDefault();
            });
            canceled.addEventListener('input', () => canceledObserved.push('input'));
            canceled.addEventListener('change', () => canceledObserved.push('change'));
            canceled.click();

            const disabled = document.createElement('input');
            disabled.type = 'checkbox';
            disabled.disabled = true;
            let disabledClicks = 0;
            disabled.addEventListener('click', () => disabledClicks++);
            disabled.click();
            globalThis.__webSceneProgrammaticClick = {
              checked: checkbox.checked,
              observed: observed,
              canceledChecked: canceled.checked,
              canceledObserved: canceledObserved,
              disabledChecked: disabled.checked,
              disabledClicks: disabledClicks
            };
            """, "v8-programmatic-checkable-click.js");

        using var result = JsonDocument.Parse(Convert.ToString(fixture.Runtime.Engine.Evaluate(
            "JSON.stringify(globalThis.__webSceneProgrammaticClick)")) ?? "{}");
        Assert.True(result.RootElement.GetProperty("checked").GetBoolean());
        Assert.Equal(
            ["click:true:false", "input:true", "change:true"],
            result.RootElement.GetProperty("observed").EnumerateArray().Select(item => item.GetString()));
        Assert.False(result.RootElement.GetProperty("canceledChecked").GetBoolean());
        Assert.Equal(
            ["click:true:false"],
            result.RootElement.GetProperty("canceledObserved").EnumerateArray().Select(item => item.GetString()));
        Assert.False(result.RootElement.GetProperty("disabledChecked").GetBoolean());
        Assert.Equal(0, result.RootElement.GetProperty("disabledClicks").GetInt32());
    }

    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void QuerySelectorAllAndIdLookupShareStableProxyIdentity()
    {
        using var fixture = CreateFixture();
        if (fixture.Runtime is null) return;

        fixture.Runtime.Execute("""
            const first = document.createElement('input');
            first.id = 'first';
            first.type = 'checkbox';
            first.checked = true;
            const second = document.createElement('input');
            second.id = 'second';
            second.type = 'radio';
            second.checked = true;
            document.body.appendChild(first);
            document.body.appendChild(second);
            const queried = [...document.querySelectorAll(':checked')];
            const lookedUp = ['first', 'second'].map(id => document.getElementById(id));
            globalThis.__webSceneCheckedIdentity = {
              lengths: [queried.length, lookedUp.length],
              same: queried.map((node, index) => node === lookedUp[index]),
              created: first === lookedUp[0] && second === lookedUp[1]
            };
            """, "v8-checked-query-identity.js");

        using var result = JsonDocument.Parse(Convert.ToString(fixture.Runtime.Engine.Evaluate(
            "JSON.stringify(globalThis.__webSceneCheckedIdentity)")) ?? "{}");
        Assert.Equal([2, 2], result.RootElement.GetProperty("lengths").EnumerateArray().Select(item => item.GetInt32()));
        Assert.All(result.RootElement.GetProperty("same").EnumerateArray(), item => Assert.True(item.GetBoolean()));
        Assert.True(result.RootElement.GetProperty("created").GetBoolean());
    }

    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void CheckedPseudoClassTracksInputTypeAndOptionSelectedState()
    {
        using var fixture = CreateFixture();
        if (fixture.Runtime is null) return;

        fixture.Runtime.Execute("""
            const option = document.createElement('option');
            option.id = 'option';
            option.setAttribute('selected', '');
            const misleadingOption = document.createElement('option');
            misleadingOption.id = 'misleading-option';
            misleadingOption.setAttribute('checked', '');
            const checkbox = document.createElement('input');
            checkbox.id = 'checkbox';
            checkbox.type = 'checkbox';
            checkbox.checked = true;
            const misleadingInput = document.createElement('input');
            misleadingInput.id = 'misleading-input';
            misleadingInput.type = 'checkbox';
            misleadingInput.setAttribute('selected', '');
            document.body.appendChild(option);
            document.body.appendChild(misleadingOption);
            document.body.appendChild(checkbox);
            document.body.appendChild(misleadingInput);
            const ids = () => [...document.querySelectorAll(':checked')].map(node => node.id);
            const initial = ids();
            checkbox.removeAttribute('type');
            const typeRemoved = ids();
            option.selected = false;
            misleadingOption.selected = 'selected';
            const optionChanged = ids();
            globalThis.__webSceneCheckedState = { initial, typeRemoved, optionChanged };
            """, "v8-checked-state-semantics.js");

        using var result = JsonDocument.Parse(Convert.ToString(fixture.Runtime.Engine.Evaluate(
            "JSON.stringify(globalThis.__webSceneCheckedState)")) ?? "{}");
        Assert.Equal(new string?[] { "option", "checkbox" }, Strings(result.RootElement.GetProperty("initial")));
        Assert.Equal(new string?[] { "option" }, Strings(result.RootElement.GetProperty("typeRemoved")));
        Assert.Equal(new string?[] { "misleading-option" }, Strings(result.RootElement.GetProperty("optionChanged")));
    }

    private static string?[] Strings(JsonElement element)
        => element.EnumerateArray().Select(item => item.GetString()).ToArray();

    private static Fixture CreateFixture()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return new Fixture(null, null, null);
        }

        var window = new Window
        {
            Width = 200,
            Height = 100,
            Content = new CssLayoutPanel()
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();
        var host = new AvaloniaBrowserHost(window);
        return new Fixture(window, host, new ClearScriptV8Runtime(host));
    }

    private sealed class Fixture(
        Window? window,
        AvaloniaBrowserHost? host,
        ClearScriptV8Runtime? runtime) : IDisposable
    {
        public ClearScriptV8Runtime? Runtime { get; } = runtime;

        public void Dispose()
        {
            Runtime?.Dispose();
            host?.Dispose();
            window?.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}
