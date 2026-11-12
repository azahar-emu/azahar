#!/bin/bash -ex

# Determine the full revision name.
GITDATE="`git show -s --date=short --format='%ad' | sed 's/-//g'`"
GITREV="`git show -s --format='%h'`"

REV_NAME="azahar-libretro-$OS-$TARGET-$GITDATE-$GITREV"
if [ "$GITHUB_REF_TYPE" = "tag" ]; then
    REV_NAME="azahar-libretro-$OS-$TARGET-$GITHUB_REF_NAME"
fi

if [ "$OS" = "android" ]; then
    CORE_FILE="$BUILD_DIR/$EXTRA_PATH/azahar_libretro_android.so"
else
    CORE_FILE="$BUILD_DIR/$EXTRA_PATH/azahar_libretro.*"
fi

# Create .zip
zip -j -9 "$REV_NAME.zip" $CORE_FILE
