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

## Новые правила (workflow)

- **Временные файлы** и любые промежуточные результаты живут в `_intermediate_64/_asset_generator/...` (папка уже в `.gitignore`).
- Каждый запуск `generate_atlas_*_material.py` **по умолчанию**:
  - пишет результат в `_intermediate_64/_asset_generator/...`
  - публикует его в **новый asset pack**:
    - `resources/assets/landscape/MaterialGrassRidge`
    - `resources/assets/landscape/MaterialGrassValley`
  - создаёт `index.json` автоматически (UUID генерится один раз при первом publish)
  - и **перегенерирует** `thumbnail.png` из `tileIndex=0` (верхний левый тайл 4x6).
- Если нужно получить только временный файл без публикации — используйте `--no-publish`.

## Примеры

1) Сгенерировать материалы в temp-root:

```bash
python utils/asset_generator/material/generate_material_textures_material.py --out-dir _intermediate_64/_asset_generator/material/materials/20260204-1200_seed1337 --size 1024 --seed 1337
```

2) Сгенерировать material-атлас и опубликовать в `resources/assets/landscape/MaterialGrassRidge`:

```bash
python utils/asset_generator/material/generate_atlas_ridge_material.py --run-id 20260204-1200_seed1337 --materials-dir _intermediate_64/_asset_generator/material/materials/20260204-1200_seed1337
```

## Документация

- ComfyUI рецепты: `COMFYUI_WORKFLOWS.md`
- Пример конфигурации: `material_style_example.json`

