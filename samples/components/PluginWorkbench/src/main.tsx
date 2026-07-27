import React, { useState } from 'react';
import { Badge, Card, SampleHeader, buttonStyle, colors, createSampleLifecycle, gridStyle, rowStyle, secondaryButtonStyle, shellStyle } from '../../shared/ui';

const plugins = [
  { id: 'inspector', name: 'Layout Inspector', capabilities: ['host.commands'] },
  { id: 'palette', name: 'Theme Palette', capabilities: ['host.settings'] },
  { id: 'files', name: 'File Explorer', capabilities: ['host.files'] }
];

function App(): React.ReactNode {
  const [active, setActive] = useState<string[]>(['inspector']);
  const toggle = (id: string, allowed: boolean) => {
    if (!allowed) return;
    const loaded = active.includes(id);
    setActive(values => loaded ? values.filter(value => value !== id) : [...values, id]);
    webscene.host.commands.invoke(loaded ? 'pluginUnloaded' : 'pluginLoaded', { id });
  };
  return <main style={shellStyle}>
    <SampleHeader eyebrow="Trusted plugin runtime" title="Plugin workbench" detail="Manifest-declared capabilities, isolated UI lifetimes, explicit permission denial and deterministic reload." />
    <div style={gridStyle}>
      {plugins.map(plugin => {
        const denied = plugin.id === 'files';
        const loaded = active.includes(plugin.id);
        return <Card key={plugin.id} style={{ flex: '1 1 230px' }}>
          <div style={rowStyle}><h2 style={{ margin: 0, fontSize: 18 }}>{plugin.name}</h2><Badge tone={denied ? 'danger' : loaded ? 'success' : 'info'}>{denied ? 'Denied' : loaded ? 'Loaded' : 'Stopped'}</Badge></div>
          <p style={{ color: colors.muted }}>Requests: {plugin.capabilities.join(', ')}</p>
          <button disabled={denied} style={denied ? secondaryButtonStyle : buttonStyle} onClick={() => toggle(plugin.id, !denied)}>{denied ? 'Capability unavailable' : loaded ? 'Unload plugin' : 'Load plugin'}</button>
        </Card>;
      })}
    </div>
    <Card style={{ marginTop: 14 }}><h2 style={{ marginTop: 0 }}>Isolation log</h2><p style={{ color: colors.muted }}>{active.length} plugin runtime(s) active. File Explorer remains denied because `host.files` is absent from this package manifest.</p></Card>
  </main>;
}

const lifecycle = createSampleLifecycle('PluginWorkbench', () => <App />);
export const mount = lifecycle.mount;
export const unmount = lifecycle.unmount;
Object.assign(globalThis, { mount, unmount });
