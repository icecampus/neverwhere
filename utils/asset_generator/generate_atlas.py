import math
import sys
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
    
    # [L, LU, U, UR, R, RD, D, DL, C]
    # Indices: L=0, LU=1, U=2, UR=3, R=4, RD=5, D=6, DL=7, C=8
    # Previous code used indices 0,1,2,3 for L,U,R,D
    # Now we must map: L->0, U->2, R->4, D->6 in the mask, but the loop iterates 0..3 for corners.
    # To minimize refactoring, I'll keep the mask format as [L, U, R, D, ...optional others]
    # or better, just use the 9-element mask and pick correctly.
    
    # Mapping for corner indices in the 9-element mask:
    # Corner 0 (Left) -> Mask 0
    # Corner 1 (Up)   -> Mask 2
    # Corner 2 (Right)-> Mask 4
    # Corner 3 (Down) -> Mask 6
    
    masks = {
        'Full':             [1, 1, 1, 1, 1, 1, 1, 1, 1],
        'DownLack':         [1, 1, 1, 1, 1, 0.5, 0, 0.5, 0.5],
        'LeftLack':         [0, 0.5, 1, 1, 1, 1, 1, 0.5, 0.5],
        'UpLack':           [1, 0.5, 0, 0.5, 1, 1, 1, 1, 0.5],
        'RightLack':        [1, 1, 1, 0.5, 0, 0.5, 1, 1, 0.5],
        'UpCorner':         [0, 0.5, 1, 0.5, 0, 0, 0, 0, 0],
        'RightCorner':      [0, 0, 0, 0.5, 1, 0.5, 0, 0, 0],
        'DownCorner':       [0, 0, 0, 0, 0, 0.5, 1, 0.5, 0],
        'LeftCorner':       [1, 0.5, 0, 0, 0, 0, 0, 0.5, 0],
        'RightUpLine':      [0, 0.5, 1, 1, 1, 0.5, 0, 0, 0.5],
        'RightDownLine':    [0, 0, 0, 0.5, 1, 1, 1, 0.5, 0.5],
        'LeftDownLine':     [1, 0.5, 0, 0, 0, 0.5, 1, 1, 0.5],
        'LeftUpLine':       [1, 1, 1, 0.5, 0, 0, 0, 0.5, 0.5],
        'UpAndDownCorners': [0, 0.5, 1, 0.5, 0, 0.5, 1, 0.5, 0.5],
        'LeftRightCorners': [1, 0.5, 0, 0.5, 1, 0.5, 0, 0.5, 0.5],
        'Unknown':          [0, 0, 0, 0, 0, 0, 0, 0, 0]
    }
    
    # Map logic corner index (0..3) to mask index
    c_idx = [0, 2, 4, 6] 
    
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
        (0, -1), # 3: Down
        (0, 0)   # 4: Center
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
        
        # Calculate Center point (geometric center of diamond)
        center_x = (pts_base[0][0] + pts_base[2][0]) / 2
        center_y = (pts_base[1][1] + pts_base[3][1]) / 2
        pt_center_base = (center_x, center_y)
        
        pts_base_all = pts_base + [pt_center_base] # 0..3 corners, 4 center
        
        pts_top = [(p[0], p[1] - ELEVATION) for p in pts_base_all]
        
        t_type = index_to_type.get(i, 'Unknown')
        m = masks.get(t_type, masks['Unknown'])
        
        # Determine heights for points
        # Corners: indices 0,1,2,3 map to mask indices 0,2,4,6
        # Center: index 4 maps to mask index 8
        
        corner_heights = [m[0], m[2], m[4], m[6]]
        center_height = m[8]
        
        heights = corner_heights + [center_height]
        
        pts_surface = []
        for k in range(5): # 0..3 corners, 4 center
            h = heights[k]
            # Interpolate between base and top
            # Usually h is 0 or 1, but can be 0.5
            px = pts_base_all[k][0]
            # y = base_y - elevation * h
            py = pts_base_all[k][1] - ELEVATION * h
            pts_surface.append((px, py))
        
        # Основание (debug)
        # draw.polygon(pts_base, outline=(230, 230, 230, 100))

        # Стенки почвы (только по периметру 0..3)
        for j in [1, 0, 2, 3]: 
            nj = (j + 1) % 4
            h_curr = heights[j]
            h_next = heights[nj]
            
            # Draw wall if at least one corner is raised > 0
            if h_curr > 0 or h_next > 0:
                side_color = soil_dark if j < 2 else soil_color
                
                # Logic: Draw quad defined by surface edge and base edge
                # pts_surface[j], pts_surface[nj], pts_base[nj], pts_base[j]
                
                poly = [pts_base[j], pts_base[nj], pts_surface[nj], pts_surface[j]]
                draw.polygon(poly, fill=side_color)

        # Поверхность
        if any(h > 0 for h in heights):
            # Calculate 3D points for lighting
            pts_3d = []
            for k in range(5):
                z = Z_SCALE * heights[k]
                pts_3d.append((coords_2d_logical[k][0], coords_2d_logical[k][1], z))

            tris = []
            
            # Triangulation Logic
            if t_type in ['UpAndDownCorners', 'LeftRightCorners']:
                # Fan from Center (index 4)
                # 4 triangles: (Center, 0, 1), (Center, 1, 2), (Center, 2, 3), (Center, 3, 0)
                tris = [
                    (4, 0, 1),
                    (4, 1, 2),
                    (4, 2, 3),
                    (4, 3, 0)
                ]
            elif t_type in ['UpCorner', 'DownCorner', 'UpLack', 'DownLack']:
                 # Split along Up-Down (1-3)
                 tris = [(0, 1, 3), (1, 2, 3)]
            elif t_type in ['LeftCorner', 'RightCorner', 'LeftLack', 'RightLack']:
                 # Split along Left-Right (0-2)
                 tris = [(0, 1, 2), (0, 2, 3)]
            else:
                 # Default (Full, Lines, etc) -> Default Split Up-Down
                 tris = [(0, 1, 3), (1, 2, 3)]

            for (i1, i2, i3) in tris:
                # Skip triangle if all vertices are at zero level (optional optimization)
                if heights[i1] == 0 and heights[i2] == 0 and heights[i3] == 0:
                    continue

                col = get_lighting_color(grass_color, pts_3d[i1], pts_3d[i2], pts_3d[i3], light_dir)
                poly = [pts_surface[i1], pts_surface[i2], pts_surface[i3]]
                draw.polygon(poly, fill=col)
            
            # Outline for the whole shape (perimeter)
            for j in range(4):
                nj = (j + 1) % 4
                if heights[j] > 0 and heights[nj] > 0:
                     draw.line([pts_surface[j], pts_surface[nj]], fill=grass_outline, width=2)
                # Also draw outline if one is high and one is low? 
                # Original code: if m[j] and m[nj]. This means both are HIGH.
                # But now we have 0.5.
                # Let's keep drawing line if both endpoints are > 0.
                elif (heights[j] > 0 or heights[nj] > 0):
                    # For Lacks/Corners, we want outline on the "ground" level? No.
                    # Usually outline is only on the "green" part.
                    # Since we draw walls for soil, green outline is on the top edge.
                    # Let's stick to: draw line if edge is part of the "upper" structure.
                    # It's hard to define for 0.5.
                    # Simple heuristic: draw line between surface points.
                    draw.line([pts_surface[j], pts_surface[nj]], fill=grass_outline, width=2)

        # Текст
        # Center of the tile slot
        tx = x + 128
        ty = y + 110
        
        # Red color for visibility against green
        text_color = (255, 0, 0, 255)
        
        # Use anchor='mm' (middle-middle) to center text
        draw.text((tx, ty - 8), f"ID: {i}", fill=text_color, anchor="mm")
        draw.text((tx, ty + 8), t_type, fill=text_color, anchor="mm")

    output_path = 'technical_atlas.png'
    if len(sys.argv) > 1:
        output_path = sys.argv[1]

    img.save(output_path)
    print(f"Technical atlas with lighting saved to {output_path}")

if __name__ == "__main__":
    generate_png_atlas()
