#!/bin/bash -ex

# TODO: Why doesn't the CI environment use the PATH set in the Dockerimage?
#       It works fine when using the image locally.
export PATH="/opt/mxe/usr/bin:${PATH}"

mkdir build && cd build

if [ "$GITHUB_REF_TYPE" == "tag" ]; then
	export EXTRA_CMAKE_FLAGS=(-DENABLE_QT_UPDATE_CHECKER=ON)
fi

x86_64-w64-mingw32.shared-cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER_AR=x86_64-w64-mingw32.shared-ar \
    -DCMAKE_CXX_COMPILER_RANLIB=x86_64-w64-mingw32.shared-ranlib \
    -DENABLE_DISCORD_RPC=ON \
	"${EXTRA_CMAKE_FLAGS[@]}"
x86_64-w64-mingw32.shared-cmake --build . -- -j$(nproc)
x86_64-w64-mingw32.shared-strip -s bin/Release/*.exe
make bundle

ccache -s -v
