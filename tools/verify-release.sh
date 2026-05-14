#!/usr/bin/env bash

# Copyright Citra Emulator Project / Azahar Emulator Project
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

set -euo pipefail

# Usage:
#   ./verify-release.sh <owner/repo> <tag>
#
# Example:
#   ./verify-release.sh azahar-emu/azahar 2126.0
#
# Behavior:
#   - Downloads all release assets
#   - Verifies asset is published in the release
#   - Verifies SPDX attestations for every asset
#
# Notes:
#   - Draft release support requires authentication with permission
#     to view the draft release.
#   - gh release verify-asset currently does NOT support draft releases.

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <owner/repo> <tag>"
    exit 1
fi

command -v gh >/dev/null 2>&1 || {
    echo "ERROR: GitHub CLI (gh) is not installed or not in PATH"
    exit 1
}

REPO="$1"
TAG="$2"

echo "==> Fetching release metadata"

IS_DRAFT=$(
    gh release view "$TAG" \
        --repo "$REPO" \
        --json isDraft \
        --jq '.isDraft'
)

WORKDIR="verify/release-${TAG}"

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"
cd "$WORKDIR"

echo
echo "==> Downloading release assets"

gh release download "$TAG" \
    --repo "$REPO"

echo
echo "==> Fetching asset list"

mapfile -t ASSETS < <(
    gh release view "$TAG" \
        --repo "$REPO" \
        --json assets \
        --jq '.assets[].name'
)

echo
echo "==> Release type: $(
    [[ "$IS_DRAFT" == "true" ]] && echo "draft" || echo "published"
)"

echo
echo "==> Verifying assets"

for asset in "${ASSETS[@]}"; do
    # Skip attestation files themselves
    if [[ "$asset" == *.intoto.jsonl ]]; then
        continue
    fi

    if [[ ! -f "$asset" ]]; then
        echo "ERROR: Missing downloaded asset: $asset"
        exit 1
    fi

    echo
    echo "========================================"
    echo "Asset: $asset"
    echo "========================================"

    echo "1/2 release verify-asset"
    
    if [[ "$IS_DRAFT" != "true" ]]; then
        gh release verify-asset "$TAG" "$asset" \
            --repo "$REPO"
        echo
    else
        echo "SKIPPED (draft releases unsupported)"
        echo
    fi

    echo "2/2 attestation verify (SPDX)"
    
    if [[ "$asset" == *.sha256sum ]]; then
        echo "SKIPPED (sha256sum does not need verification)"
    else
        gh attestation verify "$asset" \
            --repo "$REPO" \
            --predicate-type https://spdx.dev/Document
    fi

    echo "OK: $asset"
done

rm -rf "$WORKDIR"

echo
echo "========================================"
echo "All assets verified successfully"
echo "========================================"