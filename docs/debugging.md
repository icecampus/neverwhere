# Отладка падений и рантайм-значений через debug-MCP

Эта методичка описывает, как расследовать краши, ассерты и неверные рантайм-значения
в neverwhere (C++/Qt/QML) через **нативный отладчик**, подключённый как
MCP-сервер `neverwhere-debug`: на Windows — **cdb** (WinDbg), на macOS — **LLDB**
(сервер `neverwhere-debug-macos`). Рассчитана на coding-агентов и людей — что звонить,
в каком порядке, и каких граблей избегать.

## Принцип debugger-first (cdb-first на Windows)

**При падении/ассерте/неверном рантайм-значении сначала доказательство через MCP
(`debug_*`), не фикс по стектрейсу из лога.** Лог только маршрутизирует к гипотезе;
доказывает root cause — инспекция под отладчиком: первый проектный фрейм + локали +
значения выражений в точке сбоя.

Исключения — когда отладчик физически недоступен (W: waiver), например compile/link/parse
ошибка **до** запуска inferior. Тогда чиним по тексту ошибки компилятора.

## Окружение (Windows)

MCP-сервер `neverwhere-debug`:
- **Бэкенд:** `tools/debug_mcp/` (Python, `mcp>=1.0`, см. `tools/debug_mcp/requirements.txt`)
- **Лаунчер:** `tools/run_mcp_server.ps1` (ставит UTF-8, PYTHONPATH, NEVERWHERE_REPO_ROOT)
- **Подключение:** `.mcp.json` в корне репо, сервер `neverwhere-debug`. zcode/IDE стартует
  его автоматически вместе с проектом.
- **Транспорт:** stdio.

cdb search order (см. `find_cdb` в `tools/debug_mcp/debugger_mcp.py`):
1. env `CRASH_ANALYSIS_DEBUGGER_PATH` — в `.mcp.json` уже прописан
   `C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe`
2. Стандартные пути Windows Kits 10/8.1.

Разрешение exe-таргета (`tools/debug_mcp/exe_resolver.py`):
- Layout: `_intermediate_64/<config>/<target>.exe` (по умолчанию `EpicMapEditor`, `Debug`)
- Precedence: аргумент `exe`/`exe_target` → env `NEVERWHERE_DEBUG_EXE` →
  `NEVERWHERE_DEBUG_EXE_TARGET`/`_CONFIG` → `.zcode/debug_active.json`
  (пин через `debug_set_active`) → дефолт.
- Узнать текущее разрешение: `debug_overrides()`.

## Окружение (macOS)

MCP-сервер `neverwhere-debug-macos` (та же `debug_*`-контрактная поверхность, backend — LLDB):
- **Архитектура:** MCP-сервер (venv на Homebrew Python 3.14, `mcp>=1.0`) →
  `lldb_worker.py` (системный `/usr/bin/python3` 3.9 + LLDB SB API). Worker нужен потому,
  что `_lldb.cpython-39-darwin.so` из Xcode собран только под cp39, а MCP SDK требует ≥3.10.
  Backend держит один worker-процесс и гоняет JSON-команды по stdio (аналог long-lived cdb).
- **Файлы:** `tools/debug_mcp/lldb_backend.py` (функции `debug_*`, те же имена/envelope,
  что у cdb backend), `tools/debug_mcp/lldb_worker.py` (SB API: SBDebugger/SBTarget/SBProcess).
- **Лаунчер:** `tools/run_mcp_server.sh` — при первом запуске создаёт `tools/debug_mcp/.venv`
  и ставит `requirements.txt`.
- **Подключение:** `.mcp.json`, отдельная запись `neverwhere-debug-macos` (windows-запись
  на mac просто не стартует — и наоборот; клиент молча пропускает нерабочую).
- **LLDB:** из Xcode (`xcrun lldb`), SB API path — `xcrun lldb -P`. Отдельной установки
  не требуется; `debug_self_check` проверяет всё сам.
- **Символы:** DWARF встроен в Debug-бинарники, PDB/dSYM-манипуляции не нужны.
- **Разрешение exe-таргета:** тот же precedence, layout —
  `_intermediate_64/src/{apps,refs}/<target>/<config>/<target>`
  (тесты — `_intermediate_64/src/tests/<config>/neverwhere_tests`).
- **Smoke backend'а (без MCP):** `tools/debug_mcp/.venv/bin/python tools/debug_mcp/smoke_lldb_worker.py`
  (краш-бинарь с SIGSEGV) и `... --real-app` (EpicGameClient под lldb, DWARF file:line).

Отличия macOS-флоу от Windows, о которых надо помнить:
- **SIGSEGV/SIGABRT приходят как Mach-exception** (`EXC_BAD_ACCESS`, `EXC_CRASH`), не как
  сигнал: `stop_reason = exception`, но worker раскрывает
  `stop_event.exception.signal_equivalent` ("SIGSEGV" и т.п.) — проверяй его.
- **Attach** к своим процессам работает из коробки; к чужим/SIP-защищённым — нет
  (ограничение платформы; GUI другого пользователя — `DevToolsSecurity`).
- **First-chance/second-chance** дихотомии нет: Mach-исключение сразу стопорит inferior.
- `debug_pdb_resolve` / `debug_crash_dump_analyze` / `debug_heap_stat` — `not_supported`
  (на mac символы — DWARF в бинаре; постмортем — `.ips`/core, отдельная задача).
- `debug_session_wait_ready` — фактически no-op (launch/attach в worker синхронны).
- lldb резолвит брейкпоинт на `int main` в первую исполняемую строку (например 403 → 409)
  — это норма, не баг резолвера.

## Базовый flow: поймать краш EpicMapEditor

```
1. debug_self_check
   — подтверждаем, что cdb найден, exe и .pdb на месте, llvm-pdbutil доступен.
   Если что-то из этого отсутствует — чиним окружение, дальше нет смысла.

2. debug_session_start(exe_target="EpicMapEditor", break_on_start=True)
   — запускаем inferior под cdb, останавливаемся на точке входа.
   Для attach к уже бегущему процессу:
     debug_process_find(name_query="EpicMapEditor") → debug_session_start(process_id=<pid>)

3. debug_run_until_stop(session_id, timeout_ms=90000)
   — бежим до падения/брейкпоинта/выхода.
   Qt/QML-приложения грузятся долго — ставим длинный таймаут (60-120s).

4. debug_stack_get(session_id)
   — смотрим стек. Реальная точка сбоя — первый ПРОЕКТНЫЙ фрейм
   (чей source лежит под repo_root()), не верхний CRT/SEH-трамплин.
   Верхние фреймы вида RtlReportException / KiUserExceptionDispatcher / CRT —
   служебные, чинить там нечего.

5. debug_frame_select(session_id, frame=<индекс проектного фрейма>)
   — переключаем контекст, чтобы locals и eval были для нужного фрейма.

6. debug_scopes_get(session_id) — локали и аргументы.
   debug_expression_eval(session_id, "this->someField") — точечная проверка.
   debug_expression_eval(session_id, "myPtr != nullptr") → bool.

7. debug_session_stop(session_id, kill_process=True)
   — закрываем сессию (kill_process=False оставит inferior жить).
```

## Gotchas (выжимка из реальных расследований)

### 1. First-chance exceptions — не все краши

cdb по умолчанию останавливается на **first-chance** exceptions. Многие из них в Qt/QML
переживаются через SEH-обработчик — приложение не падает, а продолжает работать.

**Симптом:** `debug_run_until_stop` вернул `reason=exception`, в стеке Qt-движок
(`QQmlPropertyBinding`, `QV4::*`), и при попытке `continue` приложение бежит свободно.

**Как отличить реальный краш:**
- Real crash → second-chance exception (поле `first_chance: false` в stop_event.exception)
  ИЛИ приложение доходит до `exit`/terminate.
- Benign first-chance → после `continue` процесс живёт (`debug_session_status` → `state=running`).

**Проверить, реально ли падает:**
```python
# Пропустить first-chance AV и дождаться real crash / clean exit
session.execute(['sxd av', 'g'], ...)  # не останавливаться на first-chance AV
debug_run_until_stop(timeout_ms=...)    # дождаться second-chance или выхода
```

Если приложение выходит с `exit code 0` и процесс умирает сам — first-chance был
некритичен, реального краша нет.

### 2. Падение в `qt_static_metacall` / moc-метасистеме

Симптом: краш в `<ClassName>::qt_static_metacall` или `qt_metacall`, стек содержит
`QQmlValueTypeWrapper::readReference`, `QMetaObject::metacall`. Часто `rcx=0` / null deref.

Возможные причины:
- **Грязный autogen-кэш** — после переименования Q_OBJECT/Q_GADGET классов в `moc_*.cpp`
  остались ссылки на старые имена. Лечение:
  ```
  rm -rf _intermediate_64/src/libs/core/core_autogen
  cmake --build --preset debug --target core
  ```
- **Q_GADGET без регистрации в QML** — если gadget-тип используется через Q_PROPERTY,
  но не зарегистрирован (`qmlRegisterType` / `QML_NAMED_ELEMENT`).
- **Несовместимые сигнатуры** в Q_PROPERTY (READ-метод возвращает не тот тип).

### 3. Долгая загрузка inferior

EpicMapEditor — Qt/QML приложение. Старт может занимать 20-60 секунд
(загрузка chapters, assets, image providers). Не используйте короткие startup_timeout_ms.
Если `wait_ready=False` и сразу проверяете статус — можете поймать `state=launching`.

Параметр `wait_ready=True` (по умолчанию) ждёт READY-маркер cdb.

### 4. Сессии живут в рамках Python-процесса

`_SESSIONS` dict в `debugger_mcp.py` — in-memory. Между запусками Python (например,
если вы вызываете бэкенд через bash пошагово) сессии **не сохраняются**. Полный flow
(start → run → stack → stop) должен быть в одном Python-процессе, либо — при работе
через MCP — все вызовы идут в одном инстансе MCP-сервера.

При работе через MCP-сервер (как zcode tool-calls) это ограничение снимается —
MCP-сервер живёт долго, сессия персистентна между tool-call'ами.

### 5. Attach vs Launch

- **Launch** (`exe`/`exe_target`) — cdb сам запускает inferior под собой.
  Плюс: полный контроль с точки входа, можно ставить breakpoint до main().
- **Attach** (`process_id`/`process_name`) — подключаемся к уже бегущему.
  Плюс: если краш случается при интерактивном действии (клик, drag), удобнее
  запустить приложение свободно, воспроизвести, потом attach.

Attach flow:
```
debug_process_find(name_query="EpicMapEditor")  → получаем pid
debug_session_start(process_id=<pid>)           → cdb -p <pid>
debug_interrupt(session_id)                     → асинхронный break
debug_stack_get(session_id)                     → стек в текущей точке
```

## Инспекция: что и как смотреть

| Что | Инструмент | Пример |
|---|---|---|
| Стек текущего потока | `debug_stack_get` | возвращает frames[] с symbol/file/line/address |
| Локали в текущем фрейме | `debug_scopes_get` | после `debug_frame_select(N)` |
| Произвольное выражение | `debug_expression_eval` | `"this->m_cameraZoom"`, `"!ptr"`, `"arr[3]"` |
| Регистры | `debug_registers_get` | особенно `rcx`/`rdx`/`rip` при AV |
| Память по адресу | `debug_memory_read` | `"0x...address"`, size=64 |
| Потоки | `debug_threads_get` + `debug_thread_select` | для deadlock-расследований |
| Модули | `debug_modules_get` | проверить, что DLL loaded |
| Heap | `debug_heap_stat` | для утечек/коррупции |

## Постановка брейкпоинтов

```python
debug_breakpoint_set(session_id, source_file="src/libs/core/map/map_model.cpp", line=277)
debug_breakpoint_set(session_id, symbol="MapModel::load")
debug_breakpoint_set(session_id, address="0x7ff6ee14cd93")
debug_breakpoint_set(session_id, symbol="AssetToolsSelector::click", condition="mapModel == nullptr")
```

Manage: `debug_breakpoint_list`, `debug_breakpoint_enable`/`disable`/`remove`,
bulk-вариант `debug_breakpoint_set_bulk`.

## Постмортем (когда допилим dump-хук)

Сейчас в neverwhere **нет** C++ хука записи дампа при падении (на Windows —
`MiniDumpWriteDump` в main.cpp при SEH; на macOS аналог — свой `.ips`/core handler).
Когда добавим — `debug_crash_dump_analyze(dump_path="...crash_....dmp")` проанализирует
готовый дамп без live-repro (Windows; на mac инструмент пока `not_supported`).
Дампы будут лежать в `CRASH_DUMP_DIR` (env, дефолт — рядом с exe в `CrashDumps/`).

До этого момента — только live-debug (запуск под отладчиком).

## Чек-лист «приложение падает»

1. Воспроизвёл ли ты падение детерминированно? Если нет — attach к запущенному и
   повторяй действие пользователя.
2. `debug_self_check` — окружение ОК?
3. `debug_session_start(exe_target="...")` → `debug_run_until_stop` — поймал stop?
4. Это **first-chance** или **second-chance** exception? Если first-chance и приложение
   бежит дальше — возможно, это норма Qt SEH, не баг.
5. Стек: нашёл **проектный** фрейм под `repo_root()`? Его file:line — место бага.
6. Локали/eval в проектном фрейме: что null/invalid/не то значение?
7. Фикс → rebuild → re-run под cdb → **RE-PROVE** (убедиться, что краш ушёл).

## Ссылки по коду

- `tools/debug_mcp/debugger_mcp.py` — cdb backend (Windows), все `debug_*` функции
- `tools/debug_mcp/lldb_backend.py` — LLDB backend (macOS), тот же контракт
- `tools/debug_mcp/lldb_worker.py` — SB API worker (системный python3 3.9)
- `tools/debug_mcp/exe_resolver.py` — резолв exe-таргета и символов (обе ОС)
- `tools/debug_mcp/server.py` — FastMCP-обёртки `@mcp.tool()`, выбор backend по платформе
- `tools/debug_mcp/smoke_lldb_worker.py` — smoke LLDB backend'а без MCP
- `tools/run_mcp_server.ps1` — лаунчер (Windows), `tools/run_mcp_server.sh` — лаунчер (macOS)
- `.mcp.json` — конфигурация MCP-серверов (`neverwhere-debug` win, `neverwhere-debug-macos` mac)
- `AGENTS.md` — краткая выжимка этого документа
