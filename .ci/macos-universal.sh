#!/bin/bash -ex

ARTIFACTS_LIST=($ARTIFACTS)

BUILD_DIR=build
UNIVERSAL_DIR=$BUILD_DIR/universal
BUNDLE_DIR=$UNIVERSAL_DIR/bundle
OTHER_BUNDLE_DIR=$BUILD_DIR/x86_64/bundle

# Set up the base bundle to combine into.
mkdir $UNIVERSAL_DIR
cp -a $BUILD_DIR/arm64/bundle $UNIVERSAL_DIR

# Executable binary paths that need to be combined.
BIN_PATHS=(Azahar.app/Contents/MacOS/azahar)

# Dylib paths that need to be combined.
IFS=$'\n'
DYLIB_PATHS=($(cd $BUNDLE_DIR && find . -name '*.dylib'))
unset IFS

# Combine all of the executable binaries and dylibs.
for BIN_PATH in "${BIN_PATHS[@]}"; do
    lipo -create -output $BUNDLE_DIR/$BIN_PATH $BUNDLE_DIR/$BIN_PATH $OTHER_BUNDLE_DIR/$BIN_PATH
done

for DYLIB_PATH in "${DYLIB_PATHS[@]}"; do
    # Only merge if the libraries do not have conflicting arches, otherwise it will fail.
    DYLIB_INFO=`file $BUNDLE_DIR/$DYLIB_PATH`

    OTHER_DYLIB_INFO=`file $OTHER_BUNDLE_DIR/$DYLIB_PATH`
    if ! [[ "$DYLIB_INFO" =~ "x86_64" ]] && ! [[ "$OTHER_DYLIB_INFO" =~ "arm64" ]]; then
        lipo -create -output $BUNDLE_DIR/$DYLIB_PATH $BUNDLE_DIR/$DYLIB_PATH $OTHER_BUNDLE_DIR/$DYLIB_PATH
    fi
done

# Remove leftover libs so that they aren't distributed
rm -rf "${BUNDLE_DIR}/libs"

# Re-sign executables and bundles after combining.
APP_PATHS=(Azahar.app)
for APP_PATH in "${APP_PATHS[@]}"; do
    codesign --deep -fs - $BUNDLE_DIR/$APP_PATH
done
