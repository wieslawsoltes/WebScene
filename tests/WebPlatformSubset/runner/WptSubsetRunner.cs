using System.Diagnostics;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using JavaScript.Avalonia;
using JavaScript.Avalonia.ClearScript;

namespace WebScene.WebPlatformSubset.Runner;

internal sealed partial class WptSubsetRunner
{
    private const string ArtifactSchema = "webscene-wpt-subset-result-v2";
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        PropertyNameCaseInsensitive = true,
        WriteIndented = true
    };

    private readonly RunnerOptions _options;
    private readonly string _subsetRoot;
    private readonly string _standardsSubsetRoot;
    private readonly string _upstreamRoot;
    private readonly ProfileManifest _manifest;
    private readonly string _testHarness;
    private readonly string _checkLayoutHarness;
    private readonly HashSet<string> _pinnedUpstreamFiles;

    internal WptSubsetRunner(RunnerOptions options)
    {
        _options = options;
        _subsetRoot = Path.GetDirectoryName(options.ManifestPath)
                      ?? throw new ArgumentException("The profile manifest has no parent directory.");
        // Consumer-composition profiles keep their contracts and package locks
        // outside the standards subset, but share the exact pinned WPT harness.
        // This preserves engine isolation without presenting a framework pass as
        // an upstream standards result.
        _standardsSubsetRoot = Path.Combine(
            options.RepositoryRoot,
            "tests",
            "WebPlatformSubset");
        _upstreamRoot = Path.Combine(_standardsSubsetRoot, "upstream");
        _pinnedUpstreamFiles = ReadPinnedUpstreamFiles();
        _manifest = JsonSerializer.Deserialize<ProfileManifest>(
                        File.ReadAllText(options.ManifestPath),
                        JsonOptions)
                    ?? throw new InvalidDataException("The profile manifest is empty.");
        _testHarness = File.ReadAllText(Path.Combine(_upstreamRoot, "resources", "testharness.js"));
        _checkLayoutHarness = File.ReadAllText(Path.Combine(_upstreamRoot, "resources", "check-layout-th.js"));
    }

    internal int Run()
    {
        ValidateManifest();
        var tests = SelectTests();
        if (_options.ListOnly)
        {
            foreach (var test in tests)
            {
                Console.WriteLine($"{test.Type,-11} {test.Path}");
            }

            return 0;
        }

        RunnerApp.EnsureInitialized();
        Directory.CreateDirectory(_options.OutputDirectory);
        var startedAt = DateTimeOffset.UtcNow;
        var timer = Stopwatch.StartNew();
        var results = new List<TestResult>(tests.Count);
        var requiredPaths = _manifest.Required.Select(test => test.Path).ToHashSet(StringComparer.Ordinal);

        foreach (var test in tests)
        {
            Console.Write($"RUN  {test.Path} ... ");
            var result = string.Equals(test.Type, "reftest", StringComparison.OrdinalIgnoreCase)
                ? RunReftest(test)
                : RunTestHarness(test);
            results.Add(result);
            Console.WriteLine($"{result.Status} ({result.Duration.TotalMilliseconds:F0} ms)");
            foreach (var failedSubtest in result.Subtests.Where(item => item.Status != "PASS"))
            {
                Console.WriteLine($"     {failedSubtest.Status}: {failedSubtest.Name}: {failedSubtest.Message}");
            }
        }

        timer.Stop();
        var artifact = new RunArtifact
        {
            Schema = ArtifactSchema,
            Profile = _manifest.Profile,
            WptRevision = _manifest.WptRevision,
            Runtime = _manifest.Runtime,
            Engine = _options.Engine,
            NativeEngineIdentity = ResolveNativeEngineIdentity(),
            StartedAt = startedAt,
            Duration = timer.Elapsed,
            Selection = _options.Selection,
            Summary = Summarize(results),
            Results = results
        };
        var resultPath = Path.Combine(_options.OutputDirectory, "results.json");
        File.WriteAllText(resultPath, JsonSerializer.Serialize(artifact, JsonOptions));
        Console.WriteLine(
            $"WPT subset: {artifact.Summary.Passed}/{artifact.Summary.Tests} documents passed; " +
            $"{artifact.Summary.SubtestsPassed}/{artifact.Summary.Subtests} subtests passed.");
        Console.WriteLine($"Results: {resultPath}");

        var requiredFailure = results.Any(result =>
            requiredPaths.Contains(result.Path) && result.Status != "PASS");
        return requiredFailure ? 1 : 0;
    }

    private string? ResolveNativeEngineIdentity()
    {
        if (_options.Engine != "native") return null;
        var libraryPath = _options.NativeLibraryPath;
        if (string.IsNullOrWhiteSpace(libraryPath) || !File.Exists(libraryPath))
        {
            throw new InvalidOperationException(
                "Native WPT evidence requires an existing --native-library path.");
        }
        NativeApi.Configure(libraryPath);
        var hash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(libraryPath)))
            .ToLowerInvariant();
        return $"abi={NativeApi.GetAbiVersion()};sha256={hash}";
    }

    private TestResult RunTestHarness(ProfileTest test)
    {
        var timer = Stopwatch.StartNew();
        try
        {
            var sourcePath = TestDocumentPath(test.Path);
            var source = File.ReadAllText(sourcePath);
            var html = string.Equals(test.Type, "contract", StringComparison.OrdinalIgnoreCase)
                ? source
                : PrepareTestHarnessDocument(source, test.Path);
            var state = RunHarnessDocument(html, test.Path);
            timer.Stop();

            if (!state.Complete)
            {
                var timeoutDetails = state.Errors.Count > 0
                    ? string.Join(Environment.NewLine, state.Errors)
                    : "Document did not complete.";
                if (state.Diagnostics.Count > 0)
                {
                    timeoutDetails = string.Join(
                        Environment.NewLine,
                        new[] { timeoutDetails }.Concat(
                            state.Diagnostics.Select(value => "diagnostic: " + value)));
                }
                return Failure(test, "TIMEOUT", timer.Elapsed, timeoutDetails, state.Results);
            }

            var subtests = state.Results;
            var harnessStatus = state.Harness?.Status ?? 1;
            var status = harnessStatus switch
            {
                0 when subtests.All(item => item.Status == "PASS") => "PASS",
                2 => "TIMEOUT",
                _ => "FAIL"
            };
            var message = state.Harness?.Message;
            if (string.IsNullOrWhiteSpace(message) && state.Errors.Count > 0)
            {
                message = string.Join(Environment.NewLine, state.Errors);
            }
            if (status != "PASS" && state.Diagnostics.Count > 0)
            {
                message = string.Join(Environment.NewLine,
                    new[] { message }.Where(value => !string.IsNullOrWhiteSpace(value))!
                        .Concat(state.Diagnostics.Select(value => "diagnostic: " + value)));
            }

            return new TestResult
            {
                Path = test.Path,
                Type = test.Type,
                Status = status,
                Duration = timer.Elapsed,
                Message = message,
                Subtests = subtests
            };
        }
        catch (TimeoutException exception)
        {
            timer.Stop();
            return Failure(test, "TIMEOUT", timer.Elapsed, exception.Message);
        }
        catch (Exception exception)
        {
            timer.Stop();
            return Failure(test, "HARNESS-ERROR", timer.Elapsed, exception.ToString());
        }
    }

    private TestResult RunReftest(ProfileTest test)
    {
        var timer = Stopwatch.StartNew();
        try
        {
            if (string.IsNullOrWhiteSpace(test.Reference))
            {
                throw new InvalidDataException($"Reftest '{test.Path}' has no reference path.");
            }

            var reference = RenderDocument(
                File.ReadAllText(TestDocumentPath(test.Reference)),
                test.Reference);
            // Render the inert reference first. Some tests leave delayed layout
            // work behind while their host is disposing; allowing that test
            // work to precede the reference can starve the newly opened window
            // and produce a blank comparison frame.
            var actual = RenderDocument(
                File.ReadAllText(TestDocumentPath(test.Path)),
                test.Path);
            timer.Stop();
            var equal = actual.PixelSize == reference.PixelSize && actual.Pixels.SequenceEqual(reference.Pixels);
            if (equal)
            {
                return new TestResult
                {
                    Path = test.Path,
                    Type = test.Type,
                    Status = "PASS",
                    Duration = timer.Elapsed
                };
            }

            var artifactDirectory = Path.Combine(
                _options.OutputDirectory,
                SanitizeArtifactName(test.Path));
            Directory.CreateDirectory(artifactDirectory);
            var actualPath = Path.Combine(artifactDirectory, "actual.png");
            var referencePath = Path.Combine(artifactDirectory, "reference.png");
            var diffPath = Path.Combine(artifactDirectory, "diff.png");
            SavePixels(actual, actualPath);
            SavePixels(reference, referencePath);
            SaveDiff(actual, reference, diffPath);
            return new TestResult
            {
                Path = test.Path,
                Type = test.Type,
                Status = "FAIL",
                Duration = timer.Elapsed,
                Message = "Rendered pixels differ from the pinned WPT reference.",
                Artifacts = new Dictionary<string, string>
                {
                    ["actual"] = actualPath,
                    ["reference"] = referencePath,
                    ["diff"] = diffPath
                }
            };
        }
        catch (TimeoutException exception)
        {
            timer.Stop();
            return Failure(test, "TIMEOUT", timer.Elapsed, exception.Message);
        }
        catch (Exception exception)
        {
            timer.Stop();
            return Failure(test, "HARNESS-ERROR", timer.Elapsed, exception.ToString());
        }
    }

    private HarnessState RunHarnessDocument(string html, string documentPath)
    {
        using var environment = CreateEnvironment(html, documentPath);
        var timer = Stopwatch.StartNew();
        HarnessState? latest = null;
        while (timer.Elapsed < _options.Timeout)
        {
            Pump();
            environment.SettleFrame();
            environment.PumpInputAction();
            var json = environment.ReadState();
            if (!string.IsNullOrWhiteSpace(json))
            {
                latest = JsonSerializer.Deserialize<HarnessState>(json, JsonOptions);
                if (latest?.Complete == true)
                {
                    return latest;
                }
            }
        }

        return latest ?? new HarnessState();
    }

    private WptRenderSnapshot RenderDocument(string html, string documentName)
    {
        html = ResolvePinnedRelativeStylesheets(html, documentName);
        html = TestHarnessReportTagRegex().Replace(html, string.Empty);
        if (documentName.EndsWith(".xht", StringComparison.OrdinalIgnoreCase))
        {
            // The local blob loader currently parses through the HTML path.
            // Strip XML CDATA wrappers so XHTML STYLE content has the same
            // token stream it receives when WPT serves the file as XML.
            html = html.Replace("<![CDATA[", string.Empty, StringComparison.Ordinal)
                       .Replace("]]>", string.Empty, StringComparison.Ordinal);
        }
        using var environment = CreateEnvironment(html, documentName);
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < _options.Timeout)
        {
            Pump();
            if (environment.IsFrameComplete())
            {
                // readyState=complete precedes Avalonia's retained composition
                // commit. Give both the document and reference the same bounded
                // frame-settling window so a fast blank capture cannot produce
                // either a false pass or a one-sided reftest failure.
                for (var index = 0; index < 24; index++)
                {
                    environment.SettleFrame();
                    Pump(waitForRenderTimer: true);
                }

                return environment.CaptureSnapshot(documentName);
            }
        }

        throw new TimeoutException($"Reftest document '{documentName}' did not reach readyState=complete.");
    }

    private IWptEngineEnvironment CreateEnvironment(string html, string documentPath)
    {
        if (_options.Engine == "native")
        {
            return new NativeWptEngineEnvironment(
                _options,
                _manifest.Viewport,
                _upstreamRoot,
                html);
        }

        var root = new CssLayoutPanel
        {
            Width = _manifest.Viewport.Width,
            Height = _manifest.Viewport.Height,
            ClipToBounds = true
        };
        var window = new Window
        {
            Width = _manifest.Viewport.Width,
            Height = _manifest.Viewport.Height,
            Content = root
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        var host = new AvaloniaBrowserHost(window)
        {
            ScriptBaseDirectory = _upstreamRoot
        };
        var directDocument = documentPath.StartsWith(
            "contracts/", StringComparison.Ordinal) == true;
        ClearScriptV8Runtime? runtime = null;
        try
        {
            runtime = new ClearScriptV8Runtime(
                host,
                new ClearScriptV8RuntimeOptions
                {
                    // Both prepared upstream documents and direct neutral
                    // contracts may create same-origin iframes. Certification
                    // uses WebScene's reviewed shared-context V8 build and must
                    // expose the same connected about:blank lifecycle in both
                    // lanes rather than making direct contracts silently less
                    // capable.
                    EnableTrustedSameOriginContextSharing = true,
                    // Conformance runs must not inherit compilation units created
                    // by a different local native V8 build. Cache behavior has its
                    // own versioned tests; this lane measures DOM/layout behavior.
                    SharedCache = null
                });
            if (directDocument)
            {
                LoadDirectManagedDocument(runtime, html);
            }
            else
            {
                runtime.Execute($$"""
                    document.body.style.margin = '0';
                    document.body.style.padding = '0';
                    document.body.style.overflow = 'hidden';
                    const frame = document.createElement('iframe');
                    frame.id = 'webscene-wpt-frame';
                    frame.style.display = 'block';
                    frame.style.border = '0';
                    frame.style.width = '{{_manifest.Viewport.Width}}px';
                    frame.style.height = '{{_manifest.Viewport.Height}}px';
                    document.body.appendChild(frame);
                    frame.src = URL.createObjectURL(new Blob([{{JsonSerializer.Serialize(html)}}], { type: 'text/html' }));
                    window.__webSceneWptFrame = frame;
                    """, "webscene-wpt-owner.js");
            }
            return new ManagedWptEngineEnvironment(window, host, runtime, directDocument);
        }
        catch
        {
            runtime?.Dispose();
            host.Dispose();
            window.Close();
            Dispatcher.UIThread.RunJobs();
            throw;
        }
    }

    private void LoadDirectManagedDocument(ClearScriptV8Runtime runtime, string html)
    {
        var scriptRegex = InlineScriptRegex();
        var styleRegex = StyleElementRegex();
        var scripts = scriptRegex.Matches(html)
            .Where(match => !HtmlScriptSemantics.IsInertScript(match.Groups["attributes"].Value))
            .Select(match => match.Groups["source"].Value)
            .ToList();
        var htmlWithoutScripts = HtmlScriptSemantics.RemoveAllScripts(html, scriptRegex);
        var styles = styleRegex.Matches(htmlWithoutScripts)
            .Select(match => match.Groups["source"].Value)
            .ToList();
        var bodyMatch = BodyElementRegex().Match(html);
        var body = bodyMatch.Success ? bodyMatch.Groups["body"].Value : html;
        body = HtmlScriptSemantics.RemoveExecutableScriptsAndStyles(body, scriptRegex, styleRegex);

        runtime.Execute($$"""
            document.body.style.margin = '0';
            document.body.style.padding = '0';
            document.body.style.overflow = 'hidden';
            document.body.innerHTML = {{JsonSerializer.Serialize(body)}};
            """, "webscene-contract-document.js");
        for (var index = 0; index < styles.Count; index++)
        {
            runtime.Execute($$"""
                (() => {
                  const style = document.createElement('style');
                  style.textContent = {{JsonSerializer.Serialize(styles[index])}};
                  document.head.appendChild(style);
                })();
                """, $"webscene-contract-style-{index}.js");
        }
        for (var index = 0; index < scripts.Count; index++)
        {
            if (!string.IsNullOrWhiteSpace(scripts[index]))
            {
                runtime.Execute(scripts[index], $"webscene-contract-inline-{index}.js");
            }
        }
        runtime.Execute("window.dispatchEvent(new Event('load'));", "webscene-contract-load.js");
    }

    private string PrepareTestHarnessDocument(string html, string path)
    {
        html = ResolvePinnedRelativeStylesheets(html, path);
        html = TestHarnessReportTagRegex().Replace(html, string.Empty);
        html = TestDriverTagRegex().Replace(html, string.Empty);
        html = ResolvePinnedRelativeClassicScripts(html, path);
        html = CheckLayoutHarnessTagRegex().Replace(
            html,
            _ => "<script>" + _checkLayoutHarness + "</script>");
        var bodyOnLoad = BodyOnLoadAttributeRegex().Match(html);
        var bodyOnLoadRegistration = string.Empty;
        if (bodyOnLoad.Success)
        {
            var source = bodyOnLoad.Groups["double"].Success
                ? bodyOnLoad.Groups["double"].Value
                : bodyOnLoad.Groups["single"].Success
                    ? bodyOnLoad.Groups["single"].Value
                    : bodyOnLoad.Groups["bare"].Value;
            source = WebUtility.HtmlDecode(source);
            html = BodyOnLoadAttributeRegex().Replace(
                html,
                match => "<body" + match.Groups["before"].Value + match.Groups["after"].Value + ">",
                1);
            bodyOnLoadRegistration =
                "<script>window.addEventListener('load', function(event){" + source + "});</script>";
        }
        if (string.Equals(path, "css/selectors/hover-002.html", StringComparison.Ordinal))
        {
            // The case is about :hover invalidation, but also uses legacy
            // window-named element access incidentally. Keep this adapter
            // scoped to the selected upstream document until that unrelated
            // WindowProxy surface is implemented.
            html = html.Replace(
                "  hovered.offsetTop;",
                "  const hovered = document.getElementById('hovered');\n" +
                "  const hoveredContents = document.getElementById('hoveredContents');\n" +
                "  const hovered2 = document.getElementById('hovered2');\n" +
                "  hovered.offsetTop;",
                StringComparison.Ordinal);
        }
        var replacement = "<script>" + HarnessPreamble + "</script>" +
                          "<script>" + _testHarness + "</script>" +
                          "<script>" + HarnessReporter + "</script>" +
                          bodyOnLoadRegistration;
        // A replacement string would interpret the many '$&' and '$` tokens in
        // upstream testharness.js as Regex replacement substitutions.
        var replaced = TestHarnessTagRegex().Replace(html, _ => replacement, 1);
        if (ReferenceEquals(replaced, html) || string.Equals(replaced, html, StringComparison.Ordinal))
        {
            throw new InvalidDataException($"Testharness document '{path}' does not load /resources/testharness.js.");
        }

        return replaced;
    }

    private string ResolvePinnedRelativeClassicScripts(string html, string documentPath)
    {
        return ScriptTagRegex().Replace(html, scriptMatch =>
        {
            var script = scriptMatch.Value;
            var srcMatch = SrcAttributeRegex().Match(script);
            if (!srcMatch.Success)
            {
                return script;
            }

            var src = WebUtility.HtmlDecode(ReadAttributeValue(srcMatch)).Trim();
            if (string.IsNullOrWhiteSpace(src)
                || src.StartsWith('#')
                || src.StartsWith('/')
                || src.StartsWith('\\')
                || Uri.TryCreate(src, UriKind.Absolute, out _))
            {
                return script;
            }

            var suffixIndex = src.IndexOfAny(['?', '#']);
            var pathPart = suffixIndex >= 0 ? src[..suffixIndex] : src;
            string decodedPath;
            try
            {
                decodedPath = Uri.UnescapeDataString(pathPart).Replace('\\', '/');
            }
            catch (UriFormatException exception)
            {
                throw new InvalidDataException(
                    $"Script URL '{src}' in '{documentPath}' is not a valid local relative path.",
                    exception);
            }

            var documentDirectory = Path.GetDirectoryName(
                                        documentPath.Replace('/', Path.DirectorySeparatorChar))
                                    ?? string.Empty;
            var relativeResourcePath = Path.Combine(
                documentDirectory,
                decodedPath.Replace('/', Path.DirectorySeparatorChar));
            var fullResourcePath = UpstreamPath(relativeResourcePath);
            var canonicalRelativePath = Path.GetRelativePath(_upstreamRoot, fullResourcePath)
                .Replace(Path.DirectorySeparatorChar, '/');
            if (!_pinnedUpstreamFiles.Contains(canonicalRelativePath))
            {
                throw new InvalidDataException(
                    $"Script '{src}' in '{documentPath}' resolves to unpinned resource " +
                    $"'{canonicalRelativePath}'. Add the exact upstream file and digest before loading it.");
            }

            var source = File.ReadAllText(fullResourcePath);
            if (source.Contains("</script", StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException(
                    $"Pinned script '{canonicalRelativePath}' cannot be safely inlined by the bounded adapter.");
            }
            return "<script>" + source + "</script>";
        });
    }

    private string ResolvePinnedRelativeStylesheets(string html, string documentPath)
    {
        return LinkTagRegex().Replace(html, linkMatch =>
        {
            var link = linkMatch.Value;
            var relMatch = RelAttributeRegex().Match(link);
            if (!relMatch.Success
                || !ReadAttributeValue(relMatch).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
                    .Contains("stylesheet", StringComparer.OrdinalIgnoreCase))
            {
                return link;
            }

            var hrefMatch = HrefAttributeRegex().Match(link);
            if (!hrefMatch.Success)
            {
                return link;
            }

            var href = WebUtility.HtmlDecode(ReadAttributeValue(hrefMatch)).Trim();
            if (string.IsNullOrWhiteSpace(href)
                || href.StartsWith('#')
                || href.StartsWith('/')
                || href.StartsWith('\\')
                || Uri.TryCreate(href, UriKind.Absolute, out _))
            {
                return link;
            }

            var suffixIndex = href.IndexOfAny(['?', '#']);
            var pathPart = suffixIndex >= 0 ? href[..suffixIndex] : href;
            if (string.IsNullOrWhiteSpace(pathPart))
            {
                return link;
            }

            string decodedPath;
            try
            {
                decodedPath = Uri.UnescapeDataString(pathPart).Replace('\\', '/');
            }
            catch (UriFormatException exception)
            {
                throw new InvalidDataException(
                    $"Stylesheet URL '{href}' in '{documentPath}' is not a valid local relative path.",
                    exception);
            }

            var documentDirectory = Path.GetDirectoryName(
                                        documentPath.Replace('/', Path.DirectorySeparatorChar))
                                    ?? string.Empty;
            var relativeResourcePath = Path.Combine(
                documentDirectory,
                decodedPath.Replace('/', Path.DirectorySeparatorChar));
            var fullResourcePath = UpstreamPath(relativeResourcePath);
            var canonicalRelativePath = Path.GetRelativePath(_upstreamRoot, fullResourcePath)
                .Replace(Path.DirectorySeparatorChar, '/');
            if (!_pinnedUpstreamFiles.Contains(canonicalRelativePath))
            {
                throw new InvalidDataException(
                    $"Stylesheet '{href}' in '{documentPath}' resolves to unpinned resource " +
                    $"'{canonicalRelativePath}'. Add the exact upstream file and digest before loading it.");
            }

            var source = File.ReadAllText(fullResourcePath);
            if (source.Contains("</style", StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException(
                    $"Pinned stylesheet '{canonicalRelativePath}' cannot be safely inlined by the bounded adapter.");
            }
            // Both engine adapters must consume identical prepared CSS. The
            // native WPT environment has no general file-URL loader, so merely
            // rewriting href to file:// silently left the required stylesheet
            // unapplied and produced false layout failures. Inline only the
            // exact pinned resource after its provenance check above.
            return $"<style data-webscene-source={JsonSerializer.Serialize(canonicalRelativePath)}>" +
                   source +
                   "</style>";
        });
    }

    private static string ReadAttributeValue(Match match)
    {
        foreach (var name in new[] { "double", "single", "bare" })
        {
            if (match.Groups[name].Success)
            {
                return match.Groups[name].Value;
            }
        }

        return string.Empty;
    }

    private List<ProfileTest> SelectTests()
    {
        IEnumerable<ProfileTest> selected = _options.Selection switch
        {
            "required" => _manifest.Required,
            "candidate" => _manifest.Candidate,
            "all" => _manifest.Required.Concat(_manifest.Candidate),
            _ => throw new InvalidOperationException($"Unknown selection '{_options.Selection}'.")
        };
        if (!string.IsNullOrWhiteSpace(_options.TestFilter))
        {
            selected = selected.Where(test =>
                test.Path.Contains(_options.TestFilter, StringComparison.OrdinalIgnoreCase));
        }

        return selected.ToList();
    }

    private void ValidateManifest()
    {
        ValidateUpstreamIntegrity();
        if (!string.Equals(_manifest.Runtime, "v8", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("The WebScene component conformance profile must use the V8 runtime.");
        }
        if (_manifest.Viewport.DeviceScaleFactor != 1)
        {
            throw new InvalidDataException("The current headless adapter supports only deviceScaleFactor=1.");
        }

        var allTests = _manifest.Required
            .Concat(_manifest.Candidate)
            .Concat(_manifest.HarnessBlocked)
            .ToList();
        var duplicate = allTests.GroupBy(test => test.Path, StringComparer.Ordinal)
            .FirstOrDefault(group => group.Count() > 1);
        if (duplicate is not null)
        {
            throw new InvalidDataException($"Manifest path '{duplicate.Key}' occurs in more than one state.");
        }

        foreach (var test in _manifest.Required.Concat(_manifest.Candidate))
        {
            if (!File.Exists(TestDocumentPath(test.Path)))
            {
                throw new FileNotFoundException($"Conformance document '{test.Path}' is missing.");
            }
            if (test.Type is not ("testharness" or "reftest" or "contract"))
            {
                throw new InvalidDataException($"Unknown test type '{test.Type}' for '{test.Path}'.");
            }
            if (test.Type == "reftest" &&
                (string.IsNullOrWhiteSpace(test.Reference) || !File.Exists(TestDocumentPath(test.Reference))))
            {
                throw new FileNotFoundException($"Reference for '{test.Path}' is missing.");
            }
        }
    }

    private void ValidateUpstreamIntegrity()
    {
        var provenancePath = Path.Combine(_standardsSubsetRoot, "upstream-files.json");
        using var provenance = JsonDocument.Parse(File.ReadAllText(provenancePath));
        var root = provenance.RootElement;
        var revision = root.GetProperty("revision").GetString();
        if (!string.Equals(revision, _manifest.WptRevision, StringComparison.Ordinal))
        {
            throw new InvalidDataException(
                $"Profile revision '{_manifest.WptRevision}' does not match upstream provenance '{revision}'.");
        }

        var recordedPaths = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in root.GetProperty("files").EnumerateObject())
        {
            recordedPaths.Add(property.Name);
            var path = UpstreamPath(property.Name);
            if (!File.Exists(path))
            {
                throw new FileNotFoundException($"Pinned upstream file '{property.Name}' is missing.");
            }

            var actual = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
            if (!string.Equals(actual, property.Value.GetString(), StringComparison.Ordinal))
            {
                throw new InvalidDataException(
                    $"Pinned upstream file '{property.Name}' differs from its recorded SHA-256 digest.");
            }
        }

        var unrecorded = Directory.EnumerateFiles(_upstreamRoot, "*", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(_upstreamRoot, path).Replace(Path.DirectorySeparatorChar, '/'))
            .FirstOrDefault(path => !recordedPaths.Contains(path));
        if (unrecorded is not null)
        {
            throw new InvalidDataException($"Vendored upstream file '{unrecorded}' has no provenance digest.");
        }
    }

    private HashSet<string> ReadPinnedUpstreamFiles()
    {
        var provenancePath = Path.Combine(_standardsSubsetRoot, "upstream-files.json");
        using var provenance = JsonDocument.Parse(File.ReadAllText(provenancePath));
        return provenance.RootElement.GetProperty("files")
            .EnumerateObject()
            .Select(property => property.Name)
            .ToHashSet(StringComparer.Ordinal);
    }

    private string UpstreamPath(string relativePath)
    {
        var normalized = relativePath.Replace('/', Path.DirectorySeparatorChar);
        var fullPath = Path.GetFullPath(Path.Combine(_upstreamRoot, normalized));
        var rootPrefix = Path.GetFullPath(_upstreamRoot) + Path.DirectorySeparatorChar;
        if (!fullPath.StartsWith(rootPrefix, StringComparison.Ordinal))
        {
            throw new InvalidDataException($"Upstream path escapes the pinned root: '{relativePath}'.");
        }

        return fullPath;
    }

    private string TestDocumentPath(string relativePath)
    {
        const string contractPrefix = "contracts/";
        if (!relativePath.StartsWith(contractPrefix, StringComparison.Ordinal))
        {
            return UpstreamPath(relativePath);
        }

        var contractsRoot = Path.Combine(_subsetRoot, "contracts");
        var normalized = relativePath[contractPrefix.Length..]
            .Replace('/', Path.DirectorySeparatorChar);
        var fullPath = Path.GetFullPath(Path.Combine(contractsRoot, normalized));
        var rootPrefix = Path.GetFullPath(contractsRoot) + Path.DirectorySeparatorChar;
        if (!fullPath.StartsWith(rootPrefix, StringComparison.Ordinal))
        {
            throw new InvalidDataException(
                $"Conformance contract path escapes the contracts root: '{relativePath}'.");
        }

        return fullPath;
    }

    private static void Pump(bool waitForRenderTimer = false)
    {
        // The headless render timer targets 60 Hz. A timed sleep makes the
        // runner itself a second, coarser clock: even a requested 1 ms sleep
        // can be descheduled long enough to turn a due 16.67 ms render tick
        // into an apparent >25 ms frame. Yield to the timer thread, then drain
        // the UI dispatcher without synthesizing frames or timestamps.
        if (waitForRenderTimer)
        {
            // Reftest capture needs at least one retained-composition commit;
            // unlike a temporal harness, merely polling as fast as possible
            // can finish its bounded settle loop before the next render tick.
            Thread.Sleep(1);
        }
        else
        {
            Thread.Yield();
        }
        Dispatcher.UIThread.RunJobs();
    }

    private static TestResult Failure(
        ProfileTest test,
        string status,
        TimeSpan duration,
        string? message,
        List<SubtestResult>? subtests = null)
        => new()
        {
            Path = test.Path,
            Type = test.Type,
            Status = status,
            Duration = duration,
            Message = message,
            Subtests = subtests ?? []
        };

    private static RunSummary Summarize(List<TestResult> results)
    {
        var subtests = results.SelectMany(result => result.Subtests).ToList();
        return new RunSummary
        {
            Tests = results.Count,
            Passed = results.Count(result => result.Status == "PASS"),
            Failed = results.Count(result => result.Status == "FAIL"),
            TimedOut = results.Count(result => result.Status == "TIMEOUT"),
            HarnessErrors = results.Count(result => result.Status == "HARNESS-ERROR"),
            Subtests = subtests.Count,
            SubtestsPassed = subtests.Count(result => result.Status == "PASS"),
            SubtestsFailed = subtests.Count(result => result.Status != "PASS")
        };
    }

    internal static WptRenderSnapshot CopyPixels(Bitmap bitmap)
    {
        var stride = bitmap.PixelSize.Width * 4;
        var pixels = new byte[stride * bitmap.PixelSize.Height];
        var handle = GCHandle.Alloc(pixels, GCHandleType.Pinned);
        try
        {
            bitmap.CopyPixels(
                new PixelRect(bitmap.PixelSize),
                handle.AddrOfPinnedObject(),
                pixels.Length,
                stride);
        }
        finally
        {
            handle.Free();
        }

        var format = bitmap.Format ?? PixelFormat.Bgra8888;
        if (format == PixelFormat.Rgba8888)
        {
            for (var offset = 0; offset < pixels.Length; offset += 4)
            {
                (pixels[offset], pixels[offset + 2]) = (pixels[offset + 2], pixels[offset]);
            }
        }
        else if (format == PixelFormat.Rgb32)
        {
            // Rgb32's fourth byte is padding, not transparency. Treating it as
            // alpha made otherwise identical reference frames transparent.
            for (var offset = 3; offset < pixels.Length; offset += 4)
            {
                pixels[offset] = byte.MaxValue;
            }
        }
        else if (format != PixelFormat.Bgra8888)
        {
            throw new NotSupportedException($"Unsupported WPT capture format: {format}.");
        }

        return new WptRenderSnapshot(
            bitmap.PixelSize,
            bitmap.Dpi,
            PixelFormat.Bgra8888,
            pixels);
    }

    private static void SavePixels(WptRenderSnapshot snapshot, string path)
    {
        using var bitmap = new WriteableBitmap(
            snapshot.PixelSize,
            snapshot.Dpi,
            snapshot.Format,
            AlphaFormat.Unpremul);
        CopyIntoBitmap(snapshot.Pixels, bitmap);
        bitmap.Save(path);
    }

    private static void SaveDiff(WptRenderSnapshot actual, WptRenderSnapshot reference, string path)
    {
        var size = actual.PixelSize;
        var length = size.Width * size.Height * 4;
        var diff = new byte[length];
        var comparedLength = Math.Min(actual.Pixels.Length, reference.Pixels.Length);
        for (var offset = 0; offset < length; offset += 4)
        {
            var differs = offset + 3 >= comparedLength ||
                          actual.Pixels[offset] != reference.Pixels[offset] ||
                          actual.Pixels[offset + 1] != reference.Pixels[offset + 1] ||
                          actual.Pixels[offset + 2] != reference.Pixels[offset + 2] ||
                          actual.Pixels[offset + 3] != reference.Pixels[offset + 3];
            if (differs)
            {
                diff[offset] = 255;
                diff[offset + 1] = 0;
                diff[offset + 2] = 255;
                diff[offset + 3] = 255;
            }
        }

        using var bitmap = new WriteableBitmap(size, actual.Dpi, PixelFormat.Bgra8888, AlphaFormat.Unpremul);
        CopyIntoBitmap(diff, bitmap);
        bitmap.Save(path);
    }

    private static void CopyIntoBitmap(byte[] pixels, WriteableBitmap bitmap)
    {
        using var framebuffer = bitmap.Lock();
        var sourceStride = bitmap.PixelSize.Width * 4;
        for (var row = 0; row < bitmap.PixelSize.Height; row++)
        {
            Marshal.Copy(
                pixels,
                row * sourceStride,
                framebuffer.Address + row * framebuffer.RowBytes,
                sourceStride);
        }
    }

    private static string SanitizeArtifactName(string path)
        => string.Concat(path.Select(character =>
            char.IsLetterOrDigit(character) || character is '-' or '_' ? character : '-'));

    private const string HarnessPreamble = """
        (function () {
          const state = window.__webSceneWptState = {
            complete: false,
            harness: null,
            results: [],
            errors: []
            , diagnostics: []
          };
          window.addEventListener('error', function (event) {
            state.errors.push(String(event && (event.message || event.error) || 'window error'));
          });
          window.addEventListener('unhandledrejection', function (event) {
            state.errors.push(String(event && event.reason || 'unhandled rejection'));
          });
          let nextInputAction = 1;
          const inputResolvers = new Map();
          window.__webSceneWptInputActions = [];
          window.__webSceneCompleteInputAction = function (id, error) {
            const pending = inputResolvers.get(Number(id));
            if (!pending) return;
            inputResolvers.delete(Number(id));
            if (error == null) pending.resolve();
            else pending.reject(new Error(String(error)));
          };
          function enqueueInputAction(type, target, value) {
            return new Promise(function (resolve, reject) {
              if (!target || !target.id) {
                reject(new Error('WebScene WPT input adapter requires an element target with an id'));
                return;
              }
              const id = nextInputAction++;
              inputResolvers.set(id, { resolve: resolve, reject: reject });
              window.__webSceneWptInputActions.push({
                id: id,
                type: String(type),
                targetId: String(target.id),
                value: value == null ? null : String(value)
              });
            });
          }
          function Actions() { this.target = null; }
          Actions.prototype.pointerMove = function (_x, _y, options) {
            this.target = options && options.origin;
            return this;
          };
          Actions.prototype.send = function () {
            return enqueueInputAction('pointerMove', this.target, null);
          };
          window.test_driver = {
            Actions: Actions,
            click: function (target) {
              return enqueueInputAction('click', target, null);
            },
            context_click: function (target) {
              return enqueueInputAction('contextClick', target, null);
            },
            wheel: function (target, deltaY) {
              return enqueueInputAction('wheel', target, deltaY);
            },
            set_viewport: function (target, width, height) {
              return enqueueInputAction('resize', target, JSON.stringify([width, height]));
            },
            send_keys: function (target, keys) {
              return enqueueInputAction('sendKeys', target, keys);
            }
          };
        })();
        """;

    private const string HarnessReporter = """
        (function () {
          const state = window.__webSceneWptState;
          function statusName(status) {
            return ['PASS', 'FAIL', 'TIMEOUT', 'NOTRUN', 'PRECONDITION-FAILED'][status] || ('STATUS-' + status);
          }
          if (typeof add_result_callback !== 'function' || typeof add_completion_callback !== 'function') {
            state.errors.push('testharness.js did not expose its result callbacks');
            state.complete = true;
            state.harness = { status: 1, message: state.errors[state.errors.length - 1], stack: '' };
            return;
          }
          setup({ output: false });
          add_result_callback(function (test) {
            state.results.push({
              name: String(test.name || ''),
              status: statusName(test.status),
              message: test.message == null ? null : String(test.message),
              stack: test.stack == null ? null : String(test.stack)
            });
          });
          add_completion_callback(function (_tests, harnessStatus) {
            try {
              const styled = Array.from(document.querySelectorAll('[style]')).slice(0, 8);
              state.diagnostics = styled.map(function (element) {
                return String(element.tagName || '') + '#' + String(element.id || '') +
                  ' style=' + String(element.getAttribute('style') || '');
              });
              if (window.events && typeof window.events === 'object') {
                state.diagnostics.push('focus-events=' + JSON.stringify(window.events));
              }
              state.diagnostics.push(
                'activeElement=' + String(document.activeElement && document.activeElement.id || ''));
            } catch (error) {
              state.diagnostics.push('style snapshot failed: ' + String(error));
            }
            state.harness = {
              status: Number(harnessStatus.status),
              message: harnessStatus.message == null ? null : String(harnessStatus.message),
              stack: harnessStatus.stack == null ? null : String(harnessStatus.stack)
            };
            state.complete = true;
          });
        })();
        """;

    [GeneratedRegex(
        "<script\\b(?=[^>]*\\bsrc\\s*=\\s*(?:[\"']/resources/testharness\\.js[\"']|/resources/testharness\\.js(?=\\s|>)))[^>]*>\\s*</script\\s*>",
        RegexOptions.IgnoreCase | RegexOptions.Singleline)]
    private static partial Regex TestHarnessTagRegex();

    [GeneratedRegex(
        "<script\\b(?=[^>]*\\bsrc\\s*=\\s*(?:[\"']/resources/testharnessreport\\.js[\"']|/resources/testharnessreport\\.js(?=\\s|>)))[^>]*>\\s*</script\\s*>",
        RegexOptions.IgnoreCase | RegexOptions.Singleline)]
    private static partial Regex TestHarnessReportTagRegex();

    [GeneratedRegex(
        "<script\\b(?=[^>]*\\bsrc\\s*=\\s*(?:[\"']/resources/check-layout-th\\.js[\"']|/resources/check-layout-th\\.js(?=\\s|>)))[^>]*>\\s*</script\\s*>",
        RegexOptions.IgnoreCase | RegexOptions.Singleline)]
    private static partial Regex CheckLayoutHarnessTagRegex();

    [GeneratedRegex(
        "<script\\b(?=[^>]*\\bsrc\\s*=\\s*(?:[\"']/resources/testdriver(?:-actions|-vendor)?\\.js[\"']|/resources/testdriver(?:-actions|-vendor)?\\.js(?=\\s|>)))[^>]*>\\s*</script\\s*>",
        RegexOptions.IgnoreCase | RegexOptions.Singleline)]
    private static partial Regex TestDriverTagRegex();

    [GeneratedRegex("<link\\b[^>]*>", RegexOptions.IgnoreCase | RegexOptions.Singleline)]
    private static partial Regex LinkTagRegex();

    [GeneratedRegex("<script\\b[^>]*>\\s*</script\\s*>", RegexOptions.IgnoreCase | RegexOptions.Singleline)]
    private static partial Regex ScriptTagRegex();

    [GeneratedRegex(
        "<script\\b(?<attributes>[^>]*)>(?<source>[\\s\\S]*?)</script\\s*>",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex InlineScriptRegex();

    [GeneratedRegex(
        "<style\\b[^>]*>(?<source>[\\s\\S]*?)</style\\s*>",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex StyleElementRegex();

    [GeneratedRegex(
        "<body\\b[^>]*>(?<body>[\\s\\S]*?)</body\\s*>",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex BodyElementRegex();

    [GeneratedRegex(
        "<body\\b(?<before>[^>]*?)\\s+onload\\s*=\\s*(?:\"(?<double>[^\"]*)\"|'(?<single>[^']*)'|(?<bare>[^\\s>]+))(?<after>[^>]*)>",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex BodyOnLoadAttributeRegex();

    [GeneratedRegex(
        "\\brel\\s*=\\s*(?:\"(?<double>[^\"]*)\"|'(?<single>[^']*)'|(?<bare>[^\\s>]+))",
        RegexOptions.IgnoreCase)]
    private static partial Regex RelAttributeRegex();

    [GeneratedRegex(
        "(?<prefix>\\bhref\\s*=\\s*)(?:\"(?<double>[^\"]*)\"|'(?<single>[^']*)'|(?<bare>[^\\s>]+))",
        RegexOptions.IgnoreCase)]
    private static partial Regex HrefAttributeRegex();

    [GeneratedRegex(
        "(?<prefix>\\bsrc\\s*=\\s*)(?:\"(?<double>[^\"]*)\"|'(?<single>[^']*)'|(?<bare>[^\\s>]+))",
        RegexOptions.IgnoreCase)]
    private static partial Regex SrcAttributeRegex();

    private sealed class ManagedWptEngineEnvironment : IWptEngineEnvironment
    {
        private readonly AvaloniaBrowserHost _host;
        private readonly ClearScriptV8Runtime _runtime;
        private readonly bool _directDocument;
        private readonly Pointer _pointer = new(Pointer.GetNextFreeId(), PointerType.Mouse, true);
        private AvaloniaDomElement? _pointerTarget;

        internal ManagedWptEngineEnvironment(
            Window window,
            AvaloniaBrowserHost host,
            ClearScriptV8Runtime runtime,
            bool directDocument)
        {
            Window = window;
            _host = host;
            _runtime = runtime;
            _directDocument = directDocument;
        }

        internal Window Window { get; }

        public WptRenderSnapshot CaptureSnapshot(string documentName)
        {
            using var bitmap = Window.CaptureRenderedFrame()
                ?? throw new InvalidOperationException($"Could not capture '{documentName}'.");
            return CopyPixels(bitmap);
        }

        public void PumpInputAction()
        {
            var json = Convert.ToString(_runtime.Engine.Evaluate(_directDocument ? """
                (function () {
                  const queue = window.__webSceneWptInputActions;
                  const action = queue && queue.length ? queue.shift() : null;
                  return action ? JSON.stringify(action) : null;
                })()
                """ : """
                (function () {
                  const frame = window.__webSceneWptFrame;
                  const queue = frame && frame.contentWindow && frame.contentWindow.__webSceneWptInputActions;
                  const action = queue && queue.length ? queue.shift() : null;
                  return action ? JSON.stringify(action) : null;
                })()
                """));
            if (string.IsNullOrWhiteSpace(json))
            {
                return;
            }

            var action = JsonSerializer.Deserialize<InputAction>(json, JsonOptions)
                         ?? throw new InvalidDataException("WPT input action was empty.");
            string? error = null;
            try
            {
                var targetDocument = _directDocument
                    ? _host.Document
                    : (_host.Document.querySelector("#webscene-wpt-frame") as AvaloniaDomElement)?
                        .contentDocument as AvaloniaDomDocument
                      ?? throw new InvalidOperationException("WPT iframe document is unavailable.");
                var target = targetDocument.querySelector("#" + action.TargetId) as AvaloniaDomElement
                             ?? throw new InvalidOperationException(
                                 $"WPT input target '#{action.TargetId}' was not found.");

                switch (action.Type)
                {
                    case "pointerMove":
                        MovePointerTo(target);
                        break;
                    case "click":
                        Click(target);
                        break;
                    case "contextClick":
                        Click(target, MouseButton.Right);
                        break;
                    case "wheel":
                        Wheel(target, action.Value);
                        break;
                    case "resize":
                        Resize(action.Value);
                        break;
                    case "sendKeys":
                        SendKeys(target, action.Value);
                        break;
                    default:
                        throw new NotSupportedException(
                            $"WPT input action '{action.Type}' is not supported by this subset runner.");
                }

                Dispatcher.UIThread.RunJobs();
            }
            catch (Exception exception)
            {
                error = exception.Message;
            }

            var completionTarget = _directDocument
                ? "window"
                : "window.__webSceneWptFrame.contentWindow";
            _runtime.Engine.Execute(
                $"{completionTarget}.__webSceneCompleteInputAction(" +
                $"{action.Id}, {JsonSerializer.Serialize(error)});");
        }

        private void MovePointerTo(AvaloniaDomElement target)
        {
            var localPoint = new Point(
                Math.Max(0, target.Control.Bounds.Width / 2),
                Math.Max(0, target.Control.Bounds.Height / 2));
            var rootPoint = target.Control.TranslatePoint(localPoint, Window) ?? localPoint;
            if (_pointerTarget is not null && !ReferenceEquals(_pointerTarget, target))
            {
                RaiseBoundary(_pointerTarget, InputElement.PointerExitedEvent, rootPoint);
            }
            if (!ReferenceEquals(_pointerTarget, target))
            {
                RaiseBoundary(target, InputElement.PointerEnteredEvent, rootPoint);
                _pointerTarget = target;
            }
        }

        private void Click(AvaloniaDomElement target, MouseButton button = MouseButton.Left)
        {
            MovePointerTo(target);
            var localPoint = new Point(
                Math.Max(0, target.Control.Bounds.Width / 2),
                Math.Max(0, target.Control.Bounds.Height / 2));
            var rootPoint = target.Control.TranslatePoint(localPoint, Window) ?? localPoint;
            var pressedModifiers = button switch
            {
                MouseButton.Left => RawInputModifiers.LeftMouseButton,
                MouseButton.Middle => RawInputModifiers.MiddleMouseButton,
                MouseButton.Right => RawInputModifiers.RightMouseButton,
                _ => RawInputModifiers.None
            };
            var pressedKind = button switch
            {
                MouseButton.Left => PointerUpdateKind.LeftButtonPressed,
                MouseButton.Middle => PointerUpdateKind.MiddleButtonPressed,
                MouseButton.Right => PointerUpdateKind.RightButtonPressed,
                _ => PointerUpdateKind.Other
            };
            var releasedKind = button switch
            {
                MouseButton.Left => PointerUpdateKind.LeftButtonReleased,
                MouseButton.Middle => PointerUpdateKind.MiddleButtonReleased,
                MouseButton.Right => PointerUpdateKind.RightButtonReleased,
                _ => PointerUpdateKind.Other
            };
            target.Control.RaiseEvent(new PointerPressedEventArgs(
                target.Control,
                _pointer,
                Window,
                rootPoint,
                1,
                new PointerPointProperties(
                    pressedModifiers,
                    pressedKind),
                KeyModifiers.None));

            if (button == MouseButton.Left)
            {
                Focus(target, NavigationMethod.Pointer);
            }

            target.Control.RaiseEvent(new PointerReleasedEventArgs(
                target.Control,
                _pointer,
                Window,
                rootPoint,
                2,
                new PointerPointProperties(
                    RawInputModifiers.None,
                    releasedKind),
                KeyModifiers.None,
                button));
        }

        private void Wheel(AvaloniaDomElement target, string? value)
        {
            MovePointerTo(target);
            if (!double.TryParse(value, out var deltaY))
            {
                throw new NotSupportedException($"WPT wheel delta '{value}' was not numeric.");
            }
            var localPoint = new Point(
                Math.Max(0, target.Control.Bounds.Width / 2),
                Math.Max(0, target.Control.Bounds.Height / 2));
            var rootPoint = target.Control.TranslatePoint(localPoint, Window) ?? localPoint;
            target.Control.RaiseEvent(new PointerWheelEventArgs(
                target.Control,
                _pointer,
                Window,
                rootPoint,
                3,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
                KeyModifiers.None,
                new Vector(0, -deltaY / 100)));
        }

        private void Resize(string? value)
        {
            var size = JsonSerializer.Deserialize<double[]>(value ?? "[]") ?? [];
            if (size.Length != 2 || size[0] <= 1 || size[1] <= 1)
            {
                throw new NotSupportedException($"WPT viewport size '{value}' was invalid.");
            }
            Window.Width = size[0];
            Window.Height = size[1];
            Dispatcher.UIThread.RunJobs();
        }

        private static void SendKeys(AvaloniaDomElement target, string? keys)
        {
            Focus(target, NavigationMethod.Tab);
            if (string.Equals(keys, "\uE004", StringComparison.Ordinal))
            {
                RaiseKey(target, InputElement.KeyDownEvent, Key.Tab, "Tab");
                RaiseKey(target, InputElement.KeyUpEvent, Key.Tab, "Tab");
                return;
            }
            if (string.IsNullOrEmpty(keys))
            {
                throw new NotSupportedException("WPT send_keys requires a non-empty value.");
            }
            foreach (var rune in keys.EnumerateRunes())
            {
                var key = PrintableAsciiKey(rune);
                var symbol = rune.ToString();
                RaiseKey(target, InputElement.KeyDownEvent, key, symbol);
                target.Control.RaiseEvent(new TextInputEventArgs
                {
                    RoutedEvent = InputElement.TextInputEvent,
                    Source = target.Control,
                    Text = symbol
                });
                RaiseKey(target, InputElement.KeyUpEvent, key, symbol);
            }
        }

        private static void RaiseKey(
            AvaloniaDomElement target,
            RoutedEvent routedEvent,
            Key key,
            string symbol)
        {
            target.Control.RaiseEvent(new KeyEventArgs
            {
                RoutedEvent = routedEvent,
                Source = target.Control,
                Key = key,
                KeySymbol = symbol,
                KeyModifiers = KeyModifiers.None
            });
        }

        private static Key PrintableAsciiKey(Rune rune)
        {
            var scalar = rune.Value;
            if (scalar is >= 'a' and <= 'z') scalar -= 'a' - 'A';
            if (scalar is >= 'A' and <= 'Z'
                && Enum.TryParse<Key>(((char)scalar).ToString(), out var letter))
            {
                return letter;
            }
            if (scalar is >= '0' and <= '9'
                && Enum.TryParse<Key>($"D{(char)scalar}", out var digit))
            {
                return digit;
            }
            if (scalar == ' ') return Key.Space;
            throw new NotSupportedException(
                $"WPT send_keys currently supports printable ASCII letters, digits, and space, not '{rune}'.");
        }

        private static void Focus(AvaloniaDomElement target, NavigationMethod method)
        {
            target.Control.Focusable = true;
            if (!target.Control.Focus(method, KeyModifiers.None) && !target.Control.IsFocused)
            {
                throw new InvalidOperationException(
                    $"WPT input target '#{target.id}' could not receive {method} focus.");
            }
        }

        private void RaiseBoundary(
            AvaloniaDomElement target,
            RoutedEvent routedEvent,
            Point rootPoint)
        {
            target.Control.RaiseEvent(new PointerEventArgs(
                routedEvent,
                target.Control,
                _pointer,
                Window,
                rootPoint,
                0,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
                KeyModifiers.None));
        }

        public string? ReadState()
            => Convert.ToString(_runtime.Engine.Evaluate(_directDocument ? """
                (function () {
                  const state = window.__webSceneWptState;
                  return state ? JSON.stringify(state) : null;
                })()
                """ : """
                (function () {
                  const frame = window.__webSceneWptFrame;
                  const state = frame && frame.contentWindow && frame.contentWindow.__webSceneWptState;
                  return state ? JSON.stringify(state) : null;
                })()
                """));

        public bool IsFrameComplete()
            => _directDocument || Convert.ToBoolean(_runtime.Engine.Evaluate("""
                Boolean(window.__webSceneWptFrame &&
                  window.__webSceneWptFrame.contentDocument &&
                  window.__webSceneWptFrame.contentDocument.readyState === 'complete')
                """));

        public void SettleFrame()
        {
            if (_directDocument)
            {
                _host.Document.EnsureStylesCurrent();
                Window.InvalidateMeasure();
                Dispatcher.UIThread.RunJobs();
                return;
            }
            var frame = _host.Document.querySelector("#webscene-wpt-frame") as AvaloniaDomElement;
            if (frame?.contentDocument is not AvaloniaDomDocument frameDocument)
            {
                return;
            }

            frameDocument.EnsureStylesCurrent();
            frame.Control.InvalidateMeasure();
            Window.InvalidateMeasure();
            Dispatcher.UIThread.RunJobs();
        }

        public void Dispose()
        {
            _pointer.Dispose();
            _runtime.Dispose();
            _host.Dispose();
            Window.Close();
            Dispatcher.UIThread.RunJobs();
        }

        private sealed class InputAction
        {
            public int Id { get; init; }
            public required string Type { get; init; }
            public required string TargetId { get; init; }
            public string? Value { get; init; }
        }
    }

}
