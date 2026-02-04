import os
import runpy
import sys

def sub(a, b):
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])

def cross(a, b):
    return (a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0])

def dot(a, b):
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]

def triangle_brightness(p1, p2, p3, light_dir):
    v1 = sub(p2, p1)
    v2 = sub(p3, p1)
    n = cross(v1, v2)
    if n[2] < 0:
        n = (-n[0], -n[1], -n[2])
    n = normalize(n)
    intensity = dot(n, light_dir)
    bright = 0.3 + 1.4 * intensity
    bright = max(0.2, min(2.5, bright))
    return bright

def get_lighting_color(base_color, p1, p2, p3, light_dir):
    v1 = sub(p2, p1)
    v2 = sub(p3, p1)
    # Normal
    n = cross(v1, v2)
    # Ensure pointing up (Z > 0)
    if n[2] < 0:
        n = (-n[0], -n[1], -n[2])
    n = normalize(n)
    
    intensity = dot(n, light_dir)
    # Lighting model: Ambient + Diffuse
    # Even higher contrast: Ambient 0.3, Diffuse 1.4
    bright = 0.3 + 1.4 * intensity
    bright = max(0.2, min(2.5, bright)) # Clamp

    def clamp_255(v):
        return max(0, min(255, int(v)))

    return (
        clamp_255(base_color[0] * bright),
        clamp_255(base_color[1] * bright),
        clamp_255(base_color[2] * bright),
        base_color[3]
    )

if __name__ == "__main__":
    # Compatibility wrapper: keep old entrypoint working.
    script = os.path.join(os.path.dirname(__file__), "technical", "generate_atlas_valley.py")
    sys.argv[0] = script
    runpy.run_path(script, run_name="__main__")
