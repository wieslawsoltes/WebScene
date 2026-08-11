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

namespace WebScene.WebPlatformSubset.Runner;

internal sealed partial class WptSubsetRunner
{
    private const string ArtifactSchema = "webscene-wpt-subset-result-v3";
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
    private readonly string _manifestSha256;
    private readonly string _testHarness;
    private readonly string _checkLayoutHarness;
    private readonly HashSet<string> _pinnedUpstreamFiles;
    private readonly ChromiumReftestOracle? _chromiumOracle;

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
        var manifestText = File.ReadAllText(options.ManifestPath);
        _manifest = JsonSerializer.Deserialize<ProfileManifest>(
                        manifestText,
                        JsonOptions)
                    ?? throw new InvalidDataException("The profile manifest is empty.");
        _manifestSha256 = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(
                manifestText.Replace("\r\n", "\n", StringComparison.Ordinal).Replace('\r', '\n'))))
            .ToLowerInvariant();
        _testHarness = File.ReadAllText(Path.Combine(_upstreamRoot, "resources", "testharness.js"));
        _checkLayoutHarness = File.ReadAllText(Path.Combine(_upstreamRoot, "resources", "check-layout-th.js"));
        _chromiumOracle = string.IsNullOrWhiteSpace(options.ChromiumPath)
            ? null
            : new ChromiumReftestOracle(options.ChromiumPath, _manifest.Viewport, options.Timeout);
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
            var result = test.Type switch
            {
                "reftest" => RunReftest(test),
                "visual" => RunVisualTest(test),
                _ => RunTestHarness(test)
            };
            results.Add(result);
            Console.WriteLine($"{result.Status} ({result.Duration.TotalMilliseconds:F0} ms)");
            foreach (var failedSubtest in result.Subtests.Where(item => item.Status != "PASS"))
            {
                Console.WriteLine($"     {failedSubtest.Status}: {failedSubtest.Name}: {failedSubtest.Message}");
            }
            if (result.ChromiumOracle is not null)
            {
                var crossEngine = result.ChromiumOracle.NativeToChromiumTest;
                var differential = crossEngine is null
                    ? string.Empty
                    : $", native/Chromium differing pixels={crossEngine.DifferingPixels} " +
                      $"({crossEngine.DifferingRatio:P3})";
                Console.WriteLine($"     Chromium oracle: {result.ChromiumOracle.Status}{differential}");
            }
        }

        timer.Stop();
        var artifact = new RunArtifact
        {
            Schema = ArtifactSchema,
            Profile = _manifest.Profile,
            ProfileSha256 = _manifestSha256,
            WptRevision = _manifest.WptRevision,
            Runtime = _manifest.Runtime,
            Engine = "native",
            NativeEngineIdentity = ResolveNativeEngineIdentity(),
            ChromiumIdentity = _chromiumOracle?.Identity,
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
            ChromiumOracleResult? chromiumOracle = null;
            if (_chromiumOracle is not null)
            {
                var oracleArtifactDirectory = Path.Combine(
                    _options.OutputDirectory,
                    SanitizeArtifactName(test.Path));
                Directory.CreateDirectory(oracleArtifactDirectory);
                SavePixels(actual, Path.Combine(oracleArtifactDirectory, "native-actual.png"));
                SavePixels(reference, Path.Combine(oracleArtifactDirectory, "native-reference.png"));
                chromiumOracle = _chromiumOracle.Compare(
                    TestDocumentPath(test.Path),
                    TestDocumentPath(test.Reference),
                    oracleArtifactDirectory,
                    actual);
            }
            timer.Stop();
            var equal = actual.PixelSize == reference.PixelSize && actual.Pixels.SequenceEqual(reference.Pixels);
            if (equal)
            {
                return new TestResult
                {
                    Path = test.Path,
                    Type = test.Type,
                    Status = "PASS",
                    Duration = timer.Elapsed,
                    ChromiumOracle = chromiumOracle
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
                ChromiumOracle = chromiumOracle,
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

    private TestResult RunVisualTest(ProfileTest test)
    {
        var timer = Stopwatch.StartNew();
        try
        {
            var snapshot = RenderDocument(
                File.ReadAllText(TestDocumentPath(test.Path)),
                test.Path);
            var artifactDirectory = Path.Combine(
                _options.OutputDirectory,
                SanitizeArtifactName(test.Path));
            Directory.CreateDirectory(artifactDirectory);
            var actualPath = Path.Combine(artifactDirectory, "actual.png");
            SavePixels(snapshot, actualPath);

            var colorChecks = test.VisualChecks.Select(check =>
            {
                var count = VisualColorOracle.Count(snapshot, check.Color);
                var passed = VisualColorOracle.Passes(check, count);
                var bounds = VisualColorOracle.DescribeBounds(check);
                return new SubtestResult
                {
                    Name = check.Description ?? $"{check.Color} pixel count is {bounds}",
                    Status = passed ? "PASS" : "FAIL",
                    Message = $"Observed {count} exact opaque-or-visible {check.Color} pixels; expected {bounds}."
                };
            }).ToList();
            var gapChecks = test.VisualGapChecks.Select(check =>
            {
                var observation = VisualColorOracle.MeasureGap(snapshot, check);
                return new SubtestResult
                {
                    Name = check.Description
                        ?? $"{check.FirstColor} to {check.SecondColor} {check.Axis} gap",
                    Status = observation.Passed ? "PASS" : "FAIL",
                    Message = observation.Message
                };
            });
            var componentChecks = test.VisualComponentChecks.Select(check =>
            {
                var observation = VisualColorOracle.InspectComponent(snapshot, check);
                return new SubtestResult
                {
                    Name = check.Description ?? "foreground component shape",
                    Status = observation.Passed ? "PASS" : "FAIL",
                    Message = observation.Message
                };
            });
            var checks = colorChecks.Concat(gapChecks).Concat(componentChecks).ToList();
            timer.Stop();
            var passed = checks.All(check => check.Status == "PASS");
            return new TestResult
            {
                Path = test.Path,
                Type = test.Type,
                Status = passed ? "PASS" : "FAIL",
                Duration = timer.Elapsed,
                Message = passed
                    ? "The self-verifying visual color oracle passed."
                    : "The self-verifying visual color oracle failed.",
                Subtests = checks,
                Artifacts = new Dictionary<string, string>
                {
                    ["actual"] = actualPath
                },
                ChromiumOracle = _chromiumOracle?.InspectVisual(
                    TestDocumentPath(test.Path),
                    artifactDirectory,
                    snapshot,
                    test.VisualChecks,
                    test.VisualGapChecks,
                    test.VisualComponentChecks)
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
        return new NativeWptEngineEnvironment(
            _options,
            _manifest.Viewport,
            _upstreamRoot,
            documentPath,
            html);
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
        if (!string.IsNullOrWhiteSpace(_options.ChromiumPath)
            && !File.Exists(_options.ChromiumPath))
        {
            throw new FileNotFoundException(
                "The Chromium oracle executable was not found.",
                _options.ChromiumPath);
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
            if (test.Type is not ("testharness" or "reftest" or "contract" or "visual"))
            {
                throw new InvalidDataException($"Unknown test type '{test.Type}' for '{test.Path}'.");
            }
            if (test.Type == "reftest" &&
                (string.IsNullOrWhiteSpace(test.Reference) || !File.Exists(TestDocumentPath(test.Reference))))
            {
                throw new FileNotFoundException($"Reference for '{test.Path}' is missing.");
            }
            if (test.Type == "visual")
            {
                if (test.VisualChecks.Count == 0)
                {
                    throw new InvalidDataException(
                        $"Visual test '{test.Path}' has no color checks.");
                }
                foreach (var check in test.VisualChecks)
                {
                    _ = VisualColorOracle.Parse(check.Color);
                    if (!check.MinimumPixels.HasValue && !check.MaximumPixels.HasValue)
                    {
                        throw new InvalidDataException(
                            $"Visual test '{test.Path}' color '{check.Color}' has no pixel bound.");
                    }
                    if (check.MinimumPixels is < 0 || check.MaximumPixels is < 0
                        || check.MinimumPixels > check.MaximumPixels)
                    {
                        throw new InvalidDataException(
                            $"Visual test '{test.Path}' color '{check.Color}' has invalid pixel bounds.");
                    }
                }
                foreach (var check in test.VisualGapChecks)
                {
                    _ = VisualColorOracle.Parse(check.FirstColor);
                    _ = VisualColorOracle.Parse(check.SecondColor);
                    if (check.Axis is not ("horizontal" or "vertical"))
                    {
                        throw new InvalidDataException(
                            $"Visual test '{test.Path}' has invalid gap axis '{check.Axis}'.");
                    }
                    if (!check.MinimumPixels.HasValue && !check.MaximumPixels.HasValue)
                    {
                        throw new InvalidDataException(
                            $"Visual test '{test.Path}' gap has no pixel bound.");
                    }
                    if (check.MinimumPixels is < 0 || check.MaximumPixels is < 0
                        || check.MinimumPixels > check.MaximumPixels)
                    {
                        throw new InvalidDataException(
                            $"Visual test '{test.Path}' gap has invalid pixel bounds.");
                    }
                }
                foreach (var check in test.VisualComponentChecks)
                {
                    if (check.X < 0 || check.Y < 0 || check.Width <= 0 || check.Height <= 0
                        || check.X + check.Width > _manifest.Viewport.Width
                        || check.Y + check.Height > _manifest.Viewport.Height
                        || check.MaximumLuminance is < 0 or > 255
                        || check.MinimumWidth is < 0 || check.MaximumWidth is < 0
                        || check.MinimumHeight is < 0 || check.MaximumHeight is < 0
                        || check.MinimumPixels is < 0
                        || check.MinimumWidth > check.MaximumWidth
                        || check.MinimumHeight > check.MaximumHeight
                        || check.MinimumFillRatio is < 0 or > 1)
                    {
                        throw new InvalidDataException(
                            $"Visual test '{test.Path}' component shape has invalid bounds.");
                    }
                    if (!check.MinimumWidth.HasValue && !check.MaximumWidth.HasValue
                        && !check.MinimumHeight.HasValue && !check.MaximumHeight.HasValue
                        && !check.MinimumPixels.HasValue && !check.MinimumFillRatio.HasValue)
                    {
                        throw new InvalidDataException(
                            $"Visual test '{test.Path}' component shape has no assertion.");
                    }
                }
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
            , inputActions: []
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
            state.inputActions.push('completed:' + String(id) + ':' +
              (error == null ? 'ok' : String(error)));
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
              const rect = target.getBoundingClientRect();
              const centerX = rect.left + rect.width / 2;
              const centerY = rect.top + rect.height / 2;
              const hit = document.elementFromPoint(centerX, centerY);
              state.inputActions.push('queued:' + String(id) + ':' + String(type) + ':' +
                String(target.id) + '@' + [rect.left, rect.top, rect.width, rect.height].join(',') +
                ':hit=' + String(hit && hit.id || ''));
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
              if (state.inputActions.length) {
                state.diagnostics.push('input-actions=' + JSON.stringify(state.inputActions));
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

}
