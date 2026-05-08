#!/bin/sh -ex

mkdir build && cd build

if [ "$GITHUB_REF_TYPE" == "tag" ]; then
	export EXTRA_CMAKE_FLAGS=(-DENABLE_QT_UPDATE_CHECKER=ON)
fi

# Map the workflow ARCH (x64/arm64) onto CMake's ARCHITECTURE variable so
# Qt downloads and arch-dependent paths pick the right target even if the
# compiler-based detection gets confused (e.g. on windows-11-arm runners
# where the MSVC setup may land on an x64 host compiler).
if [ "$ARCH" = "arm64" ]; then
	export EXTRA_CMAKE_FLAGS=("${EXTRA_CMAKE_FLAGS[@]}" -DARCHITECTURE=arm64)
elif [ "$ARCH" = "x64" ]; then
	export EXTRA_CMAKE_FLAGS=("${EXTRA_CMAKE_FLAGS[@]}" -DARCHITECTURE=x86_64)
fi

cmake .. -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DENABLE_QT_TRANSLATION=ON \
    -DUSE_DISCORD_PRESENCE=ON \
	"${EXTRA_CMAKE_FLAGS[@]}"
ninja
ninja bundle
# MSVC keeps debug info in separate PDB files, so the .exe has nothing
# to strip. The `strip` that ends up in PATH on the runner is also from
# Git-for-Windows' x86_64 mingw and can't read arm64 PE binaries.
case "$TARGET" in
    msvc*) ;;
    *) strip -s bundle/*.exe ;;
esac

ccache -s -v

ctest -VV -C Release || echo "::error ::Test error occurred on Windows build"
