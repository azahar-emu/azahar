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
#   - Extracts SPDX SBOMs
#
# Notes:
#   - Requires installation of the GitHub CLI (gh) and jq tools.
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

command -v jq >/dev/null 2>&1 || {
    echo "ERROR: jq is not installed or not in PATH"
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
SBOMSUBDIR="sbom"

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"
cd "$WORKDIR"
mkdir -p "$SBOMSUBDIR"


echo
echo "==> Downloading release assets"

gh release download "$TAG" \
    --repo "$REPO"

echo
echo "==> Fetching asset list"

ASSETS=()

while IFS= read -r asset; do
    ASSETS+=("$asset")
done < <(
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

    echo "1/3 Release asset verification"

    if [[ "$IS_DRAFT" != "true" ]]; then
        gh release verify-asset "$TAG" "$asset" \
            --repo "$REPO"
        echo
    else
        echo "SKIPPED (draft releases unsupported)"
        echo
    fi

    echo "2/3 Attestation verification"

    if [[ "$asset" == *.sha256sum ]]; then
        echo "SKIPPED (sha256sum does not need verification)"
        echo "SKIPPED (no SPDX SBOM extraction)"
    else
        gh attestation verify "$asset" \
            --repo "$REPO" \
            --predicate-type https://spdx.dev/Document

        echo
        echo "3/3 SBOM extraction"

        BASE_NAME="$(basename "$asset")"
        SBOM_FILE="${SBOMSUBDIR}/${BASE_NAME}.spdx.json"

        # gh attestation download does not currently support
        # specifying the output file, nor it allows piping the
        # output. For that reason, we need to find the .jsonl
        # in the current directory.

        # Exclude any existing .jsonl files from find
        # (failsafe, should not happen)
        BEFORE_JSONL="$(find . -maxdepth 1 -name '*.jsonl' -print)"

        gh attestation download "$asset" \
            --repo "$REPO" \
            >/dev/null

        ATTESTATION_FILE=""

        while IFS= read -r file; do
            FOUND=false

            while IFS= read -r oldfile; do
                if [[ "$file" == "$oldfile" ]]; then
                    FOUND=true
                    break
                fi
            done <<< "$BEFORE_JSONL"

            # Only consider new jsonl files
            if [[ "$FOUND" == "false" ]]; then
                ATTESTATION_FILE="$file"
                break
            fi
        done < <(find . -maxdepth 1 -name '*.jsonl' -print)

        if [[ -z "$ATTESTATION_FILE" ]]; then
            echo "ERROR: Could not locate downloaded attestation jsonl"
            exit 1
        fi

        # Extract and decode the SBOM from the jsonl
        jq -r '
            .dsseEnvelope.payload
        ' "$ATTESTATION_FILE" |
        while IFS= read -r payload; do
            echo "$payload" | base64 -d
        done |
        jq '.predicate' \
            > "$SBOM_FILE"

        rm -f "$ATTESTATION_FILE"

        echo "Saved SBOM: $SBOM_FILE"
    fi

    echo
    echo "OK: $asset"
done

echo
echo "========================================"
echo "All assets verified successfully"
echo "SBOMs saved in: $WORKDIR/$SBOMSUBDIR"
echo "========================================"