import gradient from '@examples/gradient.flux?raw';
import plasma from '@examples/plasma.flux?raw';
import raymarch from '@examples/raymarch.flux?raw';
import mandelbrot from '@examples/mandelbrot.flux?raw';
import noise from '@examples/noise.flux?raw';
import optimizations from '@examples/optimizations.flux?raw';
import triangle from '@examples/triangle.flux?raw';

export interface FluxExample {
  id: string;
  title: string;
  description: string;
  source: string;
}

export const EXAMPLES: FluxExample[] = [
  { id: 'gradient', title: 'Gradient', description: 'Hello world: uniforms, swizzles, broadcasting', source: gradient },
  { id: 'plasma', title: 'Plasma', description: 'Helper functions, consts, @range / @color controls', source: plasma },
  { id: 'raymarch', title: 'Raymarcher', description: 'SDFs, loops with break, inlining + LICM', source: raymarch },
  { id: 'mandelbrot', title: 'Mandelbrot', description: 'Dynamic loop bounds and early exit', source: mandelbrot },
  { id: 'noise', title: 'FBM noise', description: 'Constant-trip loop that the optimizer unrolls', source: noise },
  { id: 'optimizations', title: 'Optimizer tour', description: 'One line per pass — step through the IR', source: optimizations },
  { id: 'triangle', title: 'Two stages', description: '@vertex + @fragment, matrices', source: triangle },
];

export const DEFAULT_EXAMPLE_ID = 'raymarch';
