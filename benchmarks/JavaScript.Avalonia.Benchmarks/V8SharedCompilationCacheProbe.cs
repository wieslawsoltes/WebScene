using Avalonia.Controls;
using Avalonia.Threading;
using JavaScript.Avalonia;
using JavaScript.Avalonia.ClearScript;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Product-free proof that four isolated V8 runtimes share immutable source and
/// compilation cache data without sharing globals, module exports, DOM roots, or lifetime.
/// </summary>
internal static class V8SharedCompilationCacheProbe
{
    private const string SharedScript = """
        globalThis.__webSceneSharedCacheToken = String(__webSceneInstanceToken);
        globalThis.__webSceneSharedCacheExecutions =
          (globalThis.__webSceneSharedCacheExecutions || 0) + 1;
        """;

    internal static int Run(string[] args)
    {
        if (TryReadProcessWorker(args, out var processDirectory, out var expectWarm))
        {
            return RunProcessWorker(processDirectory, expectWarm);
        }

        BenchmarkApp.EnsureInitialized();
        var callerThreadId = Environment.CurrentManagedThreadId;
        var temporaryDirectory = Path.Combine(
            Path.GetTempPath(),
            "webscene-v8-shared-cache-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temporaryDirectory);
        var modulePath = Path.Combine(temporaryDirectory, "shared-module.js");
        var moduleContent = "globalThis.__webSceneSharedModuleLoads = " +
            "(globalThis.__webSceneSharedModuleLoads || 0) + 1; " +
            "module.exports = { loads: globalThis.__webSceneSharedModuleLoads };\n";
        File.WriteAllText(modulePath, moduleContent);

        var persistentRoot = Path.Combine(temporaryDirectory, "persistent");
        var cacheOptions = CreatePersistentOptions(persistentRoot, "probe-compatible-v1");
        var cache = new ClearScriptV8SharedCache(cacheOptions);
        var compilationSources = new[]
        {
            new V8CompilationSource("v8-shared-cache-probe.js", SharedScript),
            ClearScriptV8Runtime.CreateCommonJsCompilationSource(modulePath, moduleContent),
            new V8CompilationSource("v8-shared-cache-heavy-probe.js", CreateHeavyScript())
        };
        var precompileTasks = Enumerable.Range(0, 4)
            .Select(_ => ClearScriptV8Runtime.PrecompileAsync(
                cache,
                compilationSources,
                includeRuntimeBootstrap: false))
            .ToArray();
        var heartbeatCount = 0;
        var concurrentTask = Task.WhenAll(precompileTasks);
        using var heartbeatCancellation = new CancellationTokenSource();
        var heartbeatProducer = Task.Run(async () =>
        {
            while (!heartbeatCancellation.IsCancellationRequested)
            {
                await Task.Delay(2, heartbeatCancellation.Token);
                Dispatcher.UIThread.Post(() => Interlocked.Increment(ref heartbeatCount));
            }
        });
        while (!concurrentTask.IsCompleted)
        {
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(1);
        }
        heartbeatCancellation.Cancel();
        try
        {
            heartbeatProducer.GetAwaiter().GetResult();
        }
        catch (OperationCanceledException)
        {
        }
        Dispatcher.UIThread.RunJobs();
        var precompileResults = concurrentTask.GetAwaiter().GetResult();
        var afterConcurrentPrecompile = cache.GetMetrics();
        var concurrentSingleFlight = precompileResults.Sum(static result => result.Compiled) == 3
                                     && precompileResults.Sum(static result => result.Reused) == 9
                                     && precompileResults.All(result =>
                                         result.RanOnThreadPool
                                         && result.WorkerThreadId != callerThreadId)
                                     && afterConcurrentPrecompile.CompilationLeaders == 3
                                     && afterConcurrentPrecompile.CodeMisses == 3
                                     && afterConcurrentPrecompile.CodeHits == 9
                                     && afterConcurrentPrecompile.DiskWrites == 3
                                     && heartbeatCount > 0;

        var warmCache = new ClearScriptV8SharedCache(cacheOptions);
        var warmResult = ClearScriptV8Runtime.PrecompileAsync(
                warmCache,
                compilationSources,
                includeRuntimeBootstrap: false)
            .GetAwaiter()
            .GetResult();
        var warmMetrics = warmCache.GetMetrics();
        var warmRestart = warmResult.Compiled == 0
                          && warmResult.Reused == 3
                          && warmMetrics.DiskHits == 3
                          && warmMetrics.CompilationLeaders == 0;

        var filesBeforeEdit = Directory.GetFiles(warmCache.PersistentDirectory!, "*.v8cache")
            .ToHashSet(StringComparer.Ordinal);
        var editedSource = new V8CompilationSource(
            "v8-shared-cache-probe.js",
            SharedScript + "\nglobalThis.__webSceneEdited = true;");
        var editResult = ClearScriptV8Runtime.PrecompileAsync(
                warmCache,
                new[] { editedSource },
                includeRuntimeBootstrap: false)
            .GetAwaiter()
            .GetResult();
        var filesAfterEdit = Directory.GetFiles(warmCache.PersistentDirectory!, "*.v8cache")
            .ToHashSet(StringComparer.Ordinal);
        var editedPath = filesAfterEdit.Except(filesBeforeEdit, StringComparer.Ordinal).Single();
        var sourceInvalidation = editResult.Compiled == 1
                                 && filesAfterEdit.Count == filesBeforeEdit.Count + 1;

        File.WriteAllBytes(editedPath, new byte[] { 0x48, 0x4D, 0x4C });
        var recoveryCache = new ClearScriptV8SharedCache(cacheOptions);
        var recoveryResult = ClearScriptV8Runtime.PrecompileAsync(
                recoveryCache,
                new[] { editedSource },
                includeRuntimeBootstrap: false)
            .GetAwaiter()
            .GetResult();
        var recoveryMetrics = recoveryCache.GetMetrics();
        var corruptionRecovery = recoveryResult.Compiled == 1
                                 && recoveryMetrics.DiskInvalidEntries == 1
                                 && recoveryMetrics.DiskWrites == 1;

        var incompatibleCache = new ClearScriptV8SharedCache(
            CreatePersistentOptions(persistentRoot, "probe-incompatible-v2"));
        var incompatibleResult = ClearScriptV8Runtime.PrecompileAsync(
                incompatibleCache,
                compilationSources,
                includeRuntimeBootstrap: false)
            .GetAwaiter()
            .GetResult();
        var compatibilityIsolation = incompatibleResult.Compiled == 3
                                     && incompatibleCache.GetMetrics().DiskHits == 0
                                     && !string.Equals(
                                         incompatibleCache.PersistentDirectory,
                                         cache.PersistentDirectory,
                                         StringComparison.Ordinal);

        var boundedCache = new ClearScriptV8SharedCache(new ClearScriptV8SharedCacheOptions
        {
            PersistentDirectory = persistentRoot,
            CompatibilityTag = "probe-bounded-v1",
            MaxPersistentEntries = 2,
            MaxPersistentBytes = 64 * 1024 * 1024
        });
        var boundedResult = ClearScriptV8Runtime.PrecompileAsync(
                boundedCache,
                Enumerable.Range(0, 3).Select(index => new V8CompilationSource(
                    $"v8-bounded-{index}.js",
                    $"globalThis.__webSceneBounded{index} = {index};")),
                includeRuntimeBootstrap: false)
            .GetAwaiter()
            .GetResult();
        var boundedEviction = boundedResult.Compiled == 3
                              && Directory.GetFiles(
                                  boundedCache.PersistentDirectory!,
                                  "*.v8cache").Length <= 2;

        var grid = new Grid
        {
            RowDefinitions = new RowDefinitions("*,*"),
            ColumnDefinitions = new ColumnDefinitions("*,*")
        };
        var roots = new List<CssLayoutPanel>();
        for (var index = 0; index < 4; index++)
        {
            var root = new CssLayoutPanel { ClipToBounds = true };
            Grid.SetRow(root, index / 2);
            Grid.SetColumn(root, index % 2);
            grid.Children.Add(root);
            roots.Add(root);
        }

        var window = new Window
        {
            Width = 800,
            Height = 600,
            Content = grid
        };
        var hosts = new List<AvaloniaBrowserHost>();
        var runtimes = new List<ClearScriptV8Runtime>();
        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            for (var index = 0; index < roots.Count; index++)
            {
                var root = roots[index];
                var host = new AvaloniaBrowserHost(
                    window,
                    currentHost => new RootedDocument(currentHost, root));
                host.ScriptBaseDirectory = temporaryDirectory;
                hosts.Add(host);
                var runtime = new ClearScriptV8Runtime(
                    host,
                    new ClearScriptV8RuntimeOptions { SharedCache = cache });
                runtimes.Add(runtime);
                runtime.Engine.Script.__webSceneInstanceToken = $"chart-{index + 1}";
                runtime.Execute(SharedScript, "v8-shared-cache-probe.js");
                runtime.ExecuteOwnerScript("./shared-module.js");
            }

            var isolated = runtimes.Select((runtime, index) =>
                    string.Equals(
                        Convert.ToString(runtime.Engine.Evaluate("globalThis.__webSceneSharedCacheToken")),
                        $"chart-{index + 1}",
                        StringComparison.Ordinal)
                    && Convert.ToInt32(runtime.Engine.Evaluate("globalThis.__webSceneSharedCacheExecutions")) == 1
                    && Convert.ToInt32(runtime.Engine.Evaluate("globalThis.__webSceneSharedModuleLoads")) == 1)
                .All(static passed => passed);
            var distinctRoots = hosts.Select(static host => host.Document)
                .Distinct(ReferenceEqualityComparer.Instance)
                .Count() == 4;

            runtimes[0].Dispose();
            hosts[0].Dispose();
            runtimes.RemoveAt(0);
            hosts.RemoveAt(0);
            foreach (var runtime in runtimes)
            {
                runtime.Execute(SharedScript, "v8-shared-cache-probe.js");
            }
            var survivors = runtimes.All(runtime =>
                Convert.ToInt32(runtime.Engine.Evaluate("globalThis.__webSceneSharedCacheExecutions")) == 2
                && Convert.ToInt32(runtime.Engine.Evaluate("globalThis.__webSceneSharedModuleLoads")) == 1);

            var metrics = cache.GetMetrics();
            var sharing = concurrentSingleFlight
                          && warmRestart
                          && sourceInvalidation
                          && corruptionRecovery
                          && compatibilityIsolation
                          && boundedEviction
                          && metrics.SourceMisses == 1
                          && metrics.SourceHits >= 3
                          && metrics.CodeMisses > 0
                          && metrics.CodeHits >= 3
                          && metrics.CodeAccepted + metrics.CodeVerified > 0
                          && metrics.CodeEntries > 0
                          && metrics.CodeBytes > 0;
            var passed = isolated && distinctRoots && survivors && sharing;
            Console.WriteLine(
                $"V8 shared compilation cache: {(passed ? "pass" : "fail")}; " +
                $"isolated={isolated}, roots={distinctRoots}, survivors={survivors}, " +
                $"single-flight={concurrentSingleFlight}, warm={warmRestart}, " +
                $"edited={sourceInvalidation}, corrupt={corruptionRecovery}, " +
                $"compatibility={compatibilityIsolation}, bounded={boundedEviction}, " +
                $"ui-heartbeat={heartbeatCount}, " +
                $"source={metrics.SourceHits} hit/{metrics.SourceMisses} miss, " +
                $"code={metrics.CodeHits} hit/{metrics.CodeMisses} miss, " +
                $"accepted={metrics.CodeAccepted}, verified={metrics.CodeVerified}, " +
                $"updated={metrics.CodeUpdated}, entries={metrics.CodeEntries}, " +
                $"bytes={metrics.CodeBytes}, disk={metrics.DiskHits} hit/" +
                $"{metrics.DiskMisses} miss/{metrics.DiskWrites} write, " +
                $"leaders={metrics.CompilationLeaders}, waiters={metrics.CompilationWaiters}");
            return passed ? 0 : 1;
        }
        finally
        {
            for (var index = runtimes.Count - 1; index >= 0; index--)
            {
                runtimes[index].Dispose();
            }
            for (var index = hosts.Count - 1; index >= 0; index--)
            {
                hosts[index].Dispose();
            }
            window.Close();
            Dispatcher.UIThread.RunJobs();
            Directory.Delete(temporaryDirectory, recursive: true);
        }
    }

    private static int RunProcessWorker(string persistentRoot, bool expectWarm)
    {
        var cache = new ClearScriptV8SharedCache(new ClearScriptV8SharedCacheOptions
        {
            PersistentDirectory = persistentRoot,
            CompatibilityTag = "probe-process-restart-v1",
            MaxPersistentEntries = 32,
            MaxPersistentBytes = 64 * 1024 * 1024
        });
        var sources = Enumerable.Range(0, 3)
            .Select(index => new V8CompilationSource(
                $"v8-process-restart-{index}.js",
                $"function webSceneProcessRestart{index}(value){{return value+{index};}}"))
            .ToArray();
        var result = ClearScriptV8Runtime.PrecompileAsync(
                cache,
                sources,
                includeRuntimeBootstrap: false)
            .GetAwaiter()
            .GetResult();
        var metrics = cache.GetMetrics();
        var passed = result.Requested == 3
                     && result.Compiled + result.Reused == 3
                     && (expectWarm
                         ? result.Compiled == 0
                           && result.Reused == 3
                           && metrics.DiskHits == 3
                           && metrics.CompilationLeaders == 0
                         : metrics.DiskWrites + metrics.DiskHits > 0);
        Console.WriteLine(
            $"V8 persistent process worker: {(passed ? "pass" : "fail")}; " +
            $"mode={(expectWarm ? "warm" : "populate")}, pid={Environment.ProcessId}, " +
            $"compiled={result.Compiled}, reused={result.Reused}, " +
            $"disk={metrics.DiskHits} hit/{metrics.DiskMisses} miss/" +
            $"{metrics.DiskWrites} write, directory='{cache.PersistentDirectory}'");
        return passed ? 0 : 1;
    }

    private static bool TryReadProcessWorker(
        IReadOnlyList<string> args,
        out string persistentRoot,
        out bool expectWarm)
    {
        persistentRoot = string.Empty;
        expectWarm = false;
        for (var index = 0; index < args.Count; index++)
        {
            if (!string.Equals(
                    args[index],
                    "--process-worker",
                    StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            if (index + 1 >= args.Count)
            {
                throw new ArgumentException("--process-worker requires a persistent root path.");
            }
            persistentRoot = args[index + 1];
            expectWarm = index + 2 < args.Count
                         && string.Equals(args[index + 2], "warm", StringComparison.OrdinalIgnoreCase);
            return true;
        }
        return false;
    }

    private static ClearScriptV8SharedCacheOptions CreatePersistentOptions(
        string persistentRoot,
        string compatibilityTag)
        => new()
        {
            PersistentDirectory = persistentRoot,
            CompatibilityTag = compatibilityTag,
            MaxPersistentEntries = 32,
            MaxPersistentBytes = 64 * 1024 * 1024
        };

    private static string CreateHeavyScript()
    {
        var builder = new System.Text.StringBuilder(8 * 1024 * 1024);
        for (var index = 0; index < 100_000; index++)
        {
            builder.Append("function webSceneCacheProbe")
                .Append(index)
                .Append("(value){return value+")
                .Append(index)
                .Append(";}\n");
        }
        return builder.ToString();
    }

    private sealed class RootedDocument : AvaloniaDomDocument
    {
        private readonly Control _root;

        internal RootedDocument(AvaloniaBrowserHost host, Control root)
            : base(host)
        {
            _root = root;
        }

        protected override Control? GetDocumentRoot() => _root;
    }
}
