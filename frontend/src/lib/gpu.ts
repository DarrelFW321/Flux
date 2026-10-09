/// <reference types="@webgpu/types" />
import type { UniformInfo } from './compiler';

// The playground supplies the vertex stage: one oversized triangle covering
// the viewport, passing uv in [0,1] (y up) at @location(0).
const FULLSCREEN_VS = /* wgsl */ `
struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
}

@vertex
fn vs(@builtin(vertex_index) i: u32) -> VOut {
  var p = array<vec2f, 3>(vec2f(-1.0, -3.0), vec2f(-1.0, 1.0), vec2f(3.0, 1.0));
  var o: VOut;
  o.pos = vec4f(p[i], 0.0, 1.0);
  o.uv = p[i] * 0.5 + 0.5;
  return o;
}
`;

// Uniform names the playground drives automatically.
export const AUTO_UNIFORMS: Record<string, string> = {
  time: 'seconds since start (float)',
  resolution: 'canvas size in pixels (vec2)',
  mouse: 'xy: pointer position, zw: last click (vec4, pixels)',
  frame: 'frame counter (float or int)',
};

export interface ShaderError {
  message: string;
  line: number;   // WGSL line, 1-based (0 = unknown)
  col: number;
}

export type UniformValues = Record<string, number[]>;

export class Renderer {
  private device!: GPUDevice;
  private context!: GPUCanvasContext;
  private format!: GPUTextureFormat;
  private vsModule!: GPUShaderModule;
  private pipeline: GPURenderPipeline | null = null;
  private bindGroup: GPUBindGroup | null = null;
  private uniformBuffer: GPUBuffer | null = null;
  private uniforms: UniformInfo[] = [];
  private uniformSize = 0;
  private generation = 0;
  lost = false;

  static async create(canvas: HTMLCanvasElement): Promise<Renderer> {
    if (!('gpu' in navigator)) throw new Error('WebGPU is not available in this browser');
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) throw new Error('No WebGPU adapter found');
    const r = new Renderer();
    r.device = await adapter.requestDevice();
    r.device.lost.then(() => { r.lost = true; });
    r.context = canvas.getContext('webgpu') as GPUCanvasContext;
    r.format = navigator.gpu.getPreferredCanvasFormat();
    r.context.configure({ device: r.device, format: r.format, alphaMode: 'opaque' });
    r.vsModule = r.device.createShaderModule({ code: FULLSCREEN_VS, label: 'fullscreen-vs' });
    return r;
  }

  // Compiles `wgsl` and swaps it in. On failure the previous pipeline keeps
  // running and the errors are returned for the diagnostics panel.
  async setShader(wgsl: string, entry: string, uniforms: UniformInfo[], uniformSize: number): Promise<ShaderError[]> {
    const gen = ++this.generation;
    const module = this.device.createShaderModule({ code: wgsl, label: 'flux-fragment' });
    const info = await module.getCompilationInfo();
    const errors: ShaderError[] = info.messages
      .filter(m => m.type === 'error')
      .map(m => ({ message: m.message, line: m.lineNum, col: m.linePos }));
    if (errors.length) return errors;

    this.device.pushErrorScope('validation');
    let pipeline: GPURenderPipeline;
    try {
      pipeline = await this.device.createRenderPipelineAsync({
        layout: 'auto',
        vertex: { module: this.vsModule, entryPoint: 'vs' },
        fragment: { module, entryPoint: entry, targets: [{ format: this.format }] },
        primitive: { topology: 'triangle-list' },
      });
    } catch (e) {
      await this.device.popErrorScope();
      return [{ message: String((e as Error).message ?? e), line: 0, col: 0 }];
    }
    const scopeErr = await this.device.popErrorScope();
    if (scopeErr) return [{ message: scopeErr.message, line: 0, col: 0 }];
    if (gen !== this.generation) return [];   // a newer shader superseded this one

    const size = Math.max(16, uniformSize);
    if (!this.uniformBuffer || this.uniformBuffer.size < size) {
      this.uniformBuffer?.destroy();
      this.uniformBuffer = this.device.createBuffer({ size, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    }
    this.bindGroup = uniforms.length
      ? this.device.createBindGroup({
          layout: pipeline.getBindGroupLayout(0),
          entries: [{ binding: 0, resource: { buffer: this.uniformBuffer } }],
        })
      : null;
    this.pipeline = pipeline;
    this.uniforms = uniforms;
    this.uniformSize = size;
    return [];
  }

  // Packs uniform values at their reflected offsets. Matrices are stored
  // column-major with each column padded to 16 bytes (WGSL / std140 layout).
  private writeUniforms(values: UniformValues) {
    if (!this.uniformBuffer || !this.uniforms.length) return;
    const bytes = new ArrayBuffer(this.uniformSize);
    const f32 = new Float32Array(bytes);
    const i32 = new Int32Array(bytes);
    for (const u of this.uniforms) {
      const v = values[u.name] ?? u.default;
      const base = u.offset / 4;
      if (u.type === 'int') { i32[base] = Math.trunc(v[0] ?? 0); continue; }
      const m = /^mat(\d)$/.exec(u.type);
      if (m) {
        const n = Number(m[1]);
        for (let c = 0; c < n; c++)
          for (let r = 0; r < n; r++) f32[base + c * 4 + r] = v[c * n + r] ?? 0;
        continue;
      }
      for (let k = 0; k < v.length; k++) f32[base + k] = v[k];
    }
    this.device.queue.writeBuffer(this.uniformBuffer, 0, bytes);
  }

  frame(values: UniformValues) {
    if (!this.pipeline || this.lost) return;
    this.writeUniforms(values);
    const encoder = this.device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [{
        view: this.context.getCurrentTexture().createView(),
        loadOp: 'clear',
        storeOp: 'store',
        clearValue: { r: 0, g: 0, b: 0, a: 1 },
      }],
    });
    pass.setPipeline(this.pipeline);
    if (this.bindGroup) pass.setBindGroup(0, this.bindGroup);
    pass.draw(3);
    pass.end();
    this.device.queue.submit([encoder.finish()]);
  }

  get hasPipeline() { return this.pipeline !== null; }
}
