using Avalonia.Controls;
using WebScene.Backends.Avalonia.Native;

namespace NativeTradingViewTerminal;

internal static partial class HeadlessProof
{
    private static int CapturePreviewLinesEvidence(NativeWebSceneView view, Window window,
        string output, int width, int height)
    {
        var create = view.EvaluateTextAsync("""
            (() => {
              const f=Array.from(document.querySelectorAll('iframe'))
                .find(f=>f.contentDocument?.querySelectorAll('canvas').length>=8);
              const w=f.contentWindow;
              window.__previewProof='pending';
              Promise.all([324,326,328].map(async (price,index)=>{
                const line=await w.tradingViewApi.activeChart().createOrderLine();
                line.setPrice(price).setText('Diagnostic preview '+index).setQuantity('1')
                  .setTooltip('Inert diagnostic — no order is submitted')
                  .setModifyTooltip('Drag preview').setEditable(true).setCancellable(false)
                  .setLineColor('#00ff00').setBodyTextColor('#00ff00');
                return {price:line.getPrice(),text:line.getText()};
              })).then(lines=>window.__previewProof=JSON.stringify(lines),
                       error=>window.__previewProof='ERROR: '+error.stack);
              return 'started';
            })()
            """);
        PumpUntil(create, TimeSpan.FromSeconds(10));
        PumpFrames(view, window, TimeSpan.FromSeconds(5));
        var state=view.EvaluateTextAsync("window.__previewProof");
        PumpUntil(state, TimeSpan.FromSeconds(10));
        File.WriteAllText(Path.Combine(output,"preview-lines.json"),state.Result);
        SaveNativeFrame((NativeSceneSurface)view.Content!,Path.Combine(output,"preview-lines.png"),width,height);
        return state.Result.StartsWith("[") ? 0 : 1;
    }
}
