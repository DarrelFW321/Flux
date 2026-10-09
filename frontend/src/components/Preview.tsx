import { useCallback, useEffect, useRef, useState } from 'react';
import type { UniformInfo } from '../lib/compiler';
import { AUTO_UNIFORMS, Renderer, type ShaderError, type UniformValues } from '../lib/gpu';

interface PreviewProps {
  wgsl: string | null;
  entry: string | null;
  uniforms: UniformInfo[];
  uniformSize: number;
  onShaderErrors: (errors: ShaderError[]) => void;
}

const isAuto = (u: UniformInfo) => u.name in AUTO_UNIFORMS;

export function Preview({ wgsl, entry, uniforms, uniformSize, onShaderErrors }: PreviewProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const rendererRef = useRef<Renderer | null>(null);
  const [gpuError, setGpuError] = useState<string | null>(null);
  const [ready, setReady] = useState(false);
  const [playing, setPlaying] = useState(true);
  const [fps, setFps] = useState(0);
  const [size, setSize] = useState<[number, number]>([0, 0]);
  const [custom, setCustom] = useState<Record<string, number[]>>({});

  const timeRef = useRef(0);
  const frameRef = useRef(0);
  const mouseRef = useRef<[number, number, number, number]>([0, 0, 0, 0]);
  const playingRef = useRef(true);
  const customRef = useRef(custom);
  const uniformsRef = useRef(uniforms);
  playingRef.current = playing;
  customRef.current = custom;
  uniformsRef.current = uniforms;

  // ── Device setup ───────────────────────────────────────────────────────
  useEffect(() => {
    let cancelled = false;
    const canvas = canvasRef.current!;
    Renderer.create(canvas)
      .then(r => { if (!cancelled) { rendererRef.current = r; setReady(true); } })
      .catch(e => setGpuError(String(e.message ?? e)));
    return () => { cancelled = true; };
  }, []);

  // Keep the drawing buffer matched to the element's size.
  useEffect(() => {
    const canvas = canvasRef.current!;
    const ro = new ResizeObserver(() => {
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      const w = Math.max(1, Math.round(canvas.clientWidth * dpr));
      const h = Math.max(1, Math.round(canvas.clientHeight * dpr));
      canvas.width = w;
      canvas.height = h;
      setSize([w, h]);
    });
    ro.observe(canvas);
    return () => ro.disconnect();
  }, []);

  // ── Shader swaps ───────────────────────────────────────────────────────
  useEffect(() => {
    const r = rendererRef.current;
    if (!ready || !r || !wgsl || !entry) return;
    let stale = false;
    r.setShader(wgsl, entry, uniforms, uniformSize).then(errs => { if (!stale) onShaderErrors(errs); });
    return () => { stale = true; };
  }, [ready, wgsl, entry, uniforms, uniformSize, onShaderErrors]);

  // Seed controls for new uniforms; keep values the user already tuned.
  useEffect(() => {
    setCustom(prev => {
      const next: Record<string, number[]> = {};
      for (const u of uniforms) {
        if (isAuto(u)) continue;
        const old = prev[u.name];
        next[u.name] = old && old.length === u.default.length ? old : [...u.default];
      }
      return next;
    });
  }, [uniforms]);

  // ── Render loop ────────────────────────────────────────────────────────
  useEffect(() => {
    if (!ready) return;
    let raf = 0;
    let last = performance.now();
    let acc = 0, frames = 0;
    const tick = (now: number) => {
      const dt = (now - last) / 1000;
      last = now;
      if (playingRef.current) timeRef.current += dt;
      acc += dt;
      frames++;
      if (acc > 0.5) { setFps(Math.round(frames / acc)); acc = 0; frames = 0; }

      const canvas = canvasRef.current!;
      const values: UniformValues = { ...customRef.current };
      for (const u of uniformsRef.current) {
        if (u.name === 'time') values.time = [timeRef.current];
        else if (u.name === 'resolution') values.resolution = [canvas.width, canvas.height];
        else if (u.name === 'mouse') values.mouse = [...mouseRef.current];
        else if (u.name === 'frame') values.frame = [frameRef.current];
      }
      if (playingRef.current || frames % 4 === 0) {
        rendererRef.current?.frame(values);
        if (playingRef.current) frameRef.current++;
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [ready]);

  const toPixels = useCallback((e: React.PointerEvent) => {
    const c = canvasRef.current!;
    const r = c.getBoundingClientRect();
    return [(e.clientX - r.left) * (c.width / r.width), (e.clientY - r.top) * (c.height / r.height)];
  }, []);

  const custom_uniforms = uniforms.filter(u => !isAuto(u));

  return (
    <div className="preview">
      <div className="preview-stage">
        <canvas
          ref={canvasRef}
          className="preview-canvas"
          onPointerMove={e => {
            const [x, y] = toPixels(e);
            mouseRef.current = [x, y, mouseRef.current[2], mouseRef.current[3]];
          }}
          onPointerDown={e => {
            const [x, y] = toPixels(e);
            mouseRef.current = [x, y, x, y];
          }}
        />
        {gpuError && (
          <div className="preview-overlay">
            <strong>Preview unavailable</strong>
            <span>{gpuError}.</span>
            <span className="dim">Try a recent Chrome, Edge or Safari. The compiler views still work.</span>
          </div>
        )}
        {!gpuError && !wgsl && <div className="preview-overlay subtle">Fix the errors to update the preview</div>}
        <div className="preview-hud">
          <button className="hud-btn" onClick={() => setPlaying(p => !p)} aria-label={playing ? 'Pause' : 'Play'}>
            {playing ? (
              <svg viewBox="0 0 16 16" width="12" height="12"><rect x="3" y="2" width="3.5" height="12" rx="1" fill="currentColor" /><rect x="9.5" y="2" width="3.5" height="12" rx="1" fill="currentColor" /></svg>
            ) : (
              <svg viewBox="0 0 16 16" width="12" height="12"><path d="M4 2.5v11l9.5-5.5z" fill="currentColor" /></svg>
            )}
          </button>
          <button className="hud-btn" onClick={() => { timeRef.current = 0; frameRef.current = 0; }} aria-label="Restart time">
            <svg viewBox="0 0 16 16" width="12" height="12"><path d="M3 8a5 5 0 1 0 1.6-3.7M3 2.5V5h2.5" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" /></svg>
          </button>
          <span className="hud-stat">{fps} fps</span>
          <span className="hud-stat dim">{size[0]}×{size[1]}</span>
        </div>
      </div>

      {custom_uniforms.length > 0 && (
        <div className="uniform-panel">
          <div className="uniform-title">Uniforms</div>
          {custom_uniforms.map(u => (
            <UniformControl
              key={u.name}
              u={u}
              value={custom[u.name] ?? u.default}
              onChange={v => setCustom(c => ({ ...c, [u.name]: v }))}
            />
          ))}
        </div>
      )}
    </div>
  );
}

function toHex(v: number[]) {
  return '#' + v.slice(0, 3).map(x => Math.round(Math.min(Math.max(x, 0), 1) * 255).toString(16).padStart(2, '0')).join('');
}

function fromHex(h: string, prev: number[]) {
  const c = [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16) / 255);
  return prev.length === 4 ? [...c, prev[3]] : c;
}

function UniformControl({ u, value, onChange }: { u: UniformInfo; value: number[]; onChange: (v: number[]) => void }) {
  if (u.color) {
    return (
      <label className="uniform-row">
        <span className="uniform-name">{u.name}</span>
        <span className="uniform-type">{u.type}</span>
        <input type="color" className="uniform-color" value={toHex(value)} onChange={e => onChange(fromHex(e.target.value, value))} />
      </label>
    );
  }
  if (u.type === 'float') {
    const [lo, hi] = u.range ?? [0, Math.max(1, Math.abs(u.default[0]) * 2)];
    return (
      <label className="uniform-row">
        <span className="uniform-name">{u.name}</span>
        <input
          type="range" min={lo} max={hi} step={(hi - lo) / 500} value={value[0]}
          onChange={e => onChange([Number(e.target.value)])}
          className="uniform-slider"
        />
        <span className="uniform-value">{value[0].toFixed(2)}</span>
      </label>
    );
  }
  return (
    <div className="uniform-row">
      <span className="uniform-name">{u.name}</span>
      <span className="uniform-type">{u.type}</span>
      <span className="uniform-vec">
        {value.map((x, i) => (
          <input
            key={i} type="number" step={u.type === 'int' ? 1 : 0.1} value={x}
            onChange={e => { const v = [...value]; v[i] = Number(e.target.value); onChange(v); }}
          />
        ))}
      </span>
    </div>
  );
}
