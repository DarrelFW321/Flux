// Type declaration for the Emscripten-generated FluxModule global.
// The script is loaded via <script src="./flux_wasm.js"> in index.html.

interface FluxWasm {
  compile_frontend: (source: string) => string;
}

declare function FluxModule(): Promise<FluxWasm>;
