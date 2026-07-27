import React, { useEffect, useRef, useState } from 'react';
import { Badge, Card, Metric, SampleHeader, buttonStyle, colors, createSampleLifecycle, gridStyle, rowStyle, shellStyle } from '../../shared/ui';

const activity = ['Package validated', 'Dashboard data refreshed', 'Canvas frame committed', 'Host policy checked'];

function Chart(): React.ReactNode {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const graphics = canvasRef.current?.getContext('2d');
    if (!graphics) return;
    graphics.strokeStyle = colors.primary;
    graphics.lineWidth = 3;
    graphics.beginPath();
    [[8, 88], [64, 62], [118, 72], [174, 34], [230, 46], [286, 18], [344, 30]].forEach(([x, y], index) => index ? graphics.lineTo(x, y) : graphics.moveTo(x, y));
    graphics.stroke();
  }, []);
  return <canvas ref={canvasRef} width={370} height={110} aria-label="Revenue trend chart" />;
}

function App(): React.ReactNode {
  const [loading, setLoading] = useState(true);
  const [overlay, setOverlay] = useState(false);
  useEffect(() => {
    webscene.host.network.invoke('request', { url: 'app://dashboard/data' }).catch(() => undefined);
    const handle = setTimeout(() => setLoading(false), 300);
    return () => clearTimeout(handle);
  }, []);
  return <main style={shellStyle}>
    <SampleHeader eyebrow="React dashboard" title="Operations overview" detail="Responsive cards, a local activity list, Canvas/SVG data visuals, async state and an overlay." />
    <div style={{ ...rowStyle, marginBottom: 14 }}><Badge tone={loading ? 'warning' : 'success'}>{loading ? 'Refreshing…' : 'Live data ready'}</Badge><Badge>DPR {window.devicePixelRatio}</Badge></div>
    <div style={gridStyle}>
      <Metric label="Active components" value="24" trend="+12% this week" />
      <Metric label="Cache reuse" value="96%" trend="18 warm starts" />
      <Metric label="UI budget" value="7.2 ms" trend="Within target" />
    </div>
    <div style={{ ...gridStyle, marginTop: 14 }}>
      <Card style={{ flex: '2 1 380px' }}><h2 style={{ marginTop: 0 }}>Revenue trend</h2><Chart /><button style={buttonStyle} onClick={() => setOverlay(true)}>Inspect chart</button></Card>
      <Card style={{ flex: '1 1 260px' }}><h2 style={{ marginTop: 0 }}>Activity</h2><ul>{activity.map(item => <li key={item} style={{ marginBottom: 8 }}>{item}</li>)}</ul></Card>
    </div>
    {overlay ? <div role="dialog" aria-label="Chart details" style={{ position: 'absolute', inset: 44, padding: 24, color: colors.ink, background: '#ffffff', border: `2px solid ${colors.primary}`, borderRadius: 14 }}><h2>Chart details</h2><p>Seven local points rendered through Canvas 2D at DPR {window.devicePixelRatio}.</p><button style={buttonStyle} onClick={() => setOverlay(false)}>Close</button></div> : null}
  </main>;
}

const lifecycle = createSampleLifecycle('ReactDashboard', () => <App />);
export const mount = lifecycle.mount;
export const unmount = lifecycle.unmount;
Object.assign(globalThis, { mount, unmount });
