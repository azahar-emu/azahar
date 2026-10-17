#!/usr/bin/env python3
# Copyright 2026 Citra Emulator Project / Azahar Emulator Project
# Licensed under GPLv3 or any later version
# Refer to the LICENSE.txt file included.

"""
Python script that updates the license headers to match
the expected format.

Only two kinds of headers can be rewritten automatically:
* the old Citra-only header, for example:
    // Copyright 2014 Citra Emulator Project
    // Licensed under GPLv2 or any later version
    // Refer to the license.txt file included.

* the old GPLv2 header used by Azahar, for example:
    // Copyright 2018-2025 Citra Emulator Project / Azahar Emulator Project
    // Licensed under GPLv2 or any later version
    // Refer to the misc/licenses/gplv2.txt file included.

Furthermore, this tool updates the year range of the license header,
for exampe: "2018-2025" -> "2018-2026"

Headers that do not follow those formats are reported for manual review
as they require manual modification. Files with a missing header are
reported as well, and only fixed automatically if --add-missing is
passed.

The tool only checks license headers for those files that changed compared
to the master branch, with uncommitted and untracked files included. Use
--all to check all files, or pass individual files to only check them.

Use --read-only-run to only show changes and not write anything.

Usage (from anywhere inside the repository):
    ./tools/fix-license-headers.py                 # files changed vs master
    ./tools/fix-license-headers.py src/core/foo.cpp src/core/bar.h
    ./tools/fix-license-headers.py --all           # every file in the tree
    ./tools/fix-license-headers.py --read-only-run       # show changes, write nothing
"""

import argparse
import datetime
import difflib
import os
import re
import subprocess
import sys

CHECKED_EXTENSIONS = (
    ".cpp", ".cpp.in", ".frag", ".h", ".h.in", ".inc", ".kt", ".kts", ".m", ".mm", ".vert",
)

NEW_HEADER_TEMPLATE = (
    "// Copyright {years} Citra Emulator Project / Azahar Emulator Project\n"
    "// Licensed under GPLv3 or any later version\n"
    "// Refer to the LICENSE.txt file included.\n"
)

CURRENT_HEADER_RE = re.compile(
    r"\A// Copyright (?P<start>\d{4})(?:-(?P<end>\d{4}))? "
    r"Citra Emulator Project / Azahar Emulator Project\n"
    r"// Licensed under GPLv3 or any later version\n"
    r"// Refer to the LICENSE\.txt file included\.\n"
)

# Old headers that can be converted automatically. The owner must be exactly
# "Citra Emulator Project" or "Citra Emulator Project / Azahar Emulator
# Project". Anything else does not match.
OLD_HEADER_RE = re.compile(
    r"\A//[ \t]*Copyright[ \t]+(?P<start>\d{4})(?:[ \t]*-[ \t]*(?P<end>\d{4}))?[ \t]+"
    r"(?P<owner>Citra Emulator Project(?:[ \t]*/[ \t]*Azahar Emulator Project)?)[ \t]*\n"
    r"//[ \t]*Licensed under (?P<license>GPLv2 or any later version|GPLv2\+|GPLv3 or any later version)[ \t]*\n"
    r"//[ \t]*Refer to the (?:license\.txt|LICENSE\.txt|misc/licenses/gplv2\.txt) file included\.{1,2}[ \t]*\n"
)

# Words that indicate some form of license/copyright notice near the top of a
# file. If one of them appears and the header did not match above, the file has
# a license we must not touch.
LICENSE_HINT_RE = re.compile(
    r"copyright|licen[cs]e|spdx|public domain|permission is hereby granted|gpl|\(c\)",
    re.IGNORECASE,
)
HINT_SCAN_LINES = 30

BOM = "\ufeff"

# Helpers to print the diffs

USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None

def color(text, code):
    return f"\033[{code}m{text}\033[0m" if USE_COLOR else text


def bold(text):
    return color(text, "1")


def red(text):
    return color(text, "31")


def green(text):
    return color(text, "32")


def yellow(text):
    return color(text, "33")


def cyan(text):
    return color(text, "36")


def print_diff(path, old_text, new_text):
    diff = difflib.unified_diff(
        old_text.splitlines(keepends=True),
        new_text.splitlines(keepends=True),
        fromfile=f"a/{path}",
        tofile=f"b/{path}",
        n=1,
    )
    for line in diff:
        line = line.rstrip("\n")
        if line.startswith(("+++", "---")):
            print(bold(line))
        elif line.startswith("@@"):
            print(cyan(line))
        elif line.startswith("+"):
            print(green(line))
        elif line.startswith("-"):
            print(red(line))
        else:
            print(line)


# Helpers to interface with git

def git(*args, cwd=None):
    result = subprocess.run(
        ["git", *args], cwd=cwd, capture_output=True, text=True, check=False
    )
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed:\n{result.stderr.strip()}")
    return result.stdout


def repo_root():
    try:
        return git("rev-parse", "--show-toplevel").strip()
    except (RuntimeError, FileNotFoundError):
        sys.exit("error: this script must be run from inside the Azahar git repository")


def resolve_base(root, base):
    candidates = [base] if base else ["master", "origin/master"]
    for candidate in candidates:
        try:
            git("rev-parse", "--verify", "--quiet", candidate + "^{commit}", cwd=root)
            return candidate
        except RuntimeError:
            continue
    sys.exit(
        f"error: could not find the base branch ({' or '.join(candidates)}).\n"
        "Run 'git fetch origin master', pass --base, or list files explicitly."
    )


def get_changed_files(root, base):
    merge_base = git("merge-base", base, "HEAD", cwd=root).strip()
    # Committed + staged + unstaged changes since the branch point.
    files = git("diff", "--name-only", "--diff-filter=d", merge_base, cwd=root).split("\n")
    # New files that have not been added yet.
    files += git("ls-files", "--others", "--exclude-standard", cwd=root).split("\n")
    return sorted({f for f in files if f})


def get_all_files(root):
    return sorted(f for f in git("ls-files", cwd=root).split("\n") if f)


# Header process helpers

def format_years(start, end, year):
    start = int(start)
    end = max(int(end) if end else start, year)
    start = min(start, end)
    return str(end) if start == end else f"{start}-{end}"


class Result:
    FIXED = "fixed"
    ADDED = "added"
    MISSING = "missing"
    OK = "ok"
    MANUAL = "manual"
    ERROR = "error"

    def __init__(self, path, status, old_text=None, new_text=None, note=None, has_bom=False):
        self.path = path
        self.status = status
        self.old_text = old_text
        self.new_text = new_text
        self.note = note
        self.has_bom = has_bom


def process_file(root, path, year, add_missing):
    full_path = os.path.join(root, path)
    try:
        with open(full_path, "rb") as f:
            raw = f.read()
        text = raw.decode("utf-8")
    except (OSError, UnicodeDecodeError) as e:
        return Result(path, Result.ERROR, note=str(e))

    has_bom = text.startswith(BOM)
    body = text[len(BOM):] if has_bom else text

    match = CURRENT_HEADER_RE.match(body)
    if match:
        years = format_years(match["start"], match["end"], year)
        current = match["start"] if not match["end"] else f"{match['start']}-{match['end']}"
        if years == current:
            return Result(path, Result.OK)
        new_body = NEW_HEADER_TEMPLATE.format(years=years) + body[match.end():]
        return Result(path, Result.FIXED, body, new_body,
                      note="year range updated", has_bom=has_bom)

    match = OLD_HEADER_RE.match(body)
    if match:
        years = format_years(match["start"], match["end"], year)
        new_body = NEW_HEADER_TEMPLATE.format(years=years) + body[match.end():]
        kind = "old Citra-only header" if "Azahar" not in match["owner"] else "old GPLv2 header"
        if match["license"].startswith("GPLv3"):
            kind = "outdated header"
        return Result(path, Result.FIXED, body, new_body, note=kind, has_bom=has_bom)

    head = "\n".join(body.split("\n")[:HINT_SCAN_LINES])
    if LICENSE_HINT_RE.search(head):
        return Result(path, Result.MANUAL, body,
                      note="has a license header that cannot be updated automatically")

    if add_missing:
        new_body = NEW_HEADER_TEMPLATE.format(years=year) + "\n" + body
        return Result(path, Result.ADDED, body, new_body,
                      note="no license header found, header added", has_bom=has_bom)
    return Result(path, Result.MISSING, body, note="no license header found")


def write_result(root, result):
    text = (BOM if result.has_bom else "") + result.new_text
    with open(os.path.join(root, result.path), "w", encoding="utf-8", newline="") as f:
        f.write(text)

DISCLAIMER = """\
================================================================================
                                   IMPORTANT
================================================================================
This tool rewrites license headers. Changing a license header has legal
implications, by applying these changes YOU are stating that the new header
(GPLv3 or any later version, Citra Emulator Project / Azahar Emulator Project)
correctly describes the licensing of each of the files listed above.

You are responsible for these changes. Please review every change above before
continuing, and do not accept them if any file contains code that is not
owned by the Citra / Azahar projects or that you are not sure about.
================================================================================"""


def confirm():
    print(yellow(DISCLAIMER))
    try:
        answer = input(bold('Type "yes" to accept responsibility and apply the changes: '))
    except (EOFError, KeyboardInterrupt):
        print()
        return False
    return answer.strip().lower() == "yes"


def main():
    parser = argparse.ArgumentParser(
        description="Update old Citra/Azahar license headers to the current GPLv3 header "
                    "format.",
    )
    parser.add_argument("paths", nargs="*",
                        help="files or directories to check (default: files changed vs the base branch)")
    parser.add_argument("--all", action="store_true",
                        help="check every tracked file in the repository")
    parser.add_argument("--base", metavar="REF",
                        help="branch to compare against (default: master, then origin/master)")
    parser.add_argument("--year", type=int, default=datetime.date.today().year,
                        help="year the header range must include (defult: current year)")
    parser.add_argument("--add-missing", action="store_true",
                        help="add the header to files that have no license header at all")
    parser.add_argument("--read-only-run", action="store_true",
                        help="only show the chnages without asking to apply them")
    args = parser.parse_args()

    root = repo_root()

    if args.paths:
        files = []
        for p in args.paths:
            abs_p = os.path.abspath(p)
            if os.path.isdir(abs_p):
                rel = os.path.relpath(abs_p, root)
                files += git("ls-files", "--cached", "--others", "--exclude-standard",
                             "--", rel, cwd=root).split("\n")
            else:
                files.append(os.path.relpath(abs_p, root))
        files = sorted({f for f in files if f})
        source = "the given paths"
    elif args.all:
        files = get_all_files(root)
        source = "all tracked files"
    else:
        base = resolve_base(root, args.base)
        files = get_changed_files(root, base)
        source = f"files changed relative to {base}"

    candidates = [f for f in files
                  if f.endswith(CHECKED_EXTENSIONS) and os.path.isfile(os.path.join(root, f))]
    skipped = [f for f in files if args.paths and f not in candidates]

    print(f"Checking {len(candidates)} files from {source} (year {args.year})\n")
    for f in skipped:
        print(f"  skipped (not a file type checked by the CI or missing): {f}")
    if skipped:
        print()

    results = [process_file(root, f, args.year, args.add_missing) for f in candidates]
    changes = [r for r in results if r.status in (Result.FIXED, Result.ADDED)]
    manual = [r for r in results if r.status == Result.MANUAL]
    missing = [r for r in results if r.status == Result.MISSING]
    errors = [r for r in results if r.status == Result.ERROR]
    ok = [r for r in results if r.status == Result.OK]

    if changes:
        print(bold(f"Proposed changes ({len(changes)} files):\n"))
        for r in changes:
            print(bold(f"{r.path}") + f"  ({r.note})")
            print_diff(r.path, r.old_text, r.new_text)
            print()

    if manual:
        print(bold(yellow(f"Needs manual review ({len(manual)} files), NOT modified:")))
        print("These files have a license header that cannot be updated automatically.\n"
              "Decide by hand how they should be adjusted.\n")
        for r in manual:
            print(f"  {yellow(r.path)}")
            for line in r.old_text.split("\n")[:4]:
                print(f"      | {line}")
        print()

    if missing:
        print(bold(yellow(f"No license header ({len(missing)} files), NOT modified:")))
        for r in missing:
            print(f"  {yellow(r.path)}")
        print("Run with --add-missing to add the header to these files or add it by hand.\n")

    if errors:
        print(bold(red(f"Could not read ({len(errors)} files):")))
        for r in errors:
            print(f"  {red(r.path)}: {r.note}")
        print()

    print(f"Summary: {len(changes)} to update, {len(ok)} already correct, "
          f"{len(manual)} need manual review, {len(missing)} without a header, "
          f"{len(errors)} unreadable.")

    if manual or missing:
        print("\nExpected header for files owned by the Azahar project:")
        print(NEW_HEADER_TEMPLATE.format(years=args.year), end="")

    needs_attention = bool(manual or missing or errors)

    if not changes:
        return 1 if needs_attention else 0

    if args.read_only_run:
        print("\nRead only run: no files were modified.")
        return 1

    print()
    if not confirm():
        print(red("Aborted. No files were modified."))
        return 1

    print()
    for r in changes:
        write_result(root, r)
        print(green("updated: ") + r.path)
    print(f"\n{len(changes)} files updated. Manual review is recommended.")
    return 1 if needs_attention else 0


if __name__ == "__main__":
    sys.exit(main())
