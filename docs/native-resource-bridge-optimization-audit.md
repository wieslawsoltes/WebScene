# Native resource bridge optimization audit

## Scope

This audit targets the synchronous native/managed text-resource envelope used by the
Avalonia backend. The control is `origin/main` at `aad1d8d`. The candidate:

- writes response metadata and UTF-8 content directly into native-owned memory;
- retains only a small pending descriptor when the destination is too short;
- supplies 64 KiB of speculative native capacity on the first callback and preserves
  the exact-size retry for larger resources; and
- replays captured resources from their already-resident UTF-8 bytes instead of
  decoding to UTF-16 and immediately encoding back to UTF-8.

The ordinary file, data, asset, mounted-directory, and HTTP paths retain the existing
`IWebSceneResourceLoader.LoadText` contract. Direct UTF-8 replay is an internal
Avalonia archive fast path and does not alter the public resource API.

## Focused measurements

Measurements were captured on an Apple Silicon development host with Release builds.
Each row is the median of seven or eleven samples from `native-resource-bridge`.

| Scenario | Control | Candidate | Change |
| --- | ---: | ---: | ---: |
| 32 KiB required-size probe + copy | 4.384 us, 65,952 B | 1.509 us, 192 B | 65.6% faster, 99.7% less allocation |
| 32 KiB speculative copy | 4.075 us, 65,832 B | 1.436 us, 0 B | 64.8% faster, allocation eliminated |
| 128 KiB required-size probe + copy | 27.006 us, 262,560 B | 6.035 us, 200 B | 77.7% faster, 99.9% less allocation |
| 2.36 MiB TradingView library, decoded-text bridge | 2.221 ms, 4,726,680 B | n/a | compatibility-path reference |
| 2.36 MiB TradingView library, direct UTF-8 bridge | n/a | 0.206 ms, 496 B | 90.7% faster than decoded-text candidate path |

The original archive path also allocated the decoded UTF-16 string before the two
payload-sized envelope arrays represented by the control rows. The deterministic
TradingView archive contains 9.32 MiB of text, including 8.43 MiB of scripts, so the
candidate removes tens of megabytes of transient managed allocation from a cold replay.

Reproduce the large-script comparison with:

```bash
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-resource-bridge \
  --payload-bytes 32768 --iterations 100 --samples 7 \
  --archive /path/to/tradingview-archive \
  --url https://trading-terminal.tradingview-widget.com/charting_library/bundles/library.22d868b063ce4cfe35ab.js \
  --archive-iterations 5
```

## End-to-end interpretation

Five interleaved fresh-cache TradingView pairs remained dominated by host and page
scheduling variance. The candidate median was 959.6 ms and the control median was
942.0 ms in the final series; paired differences ranged from -175.5 ms to +64.2 ms.
An earlier series moved in the opposite direction. These runs do not establish a
chart-ready latency improvement or regression.

The optimization is accepted for its isolated transfer-speed result, removal of
payload-sized allocations, strict replay correctness, and unchanged public API. Future
chart-ready decisions must continue to use interleaved repeated processes rather than
single-run timing.

## Validation

- Release solution build succeeds for all target frameworks.
- The complete managed solution test matrix passes, including 122 Avalonia backend
  tests on both .NET 8 and .NET 10.
- The certification native build succeeds and all four native test executables pass.
- Deterministic TradingView replay reaches its readiness gate without archive misses.
