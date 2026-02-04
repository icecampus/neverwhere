# ComfyUI рецепты для tileable материалов (casual painted)

Цель: получить **несколько бесшовных (seamless / tileable)** текстур материалов и набор декалей, которые затем композятся в атлас через `utils/asset_generator/material/*material*.py` (поверх масок геометрии тайлов).

## 0) Рекомендуемые базовые настройки (SDXL)

- **Размер**: 1024x1024 (квадрат, tileable)
- **Sampler**: DPM++ 2M Karras (или аналог)
- **Steps**: 25–40
- **CFG**: 4.5–7.0
- **Denoise**: 1.0 (для генерации с нуля), 0.35–0.65 (для inpaint/починки шва)
- **Seed**: фиксируйте, чтобы повторять результат
- **Negative prompt**: `text, watermark, logo, photorealistic, harsh contrast, jpeg artifacts`

## 1) Как сделать бесшовность (3 практичных способа)

### Способ A: Нативный tiling (если в вашей сборке есть узел “seamless / tile”)
Если у вас установлен любой пакет, который даёт узел/опцию **tile X/Y** (названия разные), включите **tile по X и Y** на стадии латента или на стадии декодирования VAE.

Ожидаемый граф (по смыслу):
- CheckpointLoader (SDXL)
- CLIPTextEncode (positive/negative)
- EmptyLatentImage (1024x1024)
- KSampler (tile enabled)
- VAE Decode
- SaveImage

### Способ B: “Offset + Inpaint” (работает почти везде, без спец-узлов)
1) Сгенерируйте обычную текстуру 1024x1024.
2) Сделайте **offset на половину ширины и высоты** (512, 512), чтобы швы оказались в центре.
3) Сделайте **inpaint центральных вертикального и горизонтального шва** (узкой маской 32–96 px).
4) Повторите offset обратно и при необходимости ещё один короткий inpaint.

### Способ C: “Tile Control” (если есть ControlNet Tile / аналог)
Этот способ хорошо держит “рисованный” стиль и детализацию.

## 2) Промпты для материалов (casual painted)

### Grass (поверхность)
Positive:
- `hand-painted grass texture, top-down, stylized casual game, seamless tile, soft color variation, small blades, subtle flowers, clean shapes`

Negative (добавить к общему):
- `realistic photo, deep shadows, strong directional light, 3d render`

### Dirt (поверхность)
Positive:
- `hand-painted soil ground texture, top-down, stylized casual game, seamless tile, subtle pebbles, warm tones, soft variation`

### Soil Side (боковая стенка грунта)
Positive:
- `hand-painted soil cross-section texture, stylized, seamless tile, subtle strata layers, small roots, warm brown palette`

### Rock Side (боковая стенка скалы)
Positive:
- `hand-painted rock cliff texture, stylized casual game, seamless tile, soft cracks, slight moss hints, readable shapes`

## 3) Декали для края (edge decals)

Подходы:
- **batch** (генерить пачку 256x256 и упаковать в 4x4 sheet)
- **inpaint по сетке** (контролировать содержимое каждой ячейки)

## 4) Минимальный набор на старт

- `grass_albedo.png`
- `soil_side.png`
- `rock_side.png` (опционально)
- `edge_decals.png` (4x4)

