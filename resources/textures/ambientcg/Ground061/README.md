# Ground061 — ambientCG (CC0)

Источник: https://ambientcg.com/view?id=Ground061 (1K-JPG комплект).
Лицензия: Creative Commons CC0 — свободное использование без атрибуции,
в т.ч. коммерческое (https://ambientcg.com/license).

Файлы (переименованы из `Ground061_1K-JPG_*.jpg`):

- `Ground061_Color.jpg` — альбедо.
- `Ground061_NormalGL.jpg` — нормал-мапа (OpenGL-конвенция, Y+).
- `Ground061_AmbientOcclusion.jpg` — baked AO.
- `Ground061_Roughness.jpg` — шероховатость.
- `Ground061_Displacement.jpg` — карта высот (используется как CPU-рельеф
  в MaskField, см. `maskfield::MaskFieldParams::reliefMap`).

Используется плейграундом `src/landscape_playgrounds/SDFWithMaterialLandscape`
(материал песчаного пляжа для слоя Mask 3D). NormalDX-вариант не включён —
конвейер работает в OpenGL-конвенции нормалей.
