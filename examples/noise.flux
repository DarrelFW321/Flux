// Fractal Brownian motion: five octaves of value noise.
// The octave loop has a constant trip count, so the optimizer unrolls it.
uniform time: float;
uniform resolution: vec2;

fn hash(p: vec2) -> float {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

fn noise(p: vec2) -> float {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let a = hash(i);
    let b = hash(i + vec2(1, 0));
    let c = hash(i + vec2(0, 1));
    let d = hash(i + vec2(1, 1));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

fn fbm(p: vec2) -> float {
    var sum = 0.0;
    var amp = 0.5;
    var q = p;
    for octave in 0..5 {
        sum += amp * noise(q);
        q = q * 2.0 + vec2(1.7, 9.2);
        amp *= 0.5;
    }
    return sum;
}

@fragment
fn main(@builtin(position) frag: vec4) -> vec4 {
    let uv = frag.xy / resolution.y * 3.0;
    let warp = vec2(fbm(uv + time * 0.1), fbm(uv + vec2(5.2, 1.3) - time * 0.1));
    let v = fbm(uv + warp * 1.5);
    let col = mix(vec3(0.05, 0.1, 0.2), vec3(0.9, 0.7, 0.4), v * v * 1.6);
    return vec4(col, 1.0);
}
