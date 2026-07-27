# Native TradingView Terminal

This sample loads the hosted
`https://trading-terminal.tradingview-widget.com/` JavaScript application
directly in `NativeWebSceneView`. Its browser-facing `WebSocket` API is backed by
the portable C++ socket transport inside the WebScene native runtime; no .NET
WebSocket callback is involved.

Run the desktop sample:

```bash
dotnet run --project samples/NativeTradingViewTerminal \
  -- --native-library /absolute/path/to/libwebscene_native_engine.dylib
```

Generate repeatable headless evidence (JSON plus a PNG):

```bash
dotnet run --project samples/NativeTradingViewTerminal -c Release -- \
  --headless-proof \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --output artifacts/native-tradingview-terminal
```

The hosted terminal delegates its data connection to a separately navigated
TradingView iframe. The proof runs that iframe in its own native V8 realm and
observes the `WebSocket` created organically by TradingView's datafeed code; it
does not inject or open a synthetic test socket. It fails unless the widget
renders a substantial native scene, the socket opens and receives live data,
and the captured PNG contains real visual variation. The runtime does not call
a .NET WebSocket implementation.
