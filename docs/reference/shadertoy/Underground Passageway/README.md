# Underground Passageway

- Ссылка: https://www.shadertoy.com/view/XdcfDf
- Автор: Shane
- Теги: raymarch, tunnel, rock, procedural, buffer, precalc, splatter, voronoi
- Источник: паста пользователя (shadertoy.com недоступен с текущего IP — Cloudflare)

Демонстрация precalc-подхода: скальная высотная карта (splatter скошенных 2D-форм с
overlap — 7×7 cell check, слишком тяжело для реалтайма в distance field) считается один
раз в Buffer A, дальше маппится на туннель как текстура.

## Файлы

- `BufferA.glsl` — генератор высотной карты скал. Self-feedback: читает свой кадр через
  `iChannel0`; пересчёт только при `iFrame<10` или смене resolution (в `.xy` хранится
  разрешение). Выход: `.a` — высотная карта.
- `Image.glsl` — реймарч туннеля: цилиндрический маппинг высотки из буфера на стены,
  песок, вентшахты, спот-лайты. Входы: `iChannel0` — Buffer A; `iChannel1` — детальная
  rock-текстура (какая именно — неизвестно, API недоступен; подобрать с mipmap/repeat).
