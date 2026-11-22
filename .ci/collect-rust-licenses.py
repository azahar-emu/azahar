#!/usr/bin/env python3
# Copyright 2026 Citra Emulator Project / Azahar Emulator Project
# Licensed under GPLv3 or any later version
# Refer to the LICENSE.txt file included.

"""Writes the license texts of a Rust package and every crate it links (normal dependencies for
one target and set of features, not build or dev dependencies) in the entry format of the iOS
app's Licenses.txt (see src/citra_ios/CMakeLists.txt): a line "@@@ name", a line with the license
and where the code comes from, then the text.

Usage: collect-rust-licenses.py <workspace dir> <package> <target> <features> <output file>
"""

import json
import os
import re
import subprocess
import sys

MIT_TEXT = """Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE."""

LICENSE_PREFIXES = ("license", "licence", "copying", "notice", "unlicense", "copyright")


def license_files(directory):
    names = []
    for name in sorted(os.listdir(directory)):
        path = os.path.join(directory, name)
        if os.path.isfile(path) and name.lower().startswith(LICENSE_PREFIXES):
            names.append(path)
    return names


def main():
    workspace, package, target, features, output = sys.argv[1:6]
    metadata = json.loads(subprocess.check_output(
        ["cargo", "metadata", "--locked", "--format-version", "1", "--filter-platform", target,
         "--no-default-features", "--features", features],
        cwd=workspace))
    by_name_version = {(p["name"], p["version"]): p for p in metadata["packages"]}
    # The crates that end up in the library: cargo tree resolves the features of this one package
    # (the metadata resolves those of the whole workspace) and follows normal dependencies only,
    # not build scripts or tests.
    tree = subprocess.check_output(
        ["cargo", "tree", "--locked", "-p", package, "--target", target, "--no-default-features",
         "--features", features, "-e", "normal", "--prefix", "none", "--format", "{p}"],
        cwd=workspace, text=True)
    seen = set()
    for line in tree.splitlines():
        match = re.match(r"^(\S+) v(\S+)", line.strip())
        if match:
            seen.add((match.group(1), match.group(2)))

    entries = []
    missing = []
    for key in seen:
        pkg = by_name_version.get(key)
        if pkg is None:
            sys.exit(f"{key[0]} {key[1]} is in cargo tree but not in cargo metadata")
        directory = os.path.dirname(pkg["manifest_path"])
        files = license_files(directory)
        # Crates of a workspace often keep the license at the workspace root only.
        if not files and pkg.get("source") is None:
            files = license_files(workspace)
        where = pkg.get("repository") or pkg.get("homepage") or "crates.io"
        summary = f"{pkg.get('license') or 'see text'}, {where} (Rust crate, version {pkg['version']})"
        texts = []
        for path in files:
            with open(path, encoding="utf-8", errors="replace") as f:
                texts.append(f.read().strip())
        if not texts:
            missing.append(pkg["name"])
            authors = ", ".join(pkg.get("authors") or []) or f"the {pkg['name']} authors"
            if "MIT" in (pkg.get("license") or ""):
                texts.append(f"Copyright (c) {authors}\n\n{MIT_TEXT}")
            else:
                texts.append(f"Licensed under {pkg.get('license') or 'an unknown license'} by "
                             f"{authors}. The crate ships no license file; the full text of that "
                             "license applies.")
        entries.append((pkg["name"].lower(), pkg["name"], summary, texts))

    # The same license text (the Apache License most of all) is written once and referred to.
    entries.sort()
    first_with = {}
    with open(output, "w", encoding="utf-8") as f:
        for _, name, summary, texts in entries:
            parts = []
            for text in texts:
                normalized = " ".join(text.split())
                if normalized in first_with and len(normalized) > 2000:
                    parts.append(f"(The same text as for {first_with[normalized]}.)")
                else:
                    first_with.setdefault(normalized, name)
                    parts.append(text)
            body = "\n\n".join(parts)
            f.write(f"@@@ {name}\n{summary}\n{body}\n")
    print(f"{len(entries)} crates, license texts written to {output}")
    if missing:
        print("crates without a license file: " + ", ".join(sorted(missing)))


if __name__ == "__main__":
    main()
