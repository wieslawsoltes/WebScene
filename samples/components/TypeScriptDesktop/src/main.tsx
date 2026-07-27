import React, { useEffect, useState } from 'react';
import { Badge, Card, SampleHeader, buttonStyle, colors, createSampleLifecycle, inputStyle, rowStyle, secondaryButtonStyle, shellStyle } from '../../shared/ui';

type View = 'home' | 'form' | 'data';

function App(): React.ReactNode {
  const [view, setView] = useState<View>('home');
  const [name, setName] = useState('Ada');
  const [saved, setSaved] = useState(false);
  const [dialogOpen, setDialogOpen] = useState(false);
  const [records, setRecords] = useState<string[]>([]);
  useEffect(() => { webscene.host.settings.invoke('get', { key: 'theme' }); }, []);
  useEffect(() => {
    if (view !== 'data' || records.length) return;
    const handle = setTimeout(() => setRecords(['Quarterly report', 'Design review', 'Launch checklist']), 300);
    return () => clearTimeout(handle);
  }, [view, records.length]);
  const save = () => {
    setSaved(true);
    webscene.host.notifications.invoke('show', { message: `Saved profile for ${name}` });
  };
  return <main style={shellStyle}>
    <SampleHeader eyebrow="TypeScript application" title="Desktop workspace" detail="Routing-shaped local views, typed form state, an overlay dialog, async data and explicit host services." />
    <nav style={{ ...rowStyle, marginBottom: 16 }} aria-label="Application views">
      {(['home', 'form', 'data'] as View[]).map(item => <button key={item} style={view === item ? buttonStyle : secondaryButtonStyle} onClick={() => setView(item)}>{item === 'data' ? 'Async data' : item[0].toUpperCase() + item.slice(1)}</button>)}
    </nav>
    {view === 'home' ? <Card>
      <h2 style={{ marginTop: 0 }}>Welcome, {name}</h2>
      <p style={{ color: colors.muted }}>This entire application surface is owned by the TypeScript component.</p>
      <button style={buttonStyle} onClick={() => setDialogOpen(true)}>Open dialog</button>
    </Card> : null}
    {view === 'form' ? <Card>
      <h2 style={{ marginTop: 0 }}>Profile form</h2>
      <div style={rowStyle}>
        <label htmlFor="profile-name">Name</label>
        <input id="profile-name" style={inputStyle} value={name} onChange={event => setName(event.currentTarget.value)} />
        <button style={buttonStyle} onClick={save}>Save</button>
        {saved ? <Badge tone="success">Saved</Badge> : null}
      </div>
    </Card> : null}
    {view === 'data' ? <Card>
      <h2 style={{ marginTop: 0 }}>Async records</h2>
      {records.length ? <ul>{records.map(record => <li key={record}>{record}</li>)}</ul> : <p style={{ color: colors.muted }}>Loading packaged data…</p>}
    </Card> : null}
    {dialogOpen ? <div role="dialog" aria-modal="true" aria-label="Workspace dialog" style={{ position: 'absolute', inset: 32, padding: 24, color: colors.ink, background: '#ffffff', border: `2px solid ${colors.primary}`, borderRadius: 14 }}>
      <h2>Native-surface overlay</h2>
      <p>This modal is rendered by the component without a browser window.</p>
      <button style={buttonStyle} onClick={() => setDialogOpen(false)}>Close dialog</button>
    </div> : null}
  </main>;
}

const lifecycle = createSampleLifecycle('TypeScriptDesktop', () => <App />);
export const mount = lifecycle.mount;
export const unmount = lifecycle.unmount;
Object.assign(globalThis, { mount, unmount });
