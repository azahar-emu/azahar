#!/bin/bash -ex

if [ "$GITHUB_REF_TYPE" == "tag" ]; then
	export EXTRA_CMAKE_FLAGS=(-DENABLE_QT_UPDATE_CHECKER=ON)
fi

if [ "$BUILD_ARCH" == "x86_64" ]; then
    export EXTRA_CMAKE_FLAGS=("${EXTRA_CMAKE_FLAGS[@]}" -DENABLE_FFMPEG=OFF)
fi

if [ "$BUILD_ARCH" == "arm64" ]; then
    export EXTRA_CMAKE_FLAGS=("${EXTRA_CMAKE_FLAGS[@]}" -DCMAKE_TOOLCHAIN_FILE=$HOME/conan/conan_toolchain.cmake)
fi

mkdir -p build/$BUILD_ARCH && cd build/$BUILD_ARCH
cmake ../.. -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$BUILD_ARCH" \
    -DENABLE_ROOM_STANDALONE=OFF \
    -DENABLE_DISCORD_RPC=ON \
	"${EXTRA_CMAKE_FLAGS[@]}"
ninja
ninja bundle
mv ./bundle/azahar.app ./bundle/Azahar.app # TODO: Can this be done in CMake?


CURRENT_ARCH=`arch`
if [ "$BUILD_ARCH" = "$CURRENT_ARCH" ]; then
  ctest -VV -C Release
fi
