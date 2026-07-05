# Compile commands / IDE indexing

## Setup
- Project uses **Visual Studio generator** for actual builds (`vs2022` preset), which **cannot** emit `compile_commands.json`.
- For clangd/Serena indexing there's a separate **Ninja** preset `ide-index` in `CMakePresets.json` (binaryDir `_intermediate_ide`).
- Helper script: `generate_compile_commands.bat` at repo root — runs vcvars64.bat + Ninja configure + publishes `compile_commands.json` (copy/symlink) to repo root.

## When to regenerate
Run `generate_compile_commands.bat` (optionally `--clean`) after:
- adding/removing source files
- changing compile flags / `nw_add_*` macros
- changing `vcpkg.json` dependencies

NOT needed after editing existing source files (clangd reuses flags).

## Without compile_commands.json
- Serena LSP falls back to structural parse (knows names, not types).
- `get_diagnostics_for_file` produces false-positive noise: `'QObject' file not found`, `Unknown type name 'Q_OBJECT'/'Q_PROPERTY'/'Q_INVOKABLE'`.
- `find_referencing_symbols` is incomplete for inline methods / templates.

## With compile_commands.json
- Diagnostics are clean (verified 2026-07-05: diamond_isometry.h, editor_rpc_server.cpp, main.cpp all return `{}`).
- `find_referencing_symbols` resolves through `iso->getDimensions()` etc.
- 231 translation units indexed for `neverwhere`.

## Files (all gitignored)
- `_intermediate_ide/` — Ninja build dir (configure-only, do NOT build here)
- `compile_commands.json` — published copy/symlink at repo root
- `.cache/` — clangd working cache
