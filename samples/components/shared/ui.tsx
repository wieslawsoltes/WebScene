import React, { type CSSProperties, type ReactNode } from 'react';
import { createRoot, type Root } from 'react-dom/client';

export interface MountContext {
  instanceId?: string;
}

export interface SampleLifecycle {
  mount(context?: MountContext): void;
  unmount(): void;
}

export const colors = {
  ink: '#0f172a',
  muted: '#526581',
  surface: '#ffffff',
  canvas: '#eef4fb',
  line: '#cbd5e1',
  primary: '#0369a1',
  primarySoft: '#e0f2fe',
  success: '#047857',
  successSoft: '#d1fae5',
  warning: '#b45309',
  warningSoft: '#fef3c7',
  danger: '#b91c1c',
  dangerSoft: '#fee2e2'
} as const;

export const shellStyle: CSSProperties = {
  boxSizing: 'border-box',
  minHeight: '100%',
  padding: 24,
  color: colors.ink,
  background: colors.canvas,
  fontFamily: 'Inter, system-ui, sans-serif'
};

export const rowStyle: CSSProperties = {
  display: 'flex',
  flexWrap: 'wrap',
  gap: 12,
  alignItems: 'center'
};

export const gridStyle: CSSProperties = {
  display: 'flex',
  flexWrap: 'wrap',
  gap: 14,
  alignItems: 'stretch'
};

export const cardStyle: CSSProperties = {
  boxSizing: 'border-box',
  padding: 16,
  color: colors.ink,
  background: colors.surface,
  border: `1px solid ${colors.line}`,
  borderRadius: 12
};

export const buttonStyle: CSSProperties = {
  padding: '9px 14px',
  color: '#ffffff',
  background: colors.primary,
  border: '0',
  borderRadius: 8,
  fontWeight: 600
};

export const secondaryButtonStyle: CSSProperties = {
  ...buttonStyle,
  color: colors.ink,
  background: colors.primarySoft,
  border: `1px solid ${colors.line}`
};

export const inputStyle: CSSProperties = {
  boxSizing: 'border-box',
  minWidth: 220,
  padding: '9px 11px',
  color: colors.ink,
  background: colors.surface,
  border: `1px solid ${colors.line}`,
  borderRadius: 8
};

export function createSampleLifecycle(
  displayName: string,
  render: (context: MountContext) => ReactNode
): SampleLifecycle {
  let root: Root | undefined;
  let container: HTMLElement | undefined;
  return {
    mount(context = {}) {
      container = document.createElement('div');
      container.id = `webscene-${displayName.toLowerCase().replaceAll(/[^a-z0-9]+/g, '-')}`;
      container.style.minHeight = '100%';
      container.style.color = colors.ink;
      container.style.background = colors.canvas;
      document.body.appendChild(container);
      root = createRoot(container as never);
      root.render(render(context));
    },
    unmount() {
      root?.unmount();
      root = undefined;
      if (container?.parentNode) container.parentNode.removeChild(container);
      container = undefined;
    }
  };
}

export function SampleHeader(props: { eyebrow: string; title: string; detail: string }): ReactNode {
  return <header style={{ marginBottom: 18 }}>
    <div style={{ color: colors.primary, fontSize: 12, fontWeight: 700, letterSpacing: 1.2, textTransform: 'uppercase' }}>{props.eyebrow}</div>
    <h1 style={{ margin: '4px 0 6px', color: colors.ink, fontSize: 26 }}>{props.title}</h1>
    <p style={{ margin: 0, color: colors.muted }}>{props.detail}</p>
  </header>;
}

export function Card(props: { children: ReactNode; style?: CSSProperties }): ReactNode {
  return <section style={{ ...cardStyle, ...props.style }}>{props.children}</section>;
}

export function Badge(props: { children: ReactNode; tone?: 'info' | 'success' | 'warning' | 'danger' }): ReactNode {
  const tone = props.tone ?? 'info';
  const palette = tone === 'success'
    ? [colors.success, colors.successSoft]
    : tone === 'warning'
      ? [colors.warning, colors.warningSoft]
      : tone === 'danger'
        ? [colors.danger, colors.dangerSoft]
        : [colors.primary, colors.primarySoft];
  return <span style={{ padding: '4px 8px', color: palette[0], background: palette[1], borderRadius: 999, fontSize: 12, fontWeight: 700 }}>{props.children}</span>;
}

export function Metric(props: { label: string; value: string; trend?: string }): ReactNode {
  return <Card style={{ minWidth: 155, flex: '1 1 155px' }}>
    <div style={{ color: colors.muted, fontSize: 12 }}>{props.label}</div>
    <div style={{ marginTop: 4, color: colors.ink, fontSize: 24, fontWeight: 700 }}>{props.value}</div>
    {props.trend ? <div style={{ marginTop: 6, color: colors.success, fontSize: 12 }}>{props.trend}</div> : null}
  </Card>;
}
