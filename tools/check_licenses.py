#!/usr/bin/env python3
"""Fail if a vcpkg port installed for SkyRelay is under the GPL or AGPL.

SkyRelay is Apache-2.0 and links only permissively licensed code. vcpkg
installs each port's licence text as share/<port>/copyright, so this reads
every one of those and flags a port whose text carries

- the title of the GPL or AGPL licence text: "GNU GENERAL PUBLIC LICENSE",
  "GNU AFFERO";
- the standard GPL notice, for ports that ship it without the licence text:
  "GNU General Public License as published by";
- or an SPDX identifier for either, such as "GPL-2.0-or-later" or "AGPL-3.0".

Whitespace is collapsed first, so a notice wrapped across lines still matches.
The phrases are matched case-sensitively, as the official texts print them.
That keeps the LGPL ("GNU LESSER GENERAL PUBLIC LICENSE", "GNU Lesser General
Public License as published by") and licences that only mention the GPL in
passing, like the EPL-2.0's "Secondary License" clause in Cyclone DDS, out of
the result.

    tools/check_licenses.py build/dev/vcpkg_installed/x64-linux/share
    tools/check_licenses.py <share> --allow <port>   # a documented exception

Exit status: 0 if nothing is flagged (or everything flagged is allowed),
1 if a port is flagged, 2 if the directory holds no copyright files at all,
which almost always means a wrong path.
"""
import argparse
import re
import sys
from pathlib import Path

MARKERS = (
    ("GNU GENERAL PUBLIC LICENSE", re.compile(r"GNU GENERAL PUBLIC LICENSE")),
    ("GNU AFFERO", re.compile(r"GNU AFFERO")),
    ("GPL notice", re.compile(r"GNU General Public License as published by")),
    # \b keeps LGPL-2.1 and LGPL-3.0 out: there is no word boundary inside "LGPL".
    ("SPDX GPL/AGPL identifier", re.compile(r"\bA?GPL-[0-9]")),
)


def find_markers(text):
    """Names of the GPL markers found in one copyright text."""
    text = " ".join(text.split())
    return [name for name, pattern in MARKERS if pattern.search(text)]


def scan(share):
    """{port: [marker, ...]} for every port under share with a copyright file."""
    results = {}
    for copyright_file in sorted(share.glob("*/copyright")):
        text = copyright_file.read_text(encoding="utf-8", errors="replace")
        results[copyright_file.parent.name] = find_markers(text)
    return results


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("share", type=Path, help="vcpkg_installed/<triplet>/share")
    parser.add_argument("--allow", action="append", default=[], metavar="PORT",
                        help="port to accept even if flagged (repeatable)")
    args = parser.parse_args(argv)

    results = scan(args.share) if args.share.is_dir() else {}
    if not results:
        print(f"check_licenses: no */copyright files under {args.share}", file=sys.stderr)
        return 2

    allowed = set(args.allow)
    failed = []
    for port, markers in results.items():
        if not markers:
            continue
        verdict = "allowed" if port in allowed else "FAIL"
        print(f"{verdict} {port}: {', '.join(markers)} in {args.share / port / 'copyright'}")
        if port not in allowed:
            failed.append(port)
    for port in sorted(allowed - {p for p, m in results.items() if m}):
        print(f"note: --allow {port} matched nothing; remove it if it is no longer needed")

    print(f"check_licenses: {len(results)} ports scanned in {args.share}, "
          f"{len(failed)} under the GPL or AGPL")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
