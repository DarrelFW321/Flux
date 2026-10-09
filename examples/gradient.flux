// Hello, shader: a time-animated colour gradient.
uniform time: float;
uniform resolution: vec2;

@fragment
fn main(@builtin(position) frag: vec4) -> vec4 {
    let uv = frag.xy / resolution;
    let wave = 0.5 + 0.5 * cos(time + uv.xyx * 3.0 + vec3(0, 2, 4));
    return vec4(wave, 1.0);
}
