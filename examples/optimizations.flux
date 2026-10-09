// A tour of the optimizer. Open the IR tab and step through the passes.
uniform time: float;
uniform resolution: vec2;

const SCALE: float = 2.0 * 0.5;        // const-fold: becomes 1.0
const STRIPES: int = 4;

fn square(x: float) -> float {          // inline: single-exit helper
    return x * x;
}

@fragment
fn main(@builtin(position) frag: vec4) -> vec4 {
    let uv = frag.xy / resolution * SCALE; // simplify: x * 1.0 → x
    let r = pow(uv.x, 2.0);                // simplify: pow(x, 2) → x * x
    let g = square(uv.y) + 0.0;            // inline + simplify
    let wave = sin(time) * sin(time);      // cse: sin(time) computed once

    var acc = 0.0;
    for i in 0..STRIPES {                  // unroll: constant trip count
        acc += step(float(i) / 4.0, uv.x) * 0.25;
    }

    var debug = false;                     // forward + branch-fold: never true
    if debug {
        return vec4(1.0, 0.0, 1.0, 1.0);
    }
    let unused = cos(uv.x) * 3.0;          // dce: never used
    return vec4(r, g, acc * wave, 1.0);
}
