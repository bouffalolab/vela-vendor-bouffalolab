#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read the primary firmware partition size used by the BL616CL image tools."""

import argparse
import sys

try:
    import tomllib
except ImportError:
    try:
        import tomli as tomllib
    except ImportError:
        sys.exit("app_partition_size: Python < 3.11 requires tomli; run "
                 "python3 -m pip install -r "
                 "vendor/bouffalolab/tools/bl616cl/requirements.txt")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("partition", help="Board partition TOML file")
    args = parser.parse_args()
    try:
        with open(args.partition, encoding="utf-8") as source:
            config = tomllib.loads(source.read())
        entries = [entry for entry in config["pt_entry"]
                   if entry.get("name") == "FW"]
        if len(entries) != 1:
            raise ValueError("expected exactly one FW partition")
        size = entries[0]["size0"]
        if type(size) is not int or size <= 0:
            raise ValueError("FW size0 must be a positive integer")
        print(size)
    except (OSError, KeyError, ValueError, TypeError) as error:
        print(f"app_partition_size: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
