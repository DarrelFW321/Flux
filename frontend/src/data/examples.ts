import hello from '@examples/hello.fl?raw';
import loops from '@examples/loops.fl?raw';
import arrays from '@examples/arrays.fl?raw';
import functions from '@examples/functions.fl?raw';
import controlFlow from '@examples/control_flow.fl?raw';
import optimizations from '@examples/optimizations.fl?raw';
import neuron from '@examples/neuron.fl?raw';

export interface FluxExample {
  id: string;
  title: string;
  description: string;
  tags: string[];
  source: string;
}

export const EXAMPLES: FluxExample[] = [
  {
    id: 'hello',
    title: 'Hello',
    description: 'Scalars, functions, and print.',
    tags: ['basics'],
    source: hello,
  },
  {
    id: 'loops',
    title: 'Loops',
    description: 'while loops and accumulation.',
    tags: ['basics', 'control flow'],
    source: loops,
  },
  {
    id: 'arrays',
    title: 'Arrays',
    description: 'Literals, broadcast, dot, and sum.',
    tags: ['arrays'],
    source: arrays,
  },
  {
    id: 'functions',
    title: 'Functions',
    description: 'Array parameters and returns.',
    tags: ['functions', 'arrays'],
    source: functions,
  },
  {
    id: 'control_flow',
    title: 'Control flow',
    description: 'if / else and per-element logic.',
    tags: ['control flow'],
    source: controlFlow,
  },
  {
    id: 'optimizations',
    title: 'Optimizations',
    description: 'Constants folded in MIR — try the diff tab.',
    tags: ['mir', 'arrays'],
    source: optimizations,
  },
  {
    id: 'neuron',
    title: 'Neuron',
    description: 'dot + bias + ReLU — a tiny ML-style kernel.',
    tags: ['ml', 'functions'],
    source: neuron,
  },
];

export const DEFAULT_EXAMPLE_ID = 'hello';

export function getExampleById(id: string): FluxExample | undefined {
  return EXAMPLES.find(e => e.id === id);
}
