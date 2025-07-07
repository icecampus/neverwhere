#!/usr/bin/env bash
print_path()
{
	echo "+++++++++++++++++++++"
	IN=${PATH}
	while [ "$IN" != "$iter" ] ;do
	    # extract the substring from start of string up to delimiter.
	    iter=${IN%%:*}
	    # delete this first "element" AND his separator, from $IN.
	    IN="${IN#$iter:}"
	    # Print (or doing anything with) the first "element".
	    printf '> %s\n' "$iter"
	done
}

#start
root_path=$(pwd)
emsdk_path=$root_path/toolchain/emsdk
vcpkg_path=$root_path/toolchain/vcpkg
ninja_dir_path=$root_path/toolchain/ninja-win

echo "==========================================================="
echo "Root path: $root_path"

export PATH=$ninja_dir_path:$emsdk_path:$PATH
ninja_path=$(where ninja)
echo "Ninja is: $ninja_path" 
echo "EMSDK: $emsdk_path"
echo "==========================================================="

export EMSDK_QUIET=1

cd $emsdk_path
	./emsdk install 3.1.70
	./emsdk activate 3.1.70
	source emsdk_env.sh 
cd $root_path

mkdir _b-em
cd _b-em
	echo $(pwd)
	emcmake cmake \
	-DCMAKE_CXX_FLAGS="-gsource-map -O0" \
    -DCMAKE_C_FLAGS="-gsource-map -O0" \
    -DCMAKE_EXE_LINKER_FLAGS="-gsource-map --emdebug" \
	-DCMAKE_TOOLCHAIN_FILE=$vcpkg_path/scripts/buildsystems/vcpkg.cmake \
	-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$emsdk_path/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake \
	-DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
	../ -G Ninja


cmake --build .  --config Release 
cd ..