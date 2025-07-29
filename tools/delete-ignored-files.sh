#!/bin/bash -ex

# This script assumes that Git is installed

# The following deletes all files and directories which are ignored by `.gitignore`

git clean -dfX
