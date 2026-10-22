#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Merge cmake_out/*/compile_commands.json into the SDK root for clangd."""

import argparse
import json
from pathlib import Path
import sys


def merge_databases(databases):
    merged = {}
    total = 0
    for database in databases:
        with database.open(encoding="utf-8") as source:
            try:
                entries = json.load(source)
            except ValueError as error:
                raise ValueError(f"{database}: {error}") from error
        if not isinstance(entries, list):
            raise ValueError(f"{database}: expected a JSON array")
        for index, entry in enumerate(entries):
            if (not isinstance(entry, dict)
                    or not isinstance(entry.get("directory"), str)
                    or not isinstance(entry.get("file"), str)
                    or not (isinstance(entry.get("command"), str)
                            or isinstance(entry.get("arguments"), list))):
                raise ValueError(f"{database}: invalid entry {index}")
            # Resolve aliases for deduplication; preserve the actual command
            # and its working directory so relative includes keep their meaning.
            source_file = (Path(entry["directory"]) / entry["file"]).resolve()
            merged.setdefault(source_file, entry)
        total += len(entries)
        print(f"{database.parent.name}: {len(entries)} entries")
    return list(merged.values()), total


def write_database(output, entries):
    # Replace the file itself, including an existing symlink, rather than
    # following it and overwriting a build's original database. A plain
    # open() keeps the umask mode; tempfile would create it 0600.
    temporary = output.with_name(f".{output.name}.tmp")
    try:
        with temporary.open("w", encoding="utf-8") as target:
            json.dump(entries, target, indent=2, ensure_ascii=False)
            target.write("\n")
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--prefer", metavar="BUILD",
        help="Prefer this cmake_out subdirectory for shared sources; "
             "otherwise use build directory name order")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    databases = sorted((root / "cmake_out").glob("*/compile_commands.json"))
    if not databases:
        parser.error(f"no compile_commands.json found under {root / 'cmake_out'}")
    if args.prefer:
        preferred = [path for path in databases if path.parent.name == args.prefer]
        if not preferred:
            parser.error(f"no database for {args.prefer}; available builds: "
                         + ", ".join(path.parent.name for path in databases))
        databases.remove(preferred[0])
        databases.insert(0, preferred[0])

    output = root / "compile_commands.json"
    try:
        entries, total = merge_databases(databases)
        write_database(output, entries)
    except (OSError, ValueError) as error:
        print(f"merge_compile_commands: {error}", file=sys.stderr)
        return 1
    print(f"Merged {len(databases)} databases: {total} entries -> "
          f"{len(entries)} sources ({total - len(entries)} duplicates removed)")
    print(f"Written {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
