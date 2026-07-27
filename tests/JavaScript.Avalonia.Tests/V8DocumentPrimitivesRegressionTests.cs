using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8DocumentPrimitivesRegressionTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void LiveAndDetachedDocumentsExposeBrowserShapedStructureCollectionsAndCookies()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var window = new Window
        {
            Width = 320,
            Height = 180,
            Content = new CssLayoutPanel()
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        try
        {
            runtime.Execute(
                """
                const detached = document.implementation.createHTMLDocument("Release notes");
                detached.body.innerHTML = "<form id='first'></form><form id='second'></form>";

                const link = document.createElement("a");
                link.id = "release-link";
                link.href = "/release";
                const form = document.createElement("form");
                form.id = "release-form";
                const image = document.createElement("img");
                image.id = "release-image";
                document.body.append(link, form, image);
                const links = document.links;
                const forms = document.forms;
                const images = document.images;

                document.cookie = "release_theme=dark; Path=/; SameSite=Lax";
                document.cookie = "release_seen=1; Path=/";
                document.cookie = "release_theme=light; Path=/";

                const initialLinkIdentity =
                  links[0] === link && links.item(0) === link
                  && links.namedItem("release-link") === link;
                link.remove();
                const removedLinkLength = links.length;
                document.body.prepend(link);

                globalThis.__webSceneDocumentPrimitiveResult = {
                  detached: {
                    tags: [
                      detached.documentElement.tagName,
                      detached.head.tagName,
                      detached.body.tagName
                    ],
                    title: detached.head.querySelector("title").textContent,
                    childCount: detached.body.childNodes.length,
                    formCount: detached.getElementsByTagName("form").length,
                    cookie: detached.cookie
                  },
                  live: {
                    tags: [
                      document.documentElement.tagName,
                      document.head.tagName,
                      document.body.tagName
                    ],
                    documentParent: document.documentElement.parentNode === document,
                    headParent: document.head.parentNode === document.documentElement,
                    bodyParent: document.body.parentNode === document.documentElement,
                    rootQueries: [
                      document.querySelectorAll("html").length,
                      document.querySelectorAll("body").length
                    ],
                    scrollingElement: document.scrollingElement === document.documentElement,
                    collectionIdentity: links === document.links,
                    linkLength: links.length,
                    linkIdentity: initialLinkIdentity
                      && removedLinkLength === 0
                      && links.item(0) === link,
                    linkNamedIdentity: typeof links.namedItem === "function"
                      && links.namedItem("release-link") === link,
                    formIdentity: forms.length === 1
                      && forms.item(0) === form
                      && forms.namedItem("release-form") === form,
                    imageIdentity: images.length === 1
                      && images.item(0) === image
                      && images.namedItem("release-image") === image,
                    cookieDescriptor: {
                      get: typeof Object.getOwnPropertyDescriptor(Document.prototype, "cookie")?.get,
                      set: typeof Object.getOwnPropertyDescriptor(Document.prototype, "cookie")?.set
                    },
                    cookie: document.cookie
                  }
                };
                """,
                "document-primitives-regression.js");

            using var result = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__webSceneDocumentPrimitiveResult)")) ?? "{}");
            var detached = result.RootElement.GetProperty("detached");
            Assert.Equal(["HTML", "HEAD", "BODY"], detached.GetProperty("tags")
                .EnumerateArray().Select(static item => item.GetString()));
            Assert.Equal("Release notes", detached.GetProperty("title").GetString());
            Assert.True(detached.TryGetProperty("childCount", out var childCount),
                result.RootElement.GetRawText());
            Assert.Equal(2, childCount.GetInt32());
            Assert.Equal(2, detached.GetProperty("formCount").GetInt32());
            Assert.Equal(string.Empty, detached.GetProperty("cookie").GetString());

            var live = result.RootElement.GetProperty("live");
            Assert.Equal(["HTML", "HEAD", "BODY"], live.GetProperty("tags")
                .EnumerateArray().Select(static item => item.GetString()));
            Assert.True(live.GetProperty("documentParent").GetBoolean());
            Assert.True(live.GetProperty("headParent").GetBoolean());
            Assert.True(live.GetProperty("bodyParent").GetBoolean());
            Assert.Equal([1, 1], live.GetProperty("rootQueries")
                .EnumerateArray().Select(static item => item.GetInt32()));
            Assert.True(live.GetProperty("scrollingElement").GetBoolean());
            Assert.True(live.GetProperty("collectionIdentity").GetBoolean());
            Assert.Equal(1, live.GetProperty("linkLength").GetInt32());
            Assert.True(live.GetProperty("linkIdentity").GetBoolean());
            Assert.True(live.GetProperty("linkNamedIdentity").GetBoolean());
            Assert.True(live.GetProperty("formIdentity").GetBoolean());
            Assert.True(live.GetProperty("imageIdentity").GetBoolean());
            Assert.Equal("function", live.GetProperty("cookieDescriptor").GetProperty("get").GetString());
            Assert.Equal("function", live.GetProperty("cookieDescriptor").GetProperty("set").GetString());
            Assert.Contains("release_theme=light", live.GetProperty("cookie").GetString());
            Assert.Contains("release_seen=1", live.GetProperty("cookie").GetString());
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}
