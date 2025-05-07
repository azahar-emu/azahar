#!/bin/bash -ex

# This script assumes that the Transifex CLI and Qt Linquist CLI tools are installed

cd ./dist/languages/
tx pull -a -f
cd ../../
lupdate -recursive './src' -ts ./dist/languages/*.ts