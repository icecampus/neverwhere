# Material atlas generators (experimental)

Эта папка — **экспериментальная** версия генератора, которая пытается уйти от плоских заливок к “материалам”:

- композитинг tileable текстур (surface/side);
- мягкие края (alpha falloff) + шум;
- декали по краю;
- режим стенок `soil/rock/mixed`.

Важно: это отдельная ветка от `../technical` и может меняться/ломаться.

## Скрипты

- `generate_material_textures_material.py`: процедурно генерит базовые tileable материалы (без художника).
- `generate_atlas_ridge_material.py`: material-генератор Ridge.
- `generate_atlas_valley_material.py`: material-генератор Valley.

## Быстрый старт

1) Сгенерировать материалы:

```bash
python utils/asset_generator/material/generate_material_textures_material.py --out-dir resources/assets/landscape/_materials --size 1024 --seed 1337
```

2) Сгенерировать material-атлас (пример конфигурации):

```bash
python utils/asset_generator/material/generate_atlas_ridge_material.py resources/assets/landscape/TechnicalGrassRidge/atlas_material.png --config utils/asset_generator/material/material_style_example.json --repo-root .
```

## Документация

- ComfyUI рецепты: `COMFYUI_WORKFLOWS.md`
- Пример конфигурации: `material_style_example.json`

