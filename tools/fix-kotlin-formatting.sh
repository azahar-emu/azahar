#!/bin/bash -ex

# TODO: Why doesn't the CI environment use the PATH set in the Dockerimage?
#       It works fine when using the image locally.
export PATH="/home/linuxbrew/.linuxbrew/bin:${PATH}"

cd ./src/android/ && ktlint -F --color '!./app/build/generated/**'
