import React, { useState } from 'react';
import { Badge, Card, SampleHeader, buttonStyle, colors, createSampleLifecycle, gridStyle, shellStyle } from '../../shared/ui';

const services = [
  ['commands', 'Run command', 'execute', { command: 'refresh' }],
  ['settings', 'Read setting', 'get', { key: 'theme' }],
  ['notifications', 'Notify', 'show', { message: 'Hello from WebScene' }],
  ['network', 'Policy request', 'request', { url: 'app://sample/data' }],
  ['clipboard', 'Copy text', 'writeText', { text: 'WebScene bridge' }],
  ['files', 'Select file', 'pick', { extensions: ['.json'] }]
] as const;

function App(): React.ReactNode {
  const [results, setResults] = useState<Record<string, string>>({});
  const invoke = async (service: typeof services[number]) => {
    const [capability, , method, argumentsValue] = service;
    setResults(values => ({ ...values, [capability]: 'Pending…' }));
    try {
      await webscene.host[capability].invoke(method, argumentsValue);
      setResults(values => ({ ...values, [capability]: 'Completed' }));
    } catch {
      setResults(values => ({ ...values, [capability]: 'Translated error' }));
    }
  };
  return <main style={shellStyle}>
    <SampleHeader eyebrow="Explicit .NET integration" title="Host bridge services" detail="Every boundary is asynchronous, JSON-only and granted by a declared capability." />
    <div style={gridStyle}>
      {services.map(service => <Card key={service[0]} style={{ flex: '1 1 210px' }}>
        <Badge>{`host.${service[0]}`}</Badge>
        <h2 style={{ fontSize: 18 }}>{service[1]}</h2>
        <p style={{ color: colors.muted }}>Method: {service[2]}</p>
        <button style={buttonStyle} onClick={() => invoke(service)}>Invoke service</button>
        <p><Badge tone={results[service[0]] === 'Completed' ? 'success' : results[service[0]] === 'Translated error' ? 'danger' : 'warning'}>{results[service[0]] ?? 'Ready'}</Badge></p>
      </Card>)}
    </div>
    <Card style={{ marginTop: 14 }}><strong>Contract tests:</strong> grant, undeclared denial, cancellation propagation and exception translation run in the SDK test project.</Card>
  </main>;
}

const lifecycle = createSampleLifecycle('HostBridge.Services', () => <App />);
export const mount = lifecycle.mount;
export const unmount = lifecycle.unmount;
Object.assign(globalThis, { mount, unmount });
