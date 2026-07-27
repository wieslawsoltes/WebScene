// WebScene Component Profile 1. This intentionally does not claim all of lib.dom.
interface AbortSignal extends EventTarget { readonly aborted: boolean; readonly reason: unknown; }
interface Event { readonly type: string; readonly target: EventTarget | null; preventDefault(): void; stopPropagation(): void; }
interface EventTarget { addEventListener(type: string, listener: (event: Event) => void): void; removeEventListener(type: string, listener: (event: Event) => void): void; dispatchEvent(event: Event): boolean; }
interface Node extends EventTarget { readonly parentNode: Node | null; readonly childNodes: ArrayLike<Node>; appendChild<T extends Node>(node: T): T; removeChild<T extends Node>(node: T): T; }
interface Element extends Node { id: string; className: string; textContent: string | null; readonly children: ArrayLike<Element>; setAttribute(name: string, value: string): void; getAttribute(name: string): string | null; querySelector(selectors: string): Element | null; querySelectorAll(selectors: string): ArrayLike<Element>; }
interface HTMLElement extends Element { style: Record<string, string>; focus(): void; click(): void; getBoundingClientRect(): DOMRect; }
interface HTMLCanvasElement extends HTMLElement { width: number; height: number; getContext(contextId: '2d'): CanvasRenderingContext2D | null; }
interface SVGElement extends Element {}
interface DOMRect { readonly x: number; readonly y: number; readonly width: number; readonly height: number; readonly top: number; readonly right: number; readonly bottom: number; readonly left: number; }
interface CanvasRenderingContext2D { fillStyle: string; strokeStyle: string; lineWidth: number; font: string; beginPath(): void; moveTo(x: number, y: number): void; lineTo(x: number, y: number): void; arc(x: number, y: number, radius: number, startAngle: number, endAngle: number): void; fill(): void; stroke(): void; fillRect(x: number, y: number, width: number, height: number): void; clearRect(x: number, y: number, width: number, height: number): void; fillText(text: string, x: number, y: number): void; measureText(text: string): { width: number }; save(): void; restore(): void; translate(x: number, y: number): void; scale(x: number, y: number): void; rotate(angle: number): void; }
interface Document extends Node { readonly body: HTMLElement; readonly documentElement: HTMLElement; createElement(tagName: 'canvas'): HTMLCanvasElement; createElement(tagName: string): HTMLElement; createElementNS(namespace: string, qualifiedName: string): SVGElement; getElementById(id: string): HTMLElement | null; querySelector(selectors: string): Element | null; }
interface Window extends EventTarget { readonly document: Document; readonly devicePixelRatio: number; requestAnimationFrame(callback: (time: number) => void): number; cancelAnimationFrame(handle: number): void; setTimeout(handler: () => void, timeout?: number): number; clearTimeout(handle: number): void; }
interface ResizeObserver { observe(target: Element): void; unobserve(target: Element): void; disconnect(): void; }
declare var ResizeObserver: { prototype: ResizeObserver; new(callback: (entries: ReadonlyArray<{ target: Element; contentRect: DOMRect }>) => void): ResizeObserver; };
interface MutationObserver { observe(target: Node, options?: { childList?: boolean; attributes?: boolean; subtree?: boolean }): void; disconnect(): void; takeRecords(): ReadonlyArray<unknown>; }
declare var MutationObserver: { prototype: MutationObserver; new(callback: (records: ReadonlyArray<unknown>) => void): MutationObserver; };
declare var document: Document;
declare var window: Window;
declare function requestAnimationFrame(callback: (time: number) => void): number;
declare function cancelAnimationFrame(handle: number): void;
declare function setTimeout(handler: () => void, timeout?: number): number;
declare function clearTimeout(handle: number): void;
