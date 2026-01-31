import math
from PIL import Image, ImageDraw

def normalize(v):
    l = math.sqrt(v[0]**2 + v[1]**2 + v[2]**2)
    if l == 0: return (0, 0, 1)
    return (v[0]/l, v[1]/l, v[2]/l)

def sub(a, b):
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])

def cross(a, b):
    return (a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0])

def dot(a, b):
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]

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

def generate_png_atlas():
    COLS, ROWS = 4, 6
    TILE_W = 256
    TILE_H = 220 
    
    WIDTH, HEIGHT = COLS * TILE_W, ROWS * TILE_H
    
    # Высота подъема
    ELEVATION = 26 
    
    masks = {
        'Full': [1, 1, 1, 1],
        'DownLack': [1, 1, 1, 0],
        'LeftLack': [0, 1, 1, 1],
        'UpLack': [1, 0, 1, 1],
        'RightLack': [1, 1, 0, 1],
        'UpCorner': [0, 1, 0, 0],
        'RightCorner': [0, 0, 1, 0],
        'DownCorner': [0, 0, 0, 1],
        'LeftCorner': [1, 0, 0, 0],
        'RightUpLine': [0, 1, 1, 0],
        'RightDownLine': [0, 0, 1, 1],
        'LeftDownLine': [1, 0, 0, 1],
        'LeftUpLine': [1, 1, 0, 0],
        'UpAndDownCorners': [0, 1, 0, 1],
        'LeftRightCorners': [1, 0, 1, 0],
        'Unknown': [0, 0, 0, 0]
    }

    index_to_type = {i: 'Full' for i in range(4)}
    mappings = {
        4: 'DownLack', 5: 'LeftLack', 6: 'UpLack', 7: 'RightLack',
        8: 'UpCorner', 9: 'RightCorner', 10: 'DownCorner', 11: 'LeftCorner',
        12: 'RightUpLine', 13: 'RightDownLine', 14: 'LeftDownLine', 15: 'LeftUpLine',
        16: 'RightUpLine', 17: 'RightDownLine', 18: 'LeftDownLine', 19: 'LeftUpLine',
        20: 'UpAndDownCorners', 21: 'LeftRightCorners', 22: 'Unknown', 23: 'Unknown'
    }
    index_to_type.update(mappings)

    # Прозрачный фон
    img = Image.new('RGBA', (WIDTH, HEIGHT), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    grass_color = (34, 177, 76, 255)
    grass_outline = (20, 100, 30, 255)
    soil_color = (120, 90, 60, 255)
    soil_dark = (90, 65, 45, 255)

    # 3D coords for lighting calculation
    # Center (0,0). Left(-1,0), Up(0,1), Right(1,0), Down(0,-1). Z up.
    coords_2d_logical = [
        (-1, 0), # 0: Left
        (0, 1),  # 1: Up
        (1, 0),  # 2: Right
        (0, -1)  # 3: Down
    ]
    Z_SCALE = 0.3
    # Light direction: Top-Left (Left-Up)
    # (-1, 1, 1) roughly points from Top-Left-Up towards Origin
    light_dir = normalize((-1, 1, 1))

    for i in range(COLS * ROWS):
        col, row = i % COLS, i // COLS
        x, y = col * TILE_W, row * TILE_H
        cy_base = y + 110 
        
        # Grid mathematics: 
        # Cell size in staggered isometry is 2:1 ratio (width:height).
        # To match the grid perfectly and avoid gaps ("brown stripes"), 
        # the base diamond must be exactly 256x128 (since TILE_W=256).
        
        pts_base = [
            (x + 0,   cy_base),       # 0: Left
            (x + 128, cy_base - 64),  # 1: Up
            (x + 256, cy_base),       # 2: Right
            (x + 128, cy_base + 64)   # 3: Down
        ]
        
        pts_top = [(p[0], p[1] - ELEVATION) for p in pts_base]
        
        t_type = index_to_type.get(i, 'Unknown')
        m = masks[t_type]
        pts_surface = [pts_top[j] if m[j] else pts_base[j] for j in range(4)]
        
        # Основание
        draw.polygon(pts_base, outline=(230, 230, 230, 100))

        # Стенки почвы
        for j in [1, 0, 2, 3]: 
            nj = (j + 1) % 4
            if m[j] or m[nj]:
                side_color = soil_dark if j < 2 else soil_color
                if m[j] and m[nj]:
                    draw.polygon([pts_base[j], pts_base[nj], pts_top[nj], pts_top[j]], fill=side_color)
                elif m[j]:
                    draw.polygon([pts_base[j], pts_base[nj], pts_top[j]], fill=side_color)
                elif m[nj]:
                    draw.polygon([pts_base[j], pts_base[nj], pts_top[nj]], fill=side_color)

        # Поверхность
        if any(m):
            # Calculate 3D points for lighting
            pts_3d = []
            for k in range(4):
                z = Z_SCALE if m[k] else 0
                pts_3d.append((coords_2d_logical[k][0], coords_2d_logical[k][1], z))

            # Split into 2 triangles
            if t_type in ['UpCorner', 'DownCorner']:
                # Split along Left-Right (0-2)
                tris = [(0, 1, 2), (0, 2, 3)]
            else:
                # Default: Split along Up-Down (1-3)
                tris = [(0, 1, 3), (1, 2, 3)]

            for (i1, i2, i3) in tris:
                col = get_lighting_color(grass_color, pts_3d[i1], pts_3d[i2], pts_3d[i3], light_dir)
                poly = [pts_surface[i1], pts_surface[i2], pts_surface[i3]]
                draw.polygon(poly, fill=col)
            
            # Outline for the whole shape
            draw.polygon(pts_surface, outline=grass_outline)
            
            for j in range(4):
                nj = (j + 1) % 4
                if m[j] and m[nj]:
                    draw.line([pts_top[j], pts_top[nj]], fill=grass_outline, width=2)

        # Текст
        # Center of the tile slot
        tx = x + 128
        ty = y + 110
        
        # Red color for visibility against green
        text_color = (255, 0, 0, 255)
        
        # Use anchor='mm' (middle-middle) to center text
        draw.text((tx, ty - 8), f"ID: {i}", fill=text_color, anchor="mm")
        draw.text((tx, ty + 8), t_type, fill=text_color, anchor="mm")

    img.save('technical_atlas.png')
    print("Technical atlas with lighting saved.")

if __name__ == "__main__":
    generate_png_atlas()
