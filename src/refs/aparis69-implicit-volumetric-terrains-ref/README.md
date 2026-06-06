# aparis69-implicit-volumetric-terrains-ref

Референс-порт [aparis69/Implicit-Volumetric-Terrains](https://github.com/aparis69/Implicit-Volumetric-Terrains) — алгоритма из статьи *Terrain Amplification with Implicit 3D Features* (Paris et al., ACM TOG / SIGGRAPH Asia 2019).

По умолчанию запускается **интерактивный viewer** (Sokol + ImGui), в стиле `aparis69-rock-fracturing-ref`. Batch-экспорт OBJ — через флаг `--batch-export`.

## Upstream

- **Repository:** [aparis69/Implicit-Volumetric-Terrains](https://github.com/aparis69/Implicit-Volumetric-Terrains)
- **Paper:** Paris et al., *Terrain Amplification with Implicit 3D Features*, ACM TOG, 2019
- **Pinned commit:** `2e7bb3ee79b8d8ffabdeb0958a55c6171f2791fe` (2022-05-20)

**Paper (PDF):** [docs/reference/papers/paris-2019-terrain-amplification/](../../../docs/reference/papers/paris-2019-terrain-amplification/)

**Port notes:** [docs/reference/ports/aparis69-implicit-volumetric-terrains-ref/](../../../docs/reference/ports/aparis69-implicit-volumetric-terrains-ref/)

## Layout

| Path | Content |
|------|---------|
| `ivt/Include/` | Upstream headers (`Code/Include`) |
| `ivt/Source/` | Upstream implementation (`Code/Source`, без `main.cpp`) |
| `IvtScene.*` | Async rebuild: terrain tree + marching cubes |
| `IvtRenderer.*` / `IvtMeshGpuRenderer.*` | GPU mesh viewport (Y-up) |
| `BatchExport.cpp` | CLI batch OBJ export + smoke test |
| `main.cpp` | Sokol viewer (default) |

## Сборка и запуск

CMake-таргет: `aparis69-implicit-volumetric-terrains-ref`

```bat
cmake --build --preset debug --target aparis69-implicit-volumetric-terrains-ref
```

**Viewer** (без аргументов):

```bat
_intermediate_64\Debug\aparis69-implicit-volumetric-terrains-ref.exe
```

Панель справа: сцена (islands / sea / karst), MC resolution, seed, Regenerate.  
Камера: LMB — orbit, Shift+LMB / MMB / RMB — pan, колесо — zoom.

**Batch export** (как upstream):

```bat
aparis69-implicit-volumetric-terrains-ref.exe --batch-export --scene island
aparis69-implicit-volumetric-terrains-ref.exe --batch-export --scene all
aparis69-implicit-volumetric-terrains-ref.exe --smoke-test
```

| Сцена | OBJ | MC resolution (batch) |
|-------|-----|-----------------------|
| `sea` | `sea.obj` | 350³ |
| `island` | `islands.obj` | 100³ |
| `karst` | `karst.obj` | 200³ |
| `all` | все три | как в upstream `main.cpp` |

`--smoke-test` гоняет все три upstream batch-сцены (отдельный subprocess на сцену) и проверяет `islands.obj`, `karst.obj`, `sea.obj` (>64 байт каждый). Sea@350³ может занять несколько минут.

