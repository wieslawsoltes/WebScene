export type WebSceneCapability =
  | 'dom' | 'css.layout' | 'canvas.2d' | 'svg'
  | 'input.pointer' | 'input.keyboard' | 'input.focus' | 'clipboard'
  | 'host.commands' | 'host.settings' | 'host.notifications'
  | 'host.network' | 'host.clipboard' | 'host.files';

export interface WebSceneInvokeOptions { signal?: AbortSignal; }

export interface WebSceneCapabilityClient {
  invoke<TResult = unknown, TArguments = unknown>(method: string, argumentsValue?: TArguments, options?: WebSceneInvokeOptions): Promise<TResult>;
}

export interface WebSceneHost {
  readonly commands: WebSceneCapabilityClient;
  readonly settings: WebSceneCapabilityClient;
  readonly notifications: WebSceneCapabilityClient;
  readonly network: WebSceneCapabilityClient;
  readonly clipboard: WebSceneCapabilityClient;
  readonly files: WebSceneCapabilityClient;
}

export interface WebSceneRuntime {
  readonly profileVersion: '1.0';
  readonly host: WebSceneHost;
}

export declare const webscene: WebSceneRuntime;

declare global { const webscene: WebSceneRuntime; }
