// The Mandelbrot set with smooth iteration colouring.
uniform time: float;
uniform resolution: vec2;
@range(16, 256) uniform iterations: float = 128.0;

@fragment
fn main(@builtin(position) frag: vec4) -> vec4 {
    let zoom = 1.6 + 0.9 * sin(time * 0.15);
    let uv = (frag.xy * 2.0 - resolution) / resolution.y;
    let c = vec2(-0.745, 0.186) + uv * pow(0.1, zoom);

    var z = vec2(0.0, 0.0);
    var n = 0.0;
    let limit = int(iterations);
    for i in 0..limit {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        if dot(z, z) > 256.0 { break; }
        n += 1.0;
    }
    if n >= iterations {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    let smooth_n = n - log2(log2(dot(z, z))) + 4.0;
    let col = 0.5 + 0.5 * cos(3.0 + smooth_n * 0.15 + vec3(0.0, 0.6, 1.0));
    return vec4(col, 1.0);
}
