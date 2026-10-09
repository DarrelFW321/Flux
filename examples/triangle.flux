// Vertex + fragment stages. The vertex shader builds a triangle from
// @builtin(vertex_index); the playground previews the fragment stage.
uniform time: float;
uniform resolution: vec2;

@vertex
fn vs_main(@builtin(vertex_index) index: int) -> vec4 {
    let angle = float(index) * 2.0943951 + time;
    let p = vec2(cos(angle), sin(angle)) * 0.7;
    return vec4(p, 0.0, 1.0);
}

@fragment
fn main(@builtin(position) frag: vec4) -> vec4 {
    let uv = frag.xy / resolution - 0.5;
    let c = cos(time * 0.5);
    let s = sin(time * 0.5);
    let rot = mat2(c, -s, s, c);
    let q = rot * uv;
    let d = abs(q.x) + abs(q.y);
    let ring = smoothstep(0.02, 0.0, abs(fract(d * 6.0 - time) - 0.5) - 0.2);
    return vec4(vec3(ring) * vec3(0.4, 0.8, 1.0), 1.0);
}
