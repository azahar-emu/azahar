#!/bin/bash -ex

if [ "$GITHUB_REF_TYPE" == "tag" ]; then
	export EXTRA_CMAKE_FLAGS=(-DENABLE_QT_UPDATE_CHECKER=ON)
fi

mkdir build && cd build
cmake .. -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$TARGET" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DENABLE_QT_TRANSLATION=ON \
    -DENABLE_ROOM_STANDALONE=OFF \
    -DUSE_DISCORD_PRESENCE=ON \
	"${EXTRA_CMAKE_FLAGS[@]}"
ninja
ninja bundle
mv ./bundle/azahar.app ./bundle/Azahar.app # TODO: Can this be done in CMake?

ccache -s -v

CURRENT_ARCH=`arch`
if [ "$TARGET" = "$CURRENT_ARCH" ]; then
  ctest -VV -C Release
fi
