# План рефактора графического слоя (runtime + editor)

## Зачем
Сейчас `src/libs/graphics` завязан на Qt (`Qt6::Quick`, GL-context через Qt, см. `src/libs/graphics/CMakeLists.txt` и `src/libs/graphics/sokol_impl.cpp`). Это удобно для QtQuick-прототипов (`EcsPlayground`), но мешает:
- собрать **runtime-игру без Qt**;
- переиспользовать один и тот же рендер-код в редакторе и в игре;
- иметь несколько “shell” (QtQuick editor, standalone game) без дублирования пайплайнов/шейдеров.

Цель: получить “слоёный пирог”, как описано в `TECHNICAL_STACK.md`, но с чистым разделением зависимостей.

## Целевое разбиение на библиотеки

### 1) `graphics_core` (без Qt)
**Зависимости**: sokol (`sokol_gfx.h`, `sokol_time.h`, при необходимости `sokol_fetch.h`), math/glm (опционально), *без* Qt.

**Ответственность**:
- создание и хранение GPU-ресурсов (pipelines, buffers, images, samplers);
- рендер-проходы уровня “мир” (спрайты/тайлы/декали/свет);
- общий API для “рисовать в текущий pass” или “рисовать в заданный pass”.

**API-идея (скелет)**:
- `GraphicsCore::init(const sg_desc&)` — без владения swapchain, только `sg_setup`.
- `GraphicsCore::shutdown()`.
- `GraphicsCore::begin_frame(width,height)` / `end_frame()` (опционально, если удобно).
- `GraphicsCore::draw_world(const WorldRenderInput&)`.

### 2) `graphics_shell_sokolapp` (runtime)
**Зависимости**: `sokol_app.h`, `sokol_glue.h`, `graphics_core`, `imgui` (через `util/sokol_imgui.h`).

**Ответственность**:
- окно + main loop (dt/input);
- swapchain/default-pass (`sg_begin_default_pass`/`sg_commit`);
- ImGui overlay;
- прокидывание событий в UI/игру.

Это ровно то, что делает `src/apps/NeverwhereGame` (будущий “production shell”).

### 3) `graphics_shell_qtquick` (editor)
**Зависимости**: QtQuick (`QQuickFramebufferObject`), OpenGL (через Qt), `graphics_core`.

**Ответственность**:
- получение/управление FBO от Qt;
- оборачивание `QOpenGLFramebufferObject::texture()` в `sg_image` + создание `sg_pass`;
- восстановление GL-state после рендера (Qt “грязнит” state);
- взаимодействие по `Project/Unproject` (если нужно для overlay-гизмо).

Внутри можно переиспользовать практики из `src/apps/EcsPlayground/GameView.cpp`:
- lazy-init `sg_setup` только при активном контексте;
- `sg_reset_state_cache()` перед кадром;
- корректная стратегия `sg_commit()` (один раз на кадр на “верхнем” уровне).

## Миграция по шагам (без ломания)
1. **Стабилизировать runtime shell** (`NeverwhereGame`) на sokol_app + ImGui (уже сделано как каркас).
2. Вытащить из текущего `src/libs/graphics/sokol_impl.cpp` минимальный “рендер квадов” в новый `graphics_core`.
3. Переподключить:
   - runtime: `NeverwhereGame` → использует `graphics_core`;
   - editor: `EcsPlayground/GameView` → вместо `Graphics::init()/draw_rects` зовёт `graphics_shell_qtquick` + `graphics_core`.
4. Оставить старый `src/libs/graphics` как “compat layer” на 1-2 итерации (или удалить после перевода).

## Важные техдетали
- **Шейдеры**: лучше перейти на `sokol-shdc` (единый пайплайн шейдеров под D3D11/Metal/GL), иначе runtime на D3D11 и editor на GL начнут расходиться.
- **Синхронизация `sg_commit()`**:
  - runtime: commit в `frame()`;
  - QtQuick: commit в конце рендера FBO-прохода, но ровно один раз на кадр на верхнем уровне.
- **TextureId/ImGui**: sokol util `sokol_imgui.h` должен быть совместим с текущей версией Dear ImGui (особенно `ImTextureID`).

