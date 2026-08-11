using System.Diagnostics;
using Avalonia;
using Avalonia.Platform;
using SkiaSharp;

namespace WebScene.WebPlatformSubset.Runner;

/// <summary>
/// Supplies an independent, non-gating Chromium view of static reftests and
/// self-verifying visual tests. The ordinary WPT result remains native-owned;
/// these metrics expose references or visual pass conditions that fail in
/// Chromium and common-mode native rendering defects.
/// </summary>
internal sealed class ChromiumReftestOracle
{
    private readonly string _executable;
    private readonly ViewportSettings _viewport;
    private readonly TimeSpan _timeout;

    internal ChromiumReftestOracle(
        string executable,
        ViewportSettings viewport,
        TimeSpan timeout)
    {
        if (!File.Exists(executable))
        {
            throw new FileNotFoundException(
                "The Chromium oracle executable was not found.",
                executable);
        }
        _executable = executable;
        _viewport = viewport;
        _timeout = timeout;
        Identity = ReadIdentity();
    }

    internal string Identity { get; }

    internal ChromiumOracleResult Compare(
        string testPath,
        string referencePath,
        string artifactDirectory,
        WptRenderSnapshot nativeTest)
    {
        var chromiumTestPath = Path.Combine(artifactDirectory, "chromium-actual.png");
        var chromiumReferencePath = Path.Combine(artifactDirectory, "chromium-reference.png");
        try
        {
            Capture(testPath, chromiumTestPath);
            Capture(referencePath, chromiumReferencePath);
            var chromiumTest = ReadSnapshot(chromiumTestPath);
            var chromiumReference = ReadSnapshot(chromiumReferencePath);
            var chromiumComparison = ComparePixels(chromiumTest, chromiumReference);
            var nativeComparison = ComparePixels(nativeTest, chromiumTest);
            return new ChromiumOracleResult
            {
                Status = chromiumComparison.DifferingPixels == 0 ? "PASS" : "FAIL",
                Message = chromiumComparison.DifferingPixels == 0
                    ? "Chromium renders the test and reference identically."
                    : "Chromium does not render the test and reference identically in this environment.",
                ChromiumTestToReference = chromiumComparison,
                NativeToChromiumTest = nativeComparison,
                Artifacts = new Dictionary<string, string>
                {
                    ["test"] = chromiumTestPath,
                    ["reference"] = chromiumReferencePath,
                    ["nativeTest"] = Path.Combine(artifactDirectory, "native-actual.png"),
                    ["nativeReference"] = Path.Combine(artifactDirectory, "native-reference.png")
                }
            };
        }
        catch (Exception exception)
        {
            return new ChromiumOracleResult
            {
                Status = "ERROR",
                Message = exception.Message,
                Artifacts = new Dictionary<string, string>
                {
                    ["test"] = chromiumTestPath,
                    ["reference"] = chromiumReferencePath
                }
            };
        }
    }

    internal ChromiumOracleResult InspectVisual(
        string testPath,
        string artifactDirectory,
        WptRenderSnapshot nativeTest,
        IReadOnlyList<VisualColorCheck> checks)
    {
        var chromiumTestPath = Path.Combine(artifactDirectory, "chromium-actual.png");
        try
        {
            Capture(testPath, chromiumTestPath);
            var chromiumTest = ReadSnapshot(chromiumTestPath);
            var observations = checks.Select(check =>
            {
                var count = VisualColorOracle.Count(chromiumTest, check.Color);
                return new
                {
                    Check = check,
                    Count = count,
                    Passed = VisualColorOracle.Passes(check, count)
                };
            }).ToList();
            var passed = observations.All(item => item.Passed);
            return new ChromiumOracleResult
            {
                Status = passed ? "PASS" : "FAIL",
                Message = string.Join(
                    " ",
                    observations.Select(item =>
                        $"{item.Check.Color}: observed {item.Count}, expected "
                        + VisualColorOracle.DescribeBounds(item.Check) + ".")),
                NativeToChromiumTest = ComparePixels(nativeTest, chromiumTest),
                Artifacts = new Dictionary<string, string>
                {
                    ["test"] = chromiumTestPath,
                    ["nativeTest"] = Path.Combine(artifactDirectory, "actual.png")
                }
            };
        }
        catch (Exception exception)
        {
            return new ChromiumOracleResult
            {
                Status = "ERROR",
                Message = exception.Message,
                Artifacts = new Dictionary<string, string>
                {
                    ["test"] = chromiumTestPath
                }
            };
        }
    }

    private string ReadIdentity()
    {
        var startInfo = new ProcessStartInfo(_executable)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };
        startInfo.ArgumentList.Add("--version");
        using var process = Process.Start(startInfo)
            ?? throw new InvalidOperationException("Could not start the Chromium oracle.");
        var outputTask = process.StandardOutput.ReadToEndAsync();
        var errorTask = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit(checked((int)Math.Min(_timeout.TotalMilliseconds, int.MaxValue))))
        {
            process.Kill(entireProcessTree: true);
            process.WaitForExit();
            throw new TimeoutException("Chromium did not report its version before the oracle timeout.");
        }
        Task.WaitAll(outputTask, errorTask);
        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException(
                $"Chromium --version failed with exit code {process.ExitCode}: {errorTask.Result.Trim()}");
        }
        return outputTask.Result.Trim();
    }

    private void Capture(string documentPath, string screenshotPath)
    {
        var profileDirectory = Path.Combine(
            Path.GetTempPath(),
            "webscene-chromium-oracle-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(profileDirectory);
        try
        {
            var startInfo = new ProcessStartInfo(_executable)
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false
            };
            foreach (var argument in new[]
                     {
                         "--headless=new",
                         "--disable-background-networking",
                         "--disable-default-apps",
                         "--disable-extensions",
                         "--disable-sync",
                         "--no-first-run",
                         "--no-default-browser-check",
                         "--allow-file-access-from-files",
                         "--force-device-scale-factor=1",
                         "--run-all-compositor-stages-before-draw",
                         $"--window-size={_viewport.Width},{_viewport.Height}",
                         $"--user-data-dir={profileDirectory}",
                         $"--screenshot={screenshotPath}",
                         new Uri(documentPath).AbsoluteUri
                     })
            {
                startInfo.ArgumentList.Add(argument);
            }

            using var process = Process.Start(startInfo)
                ?? throw new InvalidOperationException("Could not start the Chromium oracle.");
            var outputTask = process.StandardOutput.ReadToEndAsync();
            var errorTask = process.StandardError.ReadToEndAsync();
            var deadline = Stopwatch.StartNew();
            var screenshotReady = false;
            long previousLength = -1;
            while (deadline.Elapsed < _timeout && !process.HasExited)
            {
                if (File.Exists(screenshotPath))
                {
                    var length = new FileInfo(screenshotPath).Length;
                    if (length > 0 && length == previousLength)
                    {
                        screenshotReady = true;
                        break;
                    }
                    previousLength = length;
                }
                Thread.Sleep(25);
            }
            screenshotReady |= File.Exists(screenshotPath)
                && new FileInfo(screenshotPath).Length > 0;
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
                process.WaitForExit();
            }
            Task.WaitAll(outputTask, errorTask);
            if (!screenshotReady)
            {
                throw new TimeoutException(
                    $"Chromium did not capture '{documentPath}' before the oracle timeout.");
            }
            if (!File.Exists(screenshotPath))
            {
                throw new InvalidOperationException(
                    $"Chromium screenshot failed for '{documentPath}' with exit code " +
                    $"{process.ExitCode}: {errorTask.Result.Trim()} {outputTask.Result.Trim()}");
            }
        }
        finally
        {
            if (Directory.Exists(profileDirectory))
            {
                Directory.Delete(profileDirectory, recursive: true);
            }
        }
    }

    private static WptRenderSnapshot ReadSnapshot(string path)
    {
        using var bitmap = SKBitmap.Decode(path)
            ?? throw new InvalidDataException($"Chromium screenshot '{path}' is not a readable PNG.");
        var pixels = new byte[checked(bitmap.Width * bitmap.Height * 4)];
        var offset = 0;
        for (var y = 0; y < bitmap.Height; y++)
        {
            for (var x = 0; x < bitmap.Width; x++)
            {
                var color = bitmap.GetPixel(x, y);
                pixels[offset++] = color.Blue;
                pixels[offset++] = color.Green;
                pixels[offset++] = color.Red;
                pixels[offset++] = color.Alpha;
            }
        }
        return new WptRenderSnapshot(
            new PixelSize(bitmap.Width, bitmap.Height),
            new Vector(96, 96),
            PixelFormat.Bgra8888,
            pixels);
    }

    private static PixelComparison ComparePixels(
        WptRenderSnapshot actual,
        WptRenderSnapshot expected)
    {
        var width = Math.Max(actual.PixelSize.Width, expected.PixelSize.Width);
        var height = Math.Max(actual.PixelSize.Height, expected.PixelSize.Height);
        var totalPixels = checked((long)width * height);
        long differingPixels = 0;
        var maximumChannelDelta = 0;
        var minimumX = width;
        var minimumY = height;
        var maximumX = -1;
        var maximumY = -1;

        for (var y = 0; y < height; y++)
        {
            for (var x = 0; x < width; x++)
            {
                var actualOffset = (y * actual.PixelSize.Width + x) * 4;
                var expectedOffset = (y * expected.PixelSize.Width + x) * 4;
                var insideActual = x < actual.PixelSize.Width && y < actual.PixelSize.Height;
                var insideExpected = x < expected.PixelSize.Width && y < expected.PixelSize.Height;
                var differs = !insideActual || !insideExpected;
                if (!differs)
                {
                    for (var channel = 0; channel < 4; channel++)
                    {
                        var delta = Math.Abs(
                            actual.Pixels[actualOffset + channel]
                            - expected.Pixels[expectedOffset + channel]);
                        maximumChannelDelta = Math.Max(maximumChannelDelta, delta);
                        differs |= delta != 0;
                    }
                }
                else
                {
                    maximumChannelDelta = byte.MaxValue;
                }
                if (!differs) continue;
                differingPixels++;
                minimumX = Math.Min(minimumX, x);
                minimumY = Math.Min(minimumY, y);
                maximumX = Math.Max(maximumX, x);
                maximumY = Math.Max(maximumY, y);
            }
        }

        return new PixelComparison
        {
            Width = width,
            Height = height,
            TotalPixels = totalPixels,
            DifferingPixels = differingPixels,
            DifferingRatio = totalPixels == 0 ? 0 : (double)differingPixels / totalPixels,
            MaximumChannelDelta = maximumChannelDelta,
            DifferenceBounds = differingPixels == 0
                ? null
                : $"{minimumX},{minimumY},{maximumX},{maximumY}"
        };
    }
}
