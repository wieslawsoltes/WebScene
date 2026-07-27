import React, { useEffect, useState } from 'react';
import { Badge, Card, SampleHeader, buttonStyle, colors, createSampleLifecycle, inputStyle, type MountContext, rowStyle, shellStyle } from '../../shared/ui';

function Island({ context }: { context: MountContext }): React.ReactNode {
  const [count, setCount] = useState(0);
  const [setting, setSetting] = useState('Loading native setting…');
  useEffect(() => {
    webscene.host.settings.invoke('get', { key: 'accent' })
      .then(() => setSetting('Native setting received'))
      .catch(() => setSetting('Native setting unavailable'));
  }, []);
  const increment = () => {
    const next = count + 1;
    setCount(next);
    webscene.host.commands.invoke('counterChanged', { count: next, instanceId: context.instanceId });
  };
  return <main style={shellStyle}>
    <SampleHeader eyebrow="React island" title="Independent component root" detail="The catalog mounts this package twice. State and focus remain local to each V8/React lifetime." />
    <Card>
      <div style={rowStyle}>
        <Badge tone="success">Isolated state</Badge>
        <Badge>{setting}</Badge>
      </div>
      <p style={{ color: colors.muted }}>Instance: {context.instanceId ?? 'pending'}</p>
      <div style={rowStyle}>
        <button style={buttonStyle} onClick={increment}>Island count: {count}</button>
        <input style={inputStyle} aria-label="Island focus field" placeholder="Focus stays in this island" onFocus={() => webscene.host.commands.invoke('focusEntered', { instanceId: context.instanceId })} />
      </div>
    </Card>
  </main>;
}

const lifecycle = createSampleLifecycle('Hybrid.ReactIslands', context => <Island context={context} />);
export const mount = lifecycle.mount;
export const unmount = lifecycle.unmount;
Object.assign(globalThis, { mount, unmount });
