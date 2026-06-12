# Suggested Commands

## Generate
```bat
generate_vs.bat                :: bootstrap vcpkg + cmake preset vs2022
generate_vs.bat vs2022         :: explicit preset
```

## Build (single target)
```bat
cmake --build --preset debug --target EpicMapEditor
cmake --build --preset release --target EpicMapEditor
```

## Windows-specific
- Use `cmake --preset` instead of `cmake -B` directly
- vcpkg overlay ports configured in CMakePresets.json
- Build trees at `d:/_build` (VCPKG_INSTALL_OPTIONS)
- MSVC: `/MP` enabled for parallel compilation