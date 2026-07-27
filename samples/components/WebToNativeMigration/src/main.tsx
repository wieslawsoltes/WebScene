import React, { useState } from 'react';
import { Badge, Card, SampleHeader, buttonStyle, colors, createSampleLifecycle, inputStyle, rowStyle, shellStyle } from '../../shared/ui';

const orders = [
  { id: 1042, customer: 'Northwind', status: 'Ready' },
  { id: 1048, customer: 'Contoso', status: 'Review' },
  { id: 1051, customer: 'Adventure Works', status: 'Queued' }
];

function App(): React.ReactNode {
  const [selected, setSelected] = useState(orders[0]);
  const select = (order: typeof orders[number]) => {
    setSelected(order);
    webscene.host.commands.invoke('selectionChanged', order);
  };
  return <main style={shellStyle}>
    <SampleHeader eyebrow="Incremental migration" title="Existing React order panel" detail="This compatible component runs beside a native Avalonia editor while sharing selection through a typed host command." />
    <div style={rowStyle}><Badge>React list</Badge><Badge tone="success">Native editor connected</Badge></div>
    <Card style={{ marginTop: 14 }}>
      <h2 style={{ marginTop: 0 }}>Orders</h2>
      {orders.map(order => <button key={order.id} style={{ ...(selected.id === order.id ? buttonStyle : { ...buttonStyle, background: '#ffffff', color: colors.ink, border: `1px solid ${colors.line}` }), margin: 4 }} onClick={() => select(order)}>#{order.id} · {order.customer}</button>)}
      <div style={{ marginTop: 16 }}>
        <label htmlFor="migration-note">Legacy component note</label><br />
        <input id="migration-note" style={{ ...inputStyle, marginTop: 6 }} placeholder={`Note for order ${selected.id}`} onFocus={() => webscene.host.commands.invoke('focusTransfer', { target: 'react-note' })} />
      </div>
    </Card>
  </main>;
}

const lifecycle = createSampleLifecycle('WebToNativeMigration', () => <App />);
export const mount = lifecycle.mount;
export const unmount = lifecycle.unmount;
Object.assign(globalThis, { mount, unmount });
