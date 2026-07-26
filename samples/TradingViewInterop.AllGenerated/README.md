# TradingView all-surface generation gate

This project enables every model, outbound proxy, property, method, and inbound
adapter discovered in the shape-only TradingView fixture. It contains no
handwritten C# and exists to catch generator-wide collisions and unsupported
code paths.

Regenerate it with:

```bash
node ../../tooling/htmlml/interop-discover.mjs \
  --declarations ../TradingViewInterop.Generated/TradingViewApi.fixture.d.ts \
  --output TradingViewAllApi.htmlml-interop-api.json \
  --policy-output TradingViewAll.htmlml-interop-policy.json \
  --report-output TradingViewAllApi.coverage.json \
  --namespace TradingViewInterop.AllGenerated \
  --include-all-models \
  --include-all-proxies \
  --include-all-adapters \
  --include-all-functions \
  --include-all-globals \
  --fail-on-fallbacks
```
