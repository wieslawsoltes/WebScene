import React, { useEffect, useRef, useState } from 'react';
import { Badge, Card, SampleHeader, buttonStyle, colors, createSampleLifecycle, rowStyle, shellStyle } from '../../shared/ui';

function CanvasPreview(): React.ReactNode {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const canvas = canvasRef.current;
    const graphics = canvas?.getContext('2d');
    if (!canvas || !graphics) return;
    graphics.fillStyle = colors.primary;
    graphics.fillRect(0, 18, 360, 72);
    graphics.fillStyle = '#ffffff';
    graphics.font = '18px sans-serif';
    graphics.fillText('Canvas 2D from JavaScript', 22, 61);
  }, []);
  return <canvas ref={canvasRef} width={420} height={110} aria-label="Blue Canvas demonstration" />;
}

function App(): React.ReactNode {
  const [clicks, setClicks] = useState(0);
  const [timerState, setTimerState] = useState('Timer waiting');
  const startTimer = () => {
    setTimerState('Timer running…');
    setTimeout(() => setTimerState('Timer completed'), 350);
  };
  return <main style={shellStyle}>
    <SampleHeader eyebrow="Component Profile 1" title="Basic component host" detail="DOM, CSS, pointer events, timers, SVG and Canvas projected to Avalonia or Uno—without a WebView." />
    <div style={rowStyle}>
      <Badge tone="success">Mounted</Badge>
      <Badge>Offline package</Badge>
      <Badge>{timerState}</Badge>
    </div>
    <Card style={{ marginTop: 16 }}>
      <h2 style={{ marginTop: 0, color: colors.ink }}>Interactive surface</h2>
      <p style={{ color: colors.muted }}>Each click is handled by React state inside the isolated V8 component.</p>
      <div style={rowStyle}>
        <button style={buttonStyle} onClick={() => setClicks(value => value + 1)}>Clicked {clicks} {clicks === 1 ? 'time' : 'times'}</button>
        <button style={buttonStyle} onClick={startTimer}>Run timer</button>
        <svg width="44" height="44" viewBox="0 0 44 44" aria-label="SVG check mark">
          <circle cx="22" cy="22" r="20" fill={colors.successSoft} />
          <path d="M12 22 L19 29 L33 14" fill="none" stroke={colors.success} strokeWidth="4" />
        </svg>
      </div>
      <CanvasPreview />
    </Card>
  </main>;
}

const lifecycle = createSampleLifecycle('ComponentHost.Basic', () => <App />);
export const mount = lifecycle.mount;
export const unmount = lifecycle.unmount;
Object.assign(globalThis, { mount, unmount });
