# Avalonia backend authoring sample

This sample references `WebScene.Backend.Abstractions` and
`WebScene.Backend.Avalonia` directly. It mounts the public backend contract, verifies
required capabilities, creates persistent backend nodes, attaches them to the backend
root, and arranges them without involving the compatibility facade.

```sh
dotnet run --project samples/AvaloniaBackendSample -c Release
```
