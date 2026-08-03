# Общий бинарный кеш vcpkg (BlackBox9)

На домашнем сервере BlackBox9 поднят shared binary cache для vcpkg
([http binary source](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching)).
При попадании в кеш пакет не собирается локально, а скачивается готовый:
весь closure (~165 пакетов, включая Qt и CGAL) поднимается за ~1–2 минуты
вместо 1–2 часов сборки. Проверено end-to-end 2026-08-03 на x64-linux:
`Restored 165 package(s) from HTTP servers in 1.1 min`, 100% ABI-hit,
сборка проекта и 117/117 юнит-тестов на восстановленном дереве — зелёные.

Сервер — «тупое» файловое хранилище (HEAD = lookup, GET = restore, PUT = store),
вся логика на стороне vcpkg. Источник истины по серверной части: репозиторий
`truenas`, `vcpkg-cache/README.md`.

## Как это подключено в репозитории

Все configure-пресеты в `CMakePresets.json` уже содержат binary source:

```
VCPKG_BINARY_SOURCES=clear;http,https://cache.blackbox9.cc:9443/{name}/{version}/{triplet}/{sha},readwrite,$env{NEVERWHERE_VCPKG_CACHE_AUTH}
```

URL, формат пути и режим `readwrite` заданы централизованно (в git), а
per-machine остаётся **одна переменная окружения** с заголовком авторизации —
так секрет не попадает в репозиторий. `clear` отключает локальный дефолтный
кеш (`~/.cache/vcpkg/archives`), поэтому архивы не дублируются на диске.

URL в пресетах — внешний (`cache.blackbox9.cc:9443`), чтобы кеш был одинаково
доступен и из LAN, и снаружи: у всех пользователей репозитория один endpoint,
различия — только в их ключах.

## Per-machine настройка (единственный шаг)

1. Собрать base64 из `user:пароль` (пароль — в менеджере паролей владельца):

   ```powershell
   # Windows (PowerShell):
   [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('neuro:<пароль>'))
   ```

   ```bash
   # Linux/macOS (printf, НЕ echo — лишний \n сломает хеш и даст 401/403):
   printf '%s' 'neuro:<пароль>' | base64
   ```

2. Экспортировать переменную:

   ```bat
   :: Windows (постоянно, действует в новых терминалах):
   setx NEVERWHERE_VCPKG_CACHE_AUTH "Authorization: Basic <base64>"
   ```

   ```bash
   # Linux/macOS (добавить в ~/.bashrc или ~/.profile):
   export NEVERWHERE_VCPKG_CACHE_AUTH="Authorization: Basic <base64>"
   ```

3. Всё. Следующий `cmake --preset ...` будет читать кеш, а всё, что vcpkg
   соберёт локально, — пушить в него.

Переменная не установлена — конфигурация не падает: чтение из кеша работает и
без авторизации (GET открыт, см. ниже), но push не пройдёт — машина сможет
потреблять кеш, но не пополнять его.

## Endpoint'ы и учётки (серверная сторона)

| Откуда | URL | Авторизация |
|---|---|---|
| LAN (192.168.1.0/24) | `http://192.168.1.201:8088` | не нужна, полный доступ |
| Снаружи | `https://cache.blackbox9.cc:9443` | basic auth на PUT (writers); GET открыт |

Учётки: `neuro` — writer (владелец), `nickptrv` — reader+writer.
**Пароли — в менеджере паролей владельца, в репозиторий их не коммитим.**

Замечание про GET: проверено 2026-08-03 — внешний endpoint отдаёт объекты без
заголовка `Authorization` (200, не 401). Оговорка: запросы шли из LAN, сервер
мог счесть их «своими» по source IP, поэтому поведение для реально внешнего
клиента не гарантировано — при необходимости закрытого кеша проверить конфиг
nginx на сервере. Практический вывод: reader-учётки фактически не нужны,
ключ требуется только для записи.

## Условия попадания в кеш

Ключ записи — ABI-хеш: совпадают компилятор, триплет, фичи и версии портов.
`builtin-baseline` в `vcpkg.json` не запинен, поэтому версии портов
определяются коммитом сабмодуля vcpkg — пока у всех он один, кеш общий.
Переменная окружения одна на машину и действует на все триплеты
(`x64-windows`, `x64-linux`, `arm64-osx`, `wasm32-emscripten`) одновременно.

## Наполнение кеша новым триплетом

Ничего специального: собрать проект с выставленной переменной — всё, что
vcpkg построил, он же и запушил (`readwrite`). Контроль — новые записи на
дашборде.

Если пакеты этого триплета уже собраны на машине ранее (лежат в
`~/.cache/vcpkg/archives` со времён до `clear`), их можно залить разово без
пересборки: для каждой записи `Status: install ok installed` из
`<builddir>/vcpkg_installed/vcpkg/status` взять поле `Abi`, найти архив
`~/.cache/vcpkg/archives/<abi[:2]>/<abi>.zip` и сделать PUT на
`https://cache.blackbox9.cc:9443/<Package>/<Version>/<Architecture>/<Abi>`
(тело — zip как есть, заголовок — из `NEVERWHERE_VCPKG_CACHE_AUTH`).
Так 2026-08-03 был посеян триплет x64-linux: 165 объектов (~3.3 ГБ) за
несколько минут, restore затем подтвердил 100% совпадение ABI.

## Проверка

- Попадание: в логе конфигурации `Restored N package(s) from HTTP servers in ...`
  вместо строк `Building <port>...`.
- Промах: пакет собирается локально и (при `readwrite` + валидном ключе) пушится.
- Дашборд со статистикой: http://192.168.1.201:8089 (из LAN), JSON — `/api/stats`.
- `401`/`403` на PUT = нет/неверный `Authorization` (см. заметку про `echo`
  выше) или учётка не writer.
