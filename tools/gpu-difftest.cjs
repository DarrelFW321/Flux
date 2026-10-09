// GPU differential test for the optimizer.
//
// Renders every shader twice on a real GPU through WebGPU — compiled with -O0
// and with the full pass pipeline — using identical uniforms, then compares
// the pixels. Any difference means a pass changed the program's meaning.
//
// Usage (from the repo root, with the playground built and served):
//   cd frontend && npm run build && npx vite preview --port 4173 &
//   node tools/gpu-difftest.cjs http://localhost:4173/ examples tests/shaders
//
// Needs `playwright-core` (a devDependency of the frontend) and a local
// Chrome with WebGPU.
const path = require('path');
const fs = require('fs');
const { chromium } = require(require.resolve('playwright-core', { paths: [path.join(__dirname, '../frontend')] }));

const url = process.argv[2] || 'http://localhost:4173/';
const dirs = process.argv.slice(3).length ? process.argv.slice(3) : ['examples'];

// Fixed values for the uniforms the examples and tests use.
const FIXED = {
  time: [1.3], resolution: [128, 96], mouse: [100, 40, 0, 0], frame: [7],
  count: [5], xform: [1, 0.2, 0, 0, 1, 0.1, 0.3, 0, 1],
};

async function renderBoth({ src, fixed }) {
  const m = await FluxModule();
  const adapter = await navigator.gpu.requestAdapter();
  const device = await adapter.requestDevice();
  const W = 128, H = 96;   // bytesPerRow must be a multiple of 256
  const vs = device.createShaderModule({ code: `
    struct O { @builtin(position) pos: vec4f, @location(0) uv: vec2f }
    @vertex fn vs(@builtin(vertex_index) i: u32) -> O {
      var p = array<vec2f, 3>(vec2f(-1.0, -3.0), vec2f(-1.0, 1.0), vec2f(3.0, 1.0));
      var o: O; o.pos = vec4f(p[i], 0.0, 1.0); o.uv = p[i] * 0.5 + 0.5; return o;
    }` });

  const images = [], insts = [];
  for (const opts of ['O0', '']) {
    const r = JSON.parse(m.compile(src, opts));
    if (!r.ok) return { error: r.diagnostics.map(d => `${d.line}:${d.col} ${d.message}`).join('; ') };
    insts.push(r.ir.instsAfter);
    const module = device.createShaderModule({ code: r.wgsl.code });
    const errs = (await module.getCompilationInfo()).messages.filter(x => x.type === 'error');
    if (errs.length) return { error: `WGSL (${opts || 'O2'}): ` + errs.map(e => `${e.lineNum}: ${e.message}`).join('; ') };

    device.pushErrorScope('validation');
    const entry = r.reflection.entries.find(e => e.stage === 'fragment').name;
    const pipeline = device.createRenderPipeline({
      layout: 'auto', vertex: { module: vs, entryPoint: 'vs' },
      fragment: { module, entryPoint: entry, targets: [{ format: 'rgba8unorm' }] },
    });
    const size = Math.max(16, r.reflection.uniformSize);
    const bytes = new ArrayBuffer(size), f32 = new Float32Array(bytes), i32 = new Int32Array(bytes);
    for (const u of r.reflection.uniforms) {
      const v = fixed[u.name] ?? u.default, base = u.offset / 4;
      if (u.type === 'int') i32[base] = v[0];
      else if (/^mat/.test(u.type)) {
        const n = Number(u.type[3]);
        for (let c = 0; c < n; c++) for (let k = 0; k < n; k++) f32[base + c * 4 + k] = v[c * n + k];
      } else v.forEach((x, k) => { f32[base + k] = x; });
    }
    const ubo = device.createBuffer({ size, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    device.queue.writeBuffer(ubo, 0, bytes);
    const tex = device.createTexture({ size: [W, H], format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
    const out = device.createBuffer({ size: W * H * 4, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const enc = device.createCommandEncoder();
    const pass = enc.beginRenderPass({ colorAttachments: [{ view: tex.createView(), loadOp: 'clear', storeOp: 'store', clearValue: [1, 0, 1, 1] }] });
    pass.setPipeline(pipeline);
    if (r.reflection.uniforms.length)
      pass.setBindGroup(0, device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: ubo } }] }));
    pass.draw(3);
    pass.end();
    enc.copyTextureToBuffer({ texture: tex }, { buffer: out, bytesPerRow: W * 4 }, [W, H]);
    device.queue.submit([enc.finish()]);
    const verr = await device.popErrorScope();
    if (verr) return { error: verr.message };
    await out.mapAsync(GPUMapMode.READ);
    images.push(new Uint8Array(out.getMappedRange().slice(0)));
  }

  const [a, b] = images;
  let max = 0, differing = 0;
  const colors = new Set();
  for (let i = 0; i < a.length; i += 4) {
    const d = Math.max(Math.abs(a[i] - b[i]), Math.abs(a[i + 1] - b[i + 1]), Math.abs(a[i + 2] - b[i + 2]));
    max = Math.max(max, d);
    if (d) differing++;
    colors.add((a[i] << 16) | (a[i + 1] << 8) | a[i + 2]);
  }
  return { max, differing, colors: colors.size, insts };
}

(async () => {
  const browser = await chromium.launch({ channel: 'chrome', headless: true, args: ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage();
  await page.goto(url);
  await page.waitForFunction(() => typeof FluxModule === 'function', null, { timeout: 30000 });

  let failed = 0;
  for (const dir of dirs) {
    for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.flux'))) {
      const r = await page.evaluate(renderBoth, { src: fs.readFileSync(path.join(dir, f), 'utf8'), fixed: FIXED });
      const ok = !r.error && r.max === 0 && r.colors > 1;
      if (!ok) failed++;
      const detail = r.error ?? `${r.differing} differing px (max Δ ${r.max}), ${r.colors} colours, insts -O0 ${r.insts[0]} → ${r.insts[1]}`;
      console.log(`${ok ? 'PASS' : 'FAIL'}  ${path.join(dir, f).padEnd(34)} ${detail}`);
    }
  }
  await browser.close();
  process.exit(failed ? 1 : 0);
})();
