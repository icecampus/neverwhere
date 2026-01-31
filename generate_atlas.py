from PIL import Image, ImageDraw

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

    for i in range(COLS * ROWS):
        col, row = i % COLS, i // COLS
        x, y = col * TILE_W, row * TILE_H
        cy_base = y + 110 
        
        pts_base = [
            (x + 10, cy_base),       # 0: Left
            (x + 128, cy_base - 54), # 1: Up
            (x + 246, cy_base),      # 2: Right
            (x + 128, cy_base + 54)  # 3: Down
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
            draw.polygon(pts_surface, fill=grass_color, outline=grass_outline)
            for j in range(4):
                nj = (j + 1) % 4
                if m[j] and m[nj]:
                    draw.line([pts_top[j], pts_top[nj]], fill=grass_outline, width=2)

        # Текст
        draw.text((x + 15, y + 10), f"ID: {i}", fill=(200, 200, 200, 255))
        draw.text((x + 15, y + 25), t_type, fill=(100, 255, 100, 255))

    img.save('technical_atlas.png')
    print("Previous stable version of technical atlas saved.")

if __name__ == "__main__":
    generate_png_atlas()
