const bridge = globalThis.__webSceneHostBridge;

function capabilityClient(capability) {
  return Object.freeze({
    invoke(method, argumentsValue = {}, options = {}) {
      if (!bridge || typeof bridge.invoke !== 'function') return Promise.reject(new Error('WebScene host bridge is unavailable.'));
      const requestId = globalThis.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random()}`;
      const request = JSON.stringify({
        requestId,
        version: '1.0',
        capability,
        method,
        arguments: argumentsValue
      });
      return new Promise((resolve, reject) => {
        if (options.signal?.aborted) return reject(options.signal.reason);
        options.signal?.addEventListener('abort', () => bridge.cancel(requestId), { once: true });
        bridge.invoke(request, value => {
          const response = JSON.parse(value);
          response.ok ? resolve(response.result) : reject(Object.assign(new Error(response.error.message), { code: response.error.code }));
        }, reject);
      });
    }
  });
}

export const webscene = Object.freeze({
  profileVersion: '1.0',
  host: Object.freeze({
    commands: capabilityClient('host.commands'),
    settings: capabilityClient('host.settings'),
    notifications: capabilityClient('host.notifications'),
    network: capabilityClient('host.network'),
    clipboard: capabilityClient('host.clipboard'),
    files: capabilityClient('host.files')
  })
});

globalThis.webscene ??= webscene;
