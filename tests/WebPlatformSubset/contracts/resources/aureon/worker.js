import { count } from './module.js';
self.onmessage = e => {
    e.data[0] += count;
    postMessage(e.data, [e.data.buffer]);
    postMessage({ detached: e.data.byteLength === 0, documentType: typeof document });
};
