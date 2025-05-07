#!/bin/bash -ex

# SPDX-FileCopyrightText: 2024 yuzu Emulator Project
# SPDX-License-Identifier: MIT

# This script assumes that Git is installed

git submodule sync
git submodule foreach --recursive git reset --hard
git submodule update --init --recursive
