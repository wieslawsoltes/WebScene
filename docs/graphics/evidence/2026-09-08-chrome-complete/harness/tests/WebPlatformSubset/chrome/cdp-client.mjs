export class CdpClient {
  constructor(socket) {
    this.socket = socket;
    this.nextId = 0;
    this.pending = new Map();
    this.listeners = new Map();
    socket.addEventListener("message", event => this.#receive(JSON.parse(event.data)));
    socket.addEventListener("close", () => this.#rejectPending(new Error("CDP WebSocket closed.")));
    socket.addEventListener("error", () => this.#rejectPending(new Error("CDP WebSocket failed.")));
  }

  static async connect(webSocketUrl) {
    const socket = new WebSocket(webSocketUrl);
    await new Promise((resolve, reject) => {
      socket.addEventListener("open", resolve, { once: true });
      socket.addEventListener("error", reject, { once: true });
    });
    return new CdpClient(socket);
  }

  send(method, params = {}, timeoutMilliseconds = 20_000) {
    const id = ++this.nextId;
    this.socket.send(JSON.stringify({ id, method, params }));
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        if (!this.pending.delete(id)) return;
        reject(new Error(`CDP command '${method}' timed out after ${timeoutMilliseconds} ms.`));
      }, timeoutMilliseconds);
      this.pending.set(id, {
        resolve: value => {
          clearTimeout(timeout);
          resolve(value);
        },
        reject: error => {
          clearTimeout(timeout);
          reject(error);
        }
      });
    });
  }

  on(method, listener) {
    const listeners = this.listeners.get(method) ?? [];
    listeners.push(listener);
    this.listeners.set(method, listeners);
  }

  close() {
    this.#rejectPending(new Error("CDP client closed."));
    this.socket.close();
  }

  #rejectPending(error) {
    for (const continuation of this.pending.values()) continuation.reject(error);
    this.pending.clear();
  }

  #receive(message) {
    if (message.id) {
      const continuation = this.pending.get(message.id);
      if (!continuation) return;
      this.pending.delete(message.id);
      if (message.error) continuation.reject(new Error(JSON.stringify(message.error)));
      else continuation.resolve(message.result);
      return;
    }
    for (const listener of this.listeners.get(message.method) ?? []) {
      Promise.resolve(listener(message.params)).catch(error => {
        process.stderr.write(
          `CDP event listener failed for ${message.method}: ${error.stack ?? error}\n`);
      });
    }
  }
}
