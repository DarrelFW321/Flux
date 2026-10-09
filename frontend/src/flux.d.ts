// The Emscripten glue (public/flux_wasm.js, loaded from index.html) defines
// this global. See lib/compiler.ts for the typed wrapper.

interface FluxWasmModule {
  compile(source: string, options: string): string;
}

declare var FluxModule: (() => Promise<FluxWasmModule>) | undefined;
