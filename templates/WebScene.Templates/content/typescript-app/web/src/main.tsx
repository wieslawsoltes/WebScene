import React from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { webscene } from '@webscene/sdk';

let root: Root | undefined;
export function mount(): void {
  const element = document.createElement('div');
  element.id = 'app';
  document.body.appendChild(element);
  root = createRoot(element as never);
  root.render(<main><h1>WebScene TypeScript Desktop</h1><p>Component Profile {webscene.profileVersion}</p></main>);
  webscene.host.settings.invoke('get', { key: 'theme' });
}
export function unmount(): void { root?.unmount(); root = undefined; }
Object.assign(globalThis, { mount, unmount });
