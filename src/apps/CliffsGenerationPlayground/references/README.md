# References — Cliffs Generation Playground

Внешние источники, на которых основан playground, и документы сравнения с upstream.

| Документ | Описание |
|----------|----------|
| [rock-fracturing-upstream.md](./rock-fracturing-upstream.md) | Закреплённый upstream, карта файлов, gap-анализ «оригинал vs наш порт» |
| [upstream.meta.json](./upstream.meta.json) | Машиночитаемые метаданные репозитория (URL, commit, лицензия, ссылки на статью) |

## Upstream (canonical)

- **Repository:** [aparis69/Rock-fracturing](https://github.com/aparis69/Rock-fracturing)
- **Paper:** Paris et al., *Modeling Rocky Scenery using Implicit Blocks*, The Visual Computer, 2020
- **Project page:** https://aparis69.github.io/public_html/projects/paris2020_Blocks.html
- **Pinned commit (analysis baseline):** `b91965b3011ed269cbc3a051b00c9b284aaa2e36` (2022-05-20)

Код алгоритма в Neverwhere лежит в `../rock_fracture/` — порт `Code/Include` + `Code/Source/blocks.cpp`, не git-submodule (чтобы не тащить 80+ MB prebuilt `.obj` и дублировать vcpkg-зависимости).

Для ручного diff с upstream:

```bat
git clone --depth 1 https://github.com/aparis69/Rock-fracturing.git _tmp/Rock-fracturing
```

Сравнивать с `Code/Source/blocks.cpp`, `Code/Source/main.cpp` и заголовками из `Code/Include/`.
