#!/bin/bash -ex

echo -e "\e[1m\e[33mBuild tools information:\e[0m"
cmake --version
gcc -v
tx --version

mkdir build && cd build
cmake .. -DGENERATE_QT_TRANSLATION=ON -DCMAKE_BUILD_TYPE=Release -DENABLE_SDL2=OFF
make translation
cd ..

cd dist/languages
tx push -s
