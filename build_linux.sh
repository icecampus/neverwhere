#!/bin/sh
# Linux: конфигурация через preset `linux` (Ninja, триплет x64-linux; vcpkg manifest
# доустановит зависимости при первом запуске — это долго, собирается Qt).
# Бинарная директория: _int_linux (НЕ _intermediate_64 — та занята win/mac-кэшем).
# Сборка: cmake --build --preset linux-debug --target EpicMapEditor
set -e
cd "$(dirname "$0")"
cmake --preset linux
