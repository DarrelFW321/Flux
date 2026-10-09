import { useEffect, useMemo, useRef, useState } from 'react';
import type { AstNode } from './lib/compiler';

// ── Tidy-tree layout ─────────────────────────────────────────────────────────

interface Positioned {
  id: string;
  node: AstNode;
  x: number;
  y: number;
  children: { edge: string; p: Positioned }[];
}

const NODE_W = 150;
const NODE_H = 52;
const X_GAP = 18;
const Y_GAP = 52;

function width(n: AstNode, memo: Map<AstNode, number>): number {
  const m = memo.get(n);
  if (m !== undefined) return m;
  const w = n.children.length === 0
    ? NODE_W
    : Math.max(NODE_W, n.children.reduce((s, c, i) => s + width(c.node, memo) + (i ? X_GAP : 0), 0));
  memo.set(n, w);
  return w;
}

let seq = 0;
function layout(n: AstNode, depth: number, x0: number, memo: Map<AstNode, number>): Positioned {
  const tw = width(n, memo);
  const kidsW = n.children.reduce((s, c, i) => s + width(c.node, memo) + (i ? X_GAP : 0), 0);
  let cx = x0 + (tw - kidsW) / 2;
  const children = n.children.map(c => {
    const p = layout(c.node, depth + 1, cx, memo);
    cx += width(c.node, memo) + X_GAP;
    return { edge: c.edge, p };
  });
  return { id: `n${seq++}`, node: n, x: x0 + tw / 2, y: depth * (NODE_H + Y_GAP), children };
}

function depthOf(n: AstNode): number {
  return 1 + (n.children.length ? Math.max(...n.children.map(c => depthOf(c.node))) : 0);
}

const clip = (s: string, n: number) => (s.length <= n ? s : s.slice(0, n - 1) + '…');

// ── Component ────────────────────────────────────────────────────────────────

interface AstGraphProps {
  ast: AstNode;
  linkedSrc: number | null;
  onHoverSrc: (line: number | null) => void;
  onPickSrc: (line: number) => void;
}

export function AstGraph({ ast, linkedSrc, onHoverSrc, onPickSrc }: AstGraphProps) {
  const { root, w, h } = useMemo(() => {
    seq = 0;
    const memo = new Map<AstNode, number>();
    return { root: layout(ast, 0, 0, memo), w: width(ast, memo), h: depthOf(ast) * (NODE_H + Y_GAP) - Y_GAP };
  }, [ast]);

  const PAD = 40;
  const fit = useMemo(() => ({ x: -PAD, y: -PAD, w: w + 2 * PAD, h: h + 2 * PAD }), [w, h]);
  // Open at a readable zoom centred on the root rather than fitting a huge tree.
  const start = useMemo(() => {
    const vw = Math.min(w + 2 * PAD, 1300);
    return { x: root.x - vw / 2, y: -PAD, w: vw, h: Math.min(h + 2 * PAD, vw * 0.6) };
  }, [root, w, h]);
  const [vb, setVb] = useState(start);
  const [dragging, setDragging] = useState(false);
  const svgRef = useRef<SVGSVGElement | null>(null);
  const boxRef = useRef<HTMLDivElement | null>(null);
  const drag = useRef({ on: false, sx: 0, sy: 0, x: 0, y: 0 });

  // Only refit when the tree's shape changes, so editing doesn't reset the view.
  const shapeKey = `${w}x${h}`;
  useEffect(() => setVb(start), [shapeKey]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    const el = boxRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const svg = svgRef.current;
      if (!svg) return;
      const r = svg.getBoundingClientRect();
      setVb(c => {
        const ux = c.x + ((e.clientX - r.left) / r.width) * c.w;
        const uy = c.y + ((e.clientY - r.top) / r.height) * c.h;
        const z = e.deltaY > 0 ? 1.12 : 1 / 1.12;
        const nw = Math.min(Math.max(c.w * z, 160), Math.max(w * 4, 800));
        const k = nw / c.w;
        return { x: ux - (ux - c.x) * k, y: uy - (uy - c.y) * k, w: nw, h: c.h * k };
      });
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
  }, [w]);

  const nodes: JSX.Element[] = [];
  const edges: JSX.Element[] = [];
  const walk = (p: Positioned) => {
    for (const { edge, p: c } of p.children) {
      const y1 = p.y + NODE_H, y2 = c.y, my = (y1 + y2) / 2;
      edges.push(<path key={`e${c.id}`} className="ast-edge" d={`M${p.x},${y1} C${p.x},${my} ${c.x},${my} ${c.x},${y2}`} />);
      if (edge && !/^\d+$/.test(edge)) {
        const lw = edge.length * 6 + 10;
        edges.push(
          <g key={`l${c.id}`} transform={`translate(${(p.x + c.x) / 2},${my})`}>
            <rect x={-lw / 2} y={-8} width={lw} height={16} rx={8} className="ast-edge-label-bg" />
            <text className="ast-edge-label" textAnchor="middle" dy={3.5}>{edge}</text>
          </g>,
        );
      }
      walk(c);
    }
    const n = p.node;
    const linked = linkedSrc !== null && n.line === linkedSrc;
    nodes.push(
      <g key={p.id} transform={`translate(${p.x - NODE_W / 2},${p.y})`}
        className={`ast-node ast-${n.cat}${linked ? ' linked' : ''}`}
        onMouseEnter={() => onHoverSrc(n.line)} onClick={() => onPickSrc(n.line)}>
        <rect width={NODE_W} height={NODE_H} rx={8} className="ast-node-rect" />
        <text className="ast-node-kind" x={10} y={19}>{n.label}</text>
        {n.type && <text className="ast-node-type" x={NODE_W - 10} y={19} textAnchor="end">{clip(n.type, 8)}</text>}
        {n.detail && <text className="ast-node-detail" x={10} y={39}>{clip(n.detail, 22)}</text>}
      </g>,
    );
  };
  walk(root);

  const zoom = (f: number) => setVb(c => ({ x: c.x + (c.w - c.w * f) / 2, y: c.y + (c.h - c.h * f) / 2, w: c.w * f, h: c.h * f }));

  return (
    <div ref={boxRef} className="ast-graph" onMouseLeave={() => onHoverSrc(null)}>
      <div className="ast-controls">
        <button onClick={() => zoom(1 / 1.25)} aria-label="Zoom in">+</button>
        <button onClick={() => zoom(1.25)} aria-label="Zoom out">−</button>
        <button onClick={() => setVb(fit)} aria-label="Fit whole tree" title="Fit whole tree">⤢</button>
      </div>
      <svg
        ref={svgRef}
        className={`ast-svg${dragging ? ' dragging' : ''}`}
        viewBox={`${vb.x} ${vb.y} ${vb.w} ${vb.h}`}
        onMouseDown={e => { drag.current = { on: true, sx: e.clientX, sy: e.clientY, x: vb.x, y: vb.y }; setDragging(true); }}
        onMouseMove={e => {
          if (!drag.current.on || !svgRef.current) return;
          const r = svgRef.current.getBoundingClientRect();
          const s = Math.max(vb.w / r.width, vb.h / r.height);
          setVb(c => ({ ...c, x: drag.current.x - (e.clientX - drag.current.sx) * s, y: drag.current.y - (e.clientY - drag.current.sy) * s }));
        }}
        onMouseUp={() => { drag.current.on = false; setDragging(false); }}
        onMouseLeave={() => { drag.current.on = false; setDragging(false); }}
      >
        <g>{edges}</g>
        <g>{nodes}</g>
      </svg>
      <div className="ast-hint">drag to pan · scroll to zoom · click a node to jump to its line</div>
    </div>
  );
}
