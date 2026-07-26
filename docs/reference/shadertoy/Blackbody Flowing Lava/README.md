# Blackbody Flowing Lava

- Ссылка: https://www.shadertoy.com/view/ddVBR3
- Автор: —

Based on: https://www.shadertoy.com/view/MdBSRW и https://www.shadertoy.com/view/sdBGWh.

## Файлы

- `BufferA.glsl` — heightmap лавы: 2D periodic seamless perlin FBM (порт из glm `noise.inl`).
- `BufferB.glsl` — трекинг мыши между кадрами: дистанция и рысканье камеры.
- `Image.glsl` — основной рендер: течение лавы с blackbody-цветом.
