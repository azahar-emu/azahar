#!/bin/bash -ex

# Builds idevice-ffi (https://github.com/jkcoxson/idevice, MIT), the library the JIT helper of the
# iOS app uses to talk to the device's debug service. The library, its header and a module map
# end up in $OUT/lib and $OUT/include. The app builds fine without it (the helper then reports that
# the library is missing), so a failure here is reported but does not have to stop the build.

OUT=${1:-build/idevice-out}
WORK=${WORK:-build/idevice-src}
REPO_URL="https://github.com/jkcoxson/idevice.git"
TAG="v0.1.68"
PIN="d32c8189c51c2789496b0768039419c3705498c3"
TARGET="aarch64-apple-ios"
FEATURES="debug_proxy dvt rsd remote_pairing tunnel_tcp_stack rustcrypto mobile_image_mounter tss cryptexd"

if ! command -v cargo >/dev/null; then
    # rustup's own installer, no Homebrew needed.
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal
    # shellcheck disable=SC1091
    source "$HOME/.cargo/env"
fi
rustup target add "$TARGET"
cargo --version

rm -rf "$WORK" "$OUT"
git clone --depth 1 --branch "$TAG" --recurse-submodules "$REPO_URL" "$WORK"
actual=$(git -C "$WORK" rev-parse HEAD)
if [ "$actual" != "$PIN" ]; then
    echo "The tag $TAG resolves to $actual, expected $PIN"
    exit 1
fi

cd "$WORK"
export IPHONEOS_DEPLOYMENT_TARGET=16.0
cargo build --locked --release -p idevice-ffi --target "$TARGET" \
    --no-default-features --features "$FEATURES"

# The TLS stacks that need a C toolchain for the target are left out on purpose.
if cargo tree --locked -p idevice-ffi --target "$TARGET" --no-default-features \
    --features "$FEATURES" | grep -E "aws-lc|ring v"; then
    echo "warning: a C based crypto library is part of the build"
fi

cd - >/dev/null
mkdir -p "$OUT/lib" "$OUT/include"
cp "$WORK/target/$TARGET/release/libidevice_ffi.a" "$OUT/lib/"
cp "$WORK/ffi/idevice.h" "$OUT/include/"
printf 'module idevice {\n    header "idevice.h"\n    export *\n}\n' > "$OUT/include/module.modulemap"
ls -la "$OUT/lib" "$OUT/include"
