#!/bin/sh

src_dir="toolchain/stuff"
dst_dir="_b-em"

# Проверка существования исходной папки
if [ ! -d "$dst_dir" ]; then
    echo "Error: Don't exist $dst_dir path, run 'build_emscripten.sh' first!"
    exit 1
fi

# Копирование с выводом прогресса
echo "Copy from $src_dir to $dst_dir..."
cp -rv "$src_dir"/* "$dst_dir"/

echo "Copy succes!"

cd $(pwd)/$dst_dir
python serve.py