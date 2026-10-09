// Exercises less common language features; validated by spirv-val in CTest.
uniform time: float;
uniform resolution: vec2;
uniform xform: mat3;
uniform count: int;
@color uniform tint: vec4 = vec4(1.0, 0.5, 0.25, 1.0);

const AXES = mat2(1.0, 0.0, 0.0, 1.0);

fn pick(v: vec3, i: int) -> float {
    return v[i];
}

fn early(x: float) -> float {
    if x < 0.0 {
        return -1.0;
    }
    var acc = 0;
    var k = 0;
    while k < 10 {
        k += 1;
        if k % 2 == 0 { continue; }
        acc = acc + k * 3 / 2 - abs(-k) % 3;
    }
    return float(acc) * x;
}

@fragment
fn main(@builtin(position) frag: vec4, @builtin(front_facing) front: bool, @location(0) uv: vec2) -> vec4 {
    var m = mat3(2.0);
    m[1] = vec3(0.0, 1.0, 0.0);
    let t = transpose(m) * xform;
    let det = determinant(t);
    var c = tint;
    c.xz = vec2(uv.x, uv.y);
    c.y *= 0.5;
    let flip = front ? c.rgb : c.bgr;
    let r = refract(normalize(vec3(uv, 1.0)), vec3(0, 0, -1), 0.9);
    let q = AXES * uv;
    var s = 0.0;
    for i in 0..count {
        s += pick(flip, i % 3);
    }
    if s > 100.0 {
        discard;
    }
    let mixed = mix(flip, r, clamp(det, 0, 1)) + vec3(q, early(s)) * 0.01;
    let wave = vec3(sin(time), cos(time), step(0.5, uv.x));
    return vec4(max(mixed, wave * 0.1), c.w);
}
