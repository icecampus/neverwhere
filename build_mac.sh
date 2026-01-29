INTERMEDIATE_PATH="_int_mac"  

if [ ! -d "$INTERMEDIATE_PATH" ]; then
    mkdir -p "$INTERMEDIATE_PATH"
fi

cd "$INTERMEDIATE_PATH"

cmake -DCMAKE_TOOLCHAIN_FILE=toolchain/vcpkg/scripts/buildsystems/vcpkg.cmake \
      .. \
      -G "Xcode" 

cd ..