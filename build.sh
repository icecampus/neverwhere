INTERMEDIATE_PATH="_intermediate_64"  

if [ ! -d "$INTERMEDIATE_PATH" ]; then
    mkdir -p "$INTERMEDIATE_PATH"
fi

cd "$INTERMEDIATE_PATH"

cmake -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_INSTALL_OPTIONS="--x-buildtrees-root=d:/_build" \
      .. \
      -G "Visual Studio 17 2022" \
      -A x64

cd ..