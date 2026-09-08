# MCP-серверы проекта

_Вынесено из `AGENTS.md` (2026-09-06): корневой файл — компактная входная точка, подробности подсистем живут в `docs/`. Правя соответствующий код, обновляй **этот** документ._

Все серверы объявлены в `.mcp.json` (формат Claude Code) и в его копии `.cursor/mcp.json` (Cursor читает только этот путь — корневой `.mcp.json` он игнорирует); файлы должны совпадать, при правке одного — скопировать во второй. Серверы стартуют вместе с IDE; нерабочая на данной ОС запись просто не поднимается. Новый сервер появляется у агента после перезагрузки окна / включения в Settings → MCP, в уже идущей сессии инструменты не подхватываются.

| Сервер | Назначение | Подробности |
|---|---|---|
| `neverwhere-debug` / `neverwhere-debug-macos` | отладка падений (cdb на Windows, LLDB на macOS), контракт `debug_*` | `docs/debugging.md` |
| `neverwhere-editor` / `neverwhere-editor-win` | автоматизация EpicMapEditor (`editor_*` поверх TCP RPC :9877) | `docs/editor_rpc.md` |
| `neverwhere-pgg` / `neverwhere-pgg-win` | агентский цикл отладки PGG-графов (`pgg_*` поверх TCP RPC :9878; PggViewer `--serve` поднимается автоматически) | `docs/pgg/viewer_rpc.md` |
| `blender` | Blender Lab MCP | ниже |
| Context7 | документация по библиотекам/API — использовать без напоминания | — |

## Blender MCP

Сервер `blender` в `.mcp.json` — официальный Blender Lab MCP (Blender 5.1+). Цепочка: MCP-клиент ↔ stdio-сервер `blender-mcp` (пакет `blmcp` из `projects.blender.org/lab/blender_mcp`, сабдиректория `mcp`) ↔ TCP-сокет `127.0.0.1:9876` внутри Blender (аддон «MCP», null-byte-delimited JSON `{"type":"execute",...}` — НЕ HTTP/SSE). Лauncher — `tools/run_blender_mcp_server.sh` (venv в `tools/blender_mcp/.venv`, requirements пинуют `mcp[cli]<2.0`: SDK 2.0 выкинул `mcp.server.fastmcp`, а у upstream зависимость без верхней границы). Blender должен быть запущен с включённым аддоном и стартованным сервером (Preferences → MCP). Инструменты: `execute_blender_code`, `get_objects_summary`, `render_viewport_to_path`, скриншоты областей и т.п. (26 шт.).
