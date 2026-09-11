# PGG cheatsheet (арт-итерации)

Каталог билтинов **не копировать**: `PggTool docs builtins | rg -i <имя>`, карточка — `pgg_docs("clip")` или `pgg_docs("builtin:clip")` (def загруженного файла побеждает одноимённый builtin). Перед grep по спеке/ядру — эта страница, потом §8.

## Выражения и атрибуты

`sin`/`cos`/`atan2` — **радианы**; градусы → `radians()`. `mix`/`clamp`/`smoothstep`, `vec3()`, касты. `@P`/`@N`/`@index`/`@iteration`/`@tint`/`@Cd`; маска группы — `ingroup("brick")`.

## Идиомы

```
c = ingroup("brick") ? brick_col * @tint : c0
acc = repeat (empty_mesh(), iterations = n) |acc| { acc = merge(acc, piece_k) }
inst = realize(instance_on_points(pts, source = a0, variants = [a0, a1]))
punched = clip(g, origin = o, normal = n, cap_group = "brick")
edged = compute_normals(bevel(box(size), width = w, bevel_group = "edge"), mode = flat)
tube = sweep(mesh_line(count = 2, length = h, dir = (0, 1, 0)), profile = ring)
```

Массив вокруг Y: `shapes.polar_array(g, n = 6)`. Четыре стены ящика: `shapes.around_box(facade, w, d)`. Многоугольник: `shapes.ngon_frustum` — радиусы **апофемы**. Кладка: сердцевина `blockwork.core_prism` + `brick_veneer*` + `masonry.clip_*_hole`. Скруглённый план: `plan.plan_prism` / `plan.arc_shell`.

## Грабли

- Тернарник **не** выбирает `geo` — `select(cond, a, b)` или отдельные def.
- Вертикальный `sweep`: `@profile_scale = (depth, width)`, не `(width, depth)`.
- `parts.cbox` фаска `k` от **min(размер)**; на этаже — `k_abs` или голый `box`.
- Нейтраль merge/repeat: `empty_mesh()` / `empty_points()` (`mesh_line(count = 0)` — точки).
- `orient_from_euler(vec3)` — градусы, GLM `quat(radians)`: pitch X, yaw Y, roll Z.
- Апофема/описанный: `shapes.ngon_circumradius(apothem, sides)`; вынос пояса — **по нормали к грани**.
- Нахлёст черепицы — только вдоль ската (`pv < tl`); отступ по нормали константа (`lift`), иначе верхние ряды отрываются.
- RPC `render` **stateless**, кроме `orbit`/`distance`. Опущенный `wire`/`ortho`/`target`/`chrome` сбрасывается. Форму — с `wire:false`; «вижу не то» — сначала `render_state`.
- Сварка вершин — `merge_by_distance`; `weld` — параметр `mirror`.
- `grid()` → `geo<mesh>`, `instance_on_points` хочет points: якоря — `plan.point_grid` / `mesh_line`+`foreach`. `grid` уже в XZ, нормали +Y.
- Булевых вычитаний меша нет: не строить тело в проёме или резать `clip`/`masonry.clip_*`.
- Имя param def'а = top-level param → E102/E105; top-level с префиксом (`body_w`).
- `parts.piece`/`cbox` дополнительно помечают фаски в `edge`.
- Красить только реальные группы (`stats.groups` / чтение def'а); иначе E305.
- `@tint` points + faces в одном `merge` → E609.
- `bake_ao` гасит эмиссию — `separate` светящихся **до** бейка, `merge` после. Эмиссия ≤ 1.0.
- Длинная труба: прямая + короткое колено + прямая, не одна Безье на всю длину.

## MCP (одна строка)

`pgg_docs("clip")` · `pgg_render(node="house", view="front", wire=False, chrome="off")` · `pgg_render(..., compare="prev"|"baseline", png=False)` · `pgg_contact_sheet(views="*")` · `pgg_measure(node="house", a="group:stone", b="group:brick", axis="x")` · `pgg_probe("house:bbox[group=stone]")` · `pgg_reference(image=..., node="hotel")`

## Обвязка

`PggTool check` — только корневой `.pgg` (нет `--lib`; подмодуль → E501). Черновики под `resources/pgg/`. Аргументы: `pgg_docs(symbol=…)`, `pgg_reference(image=…)`. `render`/`probe` — по **загруженному** файлу. `fmt --check` на закоммиченных примерах может расходиться — канон не enforced.
