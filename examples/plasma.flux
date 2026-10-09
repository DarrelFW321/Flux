// Classic plasma: layered sine waves fed through a cosine palette.
uniform time: float;
uniform resolution: vec2;
@range(0.1, 4.0) uniform speed: float = 1.0;
@color uniform tint: vec3 = vec3(0.9, 0.4, 1.0);

const TAU: float = 6.2831853;

// Inigo Quilez's cosine palette.
fn palette(t: float) -> vec3 {
    let a = vec3(0.5, 0.5, 0.5);
    let b = vec3(0.5, 0.5, 0.5);
    let c = vec3(1.0, 1.0, 1.0);
    let d = vec3(0.0, 0.33, 0.67);
    return a + b * cos(TAU * (c * t + d));
}

@fragment
fn main(@builtin(position) frag: vec4) -> vec4 {
    let p = (frag.xy * 2.0 - resolution) / resolution.y;
    let t = time * speed;
    var v = sin(p.x * 3.0 + t);
    v += sin((p.y * 3.0 + t) * 0.5);
    v += sin(length(p * 4.0) - t);
    v += sin(p.x * 2.0 + p.y * 2.0 + t * 1.3);
    let col = palette(v * 0.25 + t * 0.1) * tint;
    return vec4(col, 1.0);
}
