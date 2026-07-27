globalThis.mount = function mount(context) {
  const root = document.createElement('div');
  root.id = 'app';
  root.textContent = 'WebScene TypeScript Desktop mounted (' + context.instanceId + ')';
  document.body.appendChild(root);
};
globalThis.unmount = function unmount() {
  const root = document.getElementById('app');
  if (root && root.parentNode) root.parentNode.removeChild(root);
};
