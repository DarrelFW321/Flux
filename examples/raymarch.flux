// Sphere-traced signed distance field: a morphing blob over a floor.
uniform time: float;
uniform resolution: vec2;
uniform mouse: vec4;

const MAX_STEPS: int = 64;
const EPS: float = 0.001;

fn sd_sphere(p: vec3, r: float) -> float {
    return length(p) - r;
}

fn smin(a: float, b: float, k: float) -> float {
    let h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

fn scene(p: vec3) -> float {
    let s1 = sd_sphere(p - vec3(sin(time) * 0.6, 0.0, 0.0), 0.5);
    let s2 = sd_sphere(p - vec3(-sin(time) * 0.6, cos(time * 0.7) * 0.3, 0.0), 0.35);
    let floor_d = p.y + 0.6;
    return min(smin(s1, s2, 0.3), floor_d);
}

fn normal(p: vec3) -> vec3 {
    let e = vec2(EPS, 0.0);
    return normalize(vec3(
        scene(p + e.xyy) - scene(p - e.xyy),
        scene(p + e.yxy) - scene(p - e.yxy),
        scene(p + e.yyx) - scene(p - e.yyx)));
}

@fragment
fn main(@builtin(position) frag: vec4) -> vec4 {
    let uv = (frag.xy * 2.0 - resolution) / resolution.y;
    let yaw = (mouse.x / resolution.x - 0.5) * 2.0;
    let ro = vec3(sin(yaw) * 3.0, 0.4, -cos(yaw) * 3.0);
    let fwd = normalize(-ro);
    let right = normalize(cross(vec3(0, 1, 0), fwd));
    let up = cross(fwd, right);
    let rd = normalize(fwd * 1.5 + right * uv.x - up * uv.y);

    var t = 0.0;
    var hit = false;
    for i in 0..MAX_STEPS {
        let d = scene(ro + rd * t);
        if d < EPS {
            hit = true;
            break;
        }
        t += d;
        if t > 20.0 { break; }
    }

    var col = vec3(0.05, 0.06, 0.09) + 0.05 * uv.y;
    if hit {
        let p = ro + rd * t;
        let n = normal(p);
        let light = normalize(vec3(0.6, 0.8, -0.4));
        let diff = max(dot(n, light), 0.0);
        let rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        col = vec3(0.2, 0.5, 1.0) * diff + vec3(1.0, 0.6, 0.3) * rim * 0.6 + 0.04;
    }
    return vec4(pow(col, vec3(1.0 / 2.2)), 1.0);
}
