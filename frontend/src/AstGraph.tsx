import { useEffect, useMemo, useRef, useState } from 'react';

// ── Graph node model ─────────────────────────────────────────────────────────

type Category = 'decl' | 'stmt' | 'expr' | 'meta';

interface GraphNode {
  id: string;
  label: string;
  detail?: string;
  category: Category;
  children: { edgeLabel?: string; node: GraphNode }[];
}

interface Positioned {
  id: string;
  label: string;
  detail?: string;
  category: Category;
  x: number;
  y: number;
  width: number;
  children: { edgeLabel?: string; node: Positioned }[];
}

// ── AST JSON → graph ─────────────────────────────────────────────────────────

let _id = 0;
const mk = (
  label: string,
  category: Category,
  detail?: string,
  children: GraphNode['children'] = [],
): GraphNode => ({ id: `n${_id++}`, label, detail, category, children });

function exprNode(e: any): GraphNode {
  if (!e || typeof e !== 'object') return mk('?', 'expr');
  switch (e.kind) {
    case 'IntLit':   return mk('IntLit',   'expr', String(e.value));
    case 'FloatLit': return mk('FloatLit', 'expr', String(e.value));
    case 'BoolLit':  return mk('BoolLit',  'expr', String(e.value));
    case 'Ident':    return mk('Ident',    'expr', e.name);
    case 'Unary':
      return mk('Unary', 'expr', e.op, [
        { edgeLabel: 'operand', node: exprNode(e.operand) },
      ]);
    case 'Binary':
      return mk('Binary', 'expr', e.op, [
        { edgeLabel: 'left',  node: exprNode(e.left)  },
        { edgeLabel: 'right', node: exprNode(e.right) },
      ]);
    case 'Call':
      return mk('Call', 'expr', e.callee + '()',
        (e.args ?? []).map((a: any, i: number) => ({
          edgeLabel: `arg ${i}`,
          node: exprNode(a),
        })));
    case 'ArrayLit': {
      const elts = e.elements ?? [];
      return mk('ArrayLit', 'expr', `[${elts.length}]`,
        elts.map((el: any, i: number) => ({
          edgeLabel: `${i}`,
          node: exprNode(el),
        })));
    }
    case 'Index':
      return mk('Index', 'expr', undefined, [
        { edgeLabel: 'array', node: exprNode(e.array) },
        { edgeLabel: 'index', node: exprNode(e.index) },
      ]);
  }
  return mk(e.kind ?? '?', 'expr');
}

function stmtNode(s: any): GraphNode {
  if (!s || typeof s !== 'object') return mk('?', 'stmt');
  switch (s.kind) {
    case 'Let':
      return mk('Let', 'stmt', `${s.name}: ${s.type}`, [
        { edgeLabel: 'init', node: exprNode(s.init) },
      ]);
    case 'Assign':
      return mk('Assign', 'stmt', s.name, [
        { edgeLabel: 'value', node: exprNode(s.value) },
      ]);
    case 'Return':
      return mk('Return', 'stmt', undefined, [
        { edgeLabel: 'value', node: exprNode(s.value) },
      ]);
    case 'Print':
      return mk('Print', 'stmt', undefined, [
        { edgeLabel: 'value', node: exprNode(s.value) },
      ]);
    case 'If': {
      const c: GraphNode['children'] = [
        { edgeLabel: 'cond', node: exprNode(s.cond) },
        { edgeLabel: 'then', node: blockNode(s.then) },
      ];
      if (s.else) c.push({ edgeLabel: 'else', node: blockNode(s.else) });
      return mk('If', 'stmt', undefined, c);
    }
    case 'While':
      return mk('While', 'stmt', undefined, [
        { edgeLabel: 'cond', node: exprNode(s.cond) },
        { edgeLabel: 'body', node: blockNode(s.body) },
      ]);
    case 'ExprStmt':
      return mk('ExprStmt', 'stmt', undefined, [
        { node: exprNode(s.expr) },
      ]);
    case 'IndexAssign':
      return mk('IndexAssign', 'stmt', undefined, [
        { edgeLabel: 'array', node: exprNode(s.array) },
        { edgeLabel: 'index', node: exprNode(s.index) },
        { edgeLabel: 'value', node: exprNode(s.value) },
      ]);
  }
  return mk(s.kind ?? '?', 'stmt');
}

function blockNode(stmts: any): GraphNode {
  const list = Array.isArray(stmts) ? stmts : [];
  const detail = `${list.length} stmt${list.length === 1 ? '' : 's'}`;
  return mk('Block', 'meta', detail,
    list.map((s) => ({ node: stmtNode(s) })));
}

function topLevelNode(t: any): GraphNode {
  if (t?.kind === 'FnDecl') {
    const params = (t.params ?? [])
      .map((p: any) => `${p.name}: ${p.type}`)
      .join(', ');
    return mk('FnDecl', 'decl', `${t.name}(${params})`, [
      { edgeLabel: `→ ${t.return_type}`, node: blockNode(t.body) },
    ]);
  }
  return stmtNode(t);
}

function buildGraph(ast: any): GraphNode {
  _id = 0;
  if (!ast || typeof ast !== 'object') return mk('Program', 'decl');
  const items = ast.items ?? [];
  return mk('Program', 'decl', `${items.length} item${items.length === 1 ? '' : 's'}`,
    items.map((it: any) => ({ node: topLevelNode(it) })));
}

// ── Tidy-tree layout ─────────────────────────────────────────────────────────

const NODE_W = 156;
const NODE_H = 56;
const X_GAP  = 22;
const Y_GAP  = 56;

function subtreeWidth(n: GraphNode): number {
  if (n.children.length === 0) return NODE_W;
  const w = n.children.reduce(
    (sum, c, i) => sum + subtreeWidth(c.node) + (i > 0 ? X_GAP : 0),
    0,
  );
  return Math.max(NODE_W, w);
}

function layout(n: GraphNode, depth: number, xStart: number): Positioned {
  const tw = subtreeWidth(n);
  if (n.children.length === 0) {
    return {
      ...n,
      x: xStart + tw / 2,
      y: depth * (NODE_H + Y_GAP),
      width: tw,
      children: [],
    };
  }
  const childrenW = n.children.reduce(
    (sum, c, i) => sum + subtreeWidth(c.node) + (i > 0 ? X_GAP : 0),
    0,
  );
  let cx = xStart + (tw - childrenW) / 2;
  const positioned: Positioned['children'] = [];
  for (const c of n.children) {
    const cw = subtreeWidth(c.node);
    positioned.push({
      edgeLabel: c.edgeLabel,
      node: layout(c.node, depth + 1, cx),
    });
    cx += cw + X_GAP;
  }
  return {
    ...n,
    x: xStart + tw / 2,
    y: depth * (NODE_H + Y_GAP),
    width: tw,
    children: positioned,
  };
}

function maxDepth(n: GraphNode): number {
  if (n.children.length === 0) return 1;
  return 1 + Math.max(...n.children.map((c) => maxDepth(c.node)));
}

function truncate(s: string, max: number): string {
  if (s.length <= max) return s;
  return s.slice(0, max - 1) + '…';
}

// ── Component ────────────────────────────────────────────────────────────────

interface AstGraphProps {
  ast: any;
}

export function AstGraph({ ast }: AstGraphProps) {
  const { root, width, height } = useMemo(() => {
    const g = buildGraph(ast);
    const r = layout(g, 0, 0);
    const tw = subtreeWidth(g);
    const td = maxDepth(g);
    return {
      root: r,
      width: tw,
      height: td * (NODE_H + Y_GAP) - Y_GAP,
    };
  }, [ast]);

  const PAD = 40;
  const initialVb = useMemo(
    () => ({ x: -PAD, y: -PAD, w: width + 2 * PAD, h: height + 2 * PAD }),
    [width, height],
  );

  const [vb, setVb] = useState(initialVb);
  const [dragging, setDragging] = useState(false);

  useEffect(() => setVb(initialVb), [initialVb]);

  const svgRef = useRef<SVGSVGElement | null>(null);
  const containerRef = useRef<HTMLDivElement | null>(null);
  const dragRef = useRef({ active: false, sx: 0, sy: 0, vbX: 0, vbY: 0 });

  // Wheel zoom (cursor-centered). Use a non-passive listener so we can preventDefault.
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const svg = svgRef.current;
      if (!svg) return;
      const rect = svg.getBoundingClientRect();
      const px = e.clientX - rect.left;
      const py = e.clientY - rect.top;
      setVb((curr) => {
        const ux = curr.x + (px / rect.width) * curr.w;
        const uy = curr.y + (py / rect.height) * curr.h;
        const zoom = e.deltaY > 0 ? 1.12 : 1 / 1.12;
        const minW = Math.max(width * 0.15, 200);
        const maxW = width * 10;
        const nw = Math.min(Math.max(curr.w * zoom, minW), maxW);
        const ratio = nw / curr.w;
        const nh = curr.h * ratio;
        return {
          x: ux - (ux - curr.x) * ratio,
          y: uy - (uy - curr.y) * ratio,
          w: nw,
          h: nh,
        };
      });
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
  }, [width]);

  const onMouseDown = (e: React.MouseEvent) => {
    dragRef.current = {
      active: true,
      sx: e.clientX,
      sy: e.clientY,
      vbX: vb.x,
      vbY: vb.y,
    };
    setDragging(true);
  };
  const onMouseMove = (e: React.MouseEvent) => {
    if (!dragRef.current.active || !svgRef.current) return;
    const rect = svgRef.current.getBoundingClientRect();
    const dx = ((e.clientX - dragRef.current.sx) * vb.w) / rect.width;
    const dy = ((e.clientY - dragRef.current.sy) * vb.h) / rect.height;
    setVb((curr) => ({
      ...curr,
      x: dragRef.current.vbX - dx,
      y: dragRef.current.vbY - dy,
    }));
  };
  const endDrag = () => {
    dragRef.current.active = false;
    setDragging(false);
  };

  const reset = () => setVb(initialVb);
  const zoom = (factor: number) =>
    setVb((curr) => {
      const nw = curr.w * factor;
      const nh = curr.h * factor;
      return {
        x: curr.x + (curr.w - nw) / 2,
        y: curr.y + (curr.h - nh) / 2,
        w: nw,
        h: nh,
      };
    });

  // Walk tree and emit SVG elements.
  const nodes: JSX.Element[] = [];
  const edges: JSX.Element[] = [];

  const walk = (n: Positioned) => {
    for (const c of n.children) {
      const x1 = n.x;
      const y1 = n.y + NODE_H;
      const x2 = c.node.x;
      const y2 = c.node.y;
      const midY = (y1 + y2) / 2;
      edges.push(
        <path
          key={`e-${n.id}-${c.node.id}`}
          d={`M${x1},${y1} C${x1},${midY} ${x2},${midY} ${x2},${y2}`}
          className="ast-edge"
        />,
      );
      if (c.edgeLabel) {
        const w = c.edgeLabel.length * 6.2 + 12;
        edges.push(
          <g
            key={`l-${n.id}-${c.node.id}`}
            transform={`translate(${(x1 + x2) / 2},${midY})`}
          >
            <rect
              x={-w / 2}
              y={-8}
              width={w}
              height={16}
              rx={3}
              className="ast-edge-label-bg"
            />
            <text className="ast-edge-label" textAnchor="middle" dy={3}>
              {c.edgeLabel}
            </text>
          </g>,
        );
      }
      walk(c.node);
    }
    nodes.push(
      <g
        key={n.id}
        transform={`translate(${n.x - NODE_W / 2},${n.y})`}
        className={`ast-node ast-node-${n.category}`}
      >
        <rect
          width={NODE_W}
          height={NODE_H}
          rx={6}
          className="ast-node-rect"
        />
        <text
          className="ast-node-kind"
          x={NODE_W / 2}
          y={n.detail ? 20 : NODE_H / 2 + 4}
          textAnchor="middle"
        >
          {n.label}
        </text>
        {n.detail && (
          <text
            className="ast-node-detail"
            x={NODE_W / 2}
            y={40}
            textAnchor="middle"
          >
            {truncate(n.detail, 22)}
          </text>
        )}
      </g>,
    );
  };
  walk(root);

  return (
    <div ref={containerRef} className="ast-graph-container">
      <div className="ast-graph-controls">
        <button onClick={() => zoom(1 / 1.2)} title="Zoom in" aria-label="Zoom in">+</button>
        <button onClick={() => zoom(1.2)} title="Zoom out" aria-label="Zoom out">−</button>
        <button onClick={reset} title="Fit to view" aria-label="Fit to view">◇</button>
      </div>
      <svg
        ref={svgRef}
        className={`ast-graph-svg${dragging ? ' dragging' : ''}`}
        viewBox={`${vb.x} ${vb.y} ${vb.w} ${vb.h}`}
        preserveAspectRatio="xMidYMid meet"
        onMouseDown={onMouseDown}
        onMouseMove={onMouseMove}
        onMouseUp={endDrag}
        onMouseLeave={endDrag}
      >
        <g className="ast-edges">{edges}</g>
        <g className="ast-nodes">{nodes}</g>
      </svg>
      <div className="ast-graph-hint">drag to pan · scroll to zoom</div>
    </div>
  );
}
