#!/bin/bash -ex

# This script assumes that Git is installed

cd ./dist/compatibility_list/
git fetch origin
git checkout master
git pull origin master
git checkout --detach
