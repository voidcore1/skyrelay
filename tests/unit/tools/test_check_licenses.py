import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import check_licenses  # noqa: E402

GPL_TITLE = "GNU GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007\n"
LGPL_TITLE = (
    "GNU LESSER GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007\n\n"
    "This version of the GNU Lesser General Public License incorporates the terms\n"
    "and conditions of version 3 of the GNU General Public License.\n"
)
# Cyclone DDS ships the EPL-2.0, whose definition of a "Secondary License" names
# the GPL without granting it.
EPL_SECONDARY = (
    "Eclipse Public License - v 2.0\n\n"
    '"Secondary License" means either the GNU General Public License,\n'
    "Version 2.0, or any later versions of that license.\n"
)


def make_share(tmp, ports):
    share = Path(tmp) / "share"
    for name, text in ports.items():
        (share / name).mkdir(parents=True)
        (share / name / "copyright").write_text(text)
    # A CMake config directory with no copyright file, as vcpkg installs them.
    (share / "foo_config").mkdir(parents=True, exist_ok=True)
    return share


def run(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = check_licenses.main(argv)
    return rc, out.getvalue() + err.getvalue()


class CheckLicenses(unittest.TestCase):
    def test_flags_gpl(self):
        with tempfile.TemporaryDirectory() as tmp:
            share = make_share(tmp, {"foo": GPL_TITLE, "bar": "MIT License\n"})
            rc, output = run([str(share)])
            self.assertEqual(rc, 1)
            self.assertIn("foo", output)
            self.assertNotIn("bar:", output)

    def test_flags_agpl(self):
        with tempfile.TemporaryDirectory() as tmp:
            share = make_share(tmp, {"foo": "GNU AFFERO GENERAL PUBLIC LICENSE\n"})
            rc, output = run([str(share)])
            self.assertEqual(rc, 1)
            self.assertIn("foo", output)

    def test_flags_spdx_identifier(self):
        with tempfile.TemporaryDirectory() as tmp:
            share = make_share(tmp, {"foo": "SPDX-License-Identifier: GPL-2.0-or-later\n"})
            rc, output = run([str(share)])
            self.assertEqual(rc, 1)
            self.assertIn("foo", output)

    def test_flags_gpl_notice_without_licence_text(self):
        # Many projects ship only the short notice, often wrapped across lines.
        notices = [
            "under the terms of the GNU General Public License as published by\n"
            "the Free Software Foundation, either version 3 of the License.\n",
            "under the terms of the GNU General Public License\n"
            "as published by the Free Software Foundation.\n",
        ]
        for notice in notices:
            with self.subTest(notice=notice), tempfile.TemporaryDirectory() as tmp:
                share = make_share(tmp, {"foo": notice})
                rc, output = run([str(share)])
                self.assertEqual(rc, 1)
                self.assertIn("foo", output)

    def test_lgpl_notice_not_flagged(self):
        with tempfile.TemporaryDirectory() as tmp:
            share = make_share(tmp, {"foo": (
                "This library is free software; you can redistribute it and/or modify it\n"
                "under the terms of the GNU Lesser General Public License as published by\n"
                "the Free Software Foundation; either version 2.1 of the License.\n")})
            rc, _ = run([str(share)])
            self.assertEqual(rc, 0)

    def test_lgpl_not_flagged(self):
        with tempfile.TemporaryDirectory() as tmp:
            share = make_share(tmp, {"foo": LGPL_TITLE + "SPDX-License-Identifier: LGPL-3.0\n"})
            rc, _ = run([str(share)])
            self.assertEqual(rc, 0)

    def test_gpl_named_as_secondary_licence_not_flagged(self):
        with tempfile.TemporaryDirectory() as tmp:
            share = make_share(tmp, {"cyclonedds": EPL_SECONDARY})
            rc, _ = run([str(share)])
            self.assertEqual(rc, 0)

    def test_allow_list(self):
        with tempfile.TemporaryDirectory() as tmp:
            share = make_share(tmp, {"foo": GPL_TITLE, "bar": "MIT License\n"})
            rc, output = run([str(share), "--allow", "foo"])
            self.assertEqual(rc, 0)
            self.assertIn("foo", output)

    def test_allow_list_covers_only_named_ports(self):
        with tempfile.TemporaryDirectory() as tmp:
            share = make_share(tmp, {"foo": GPL_TITLE, "baz": GPL_TITLE})
            rc, output = run([str(share), "--allow", "foo"])
            self.assertEqual(rc, 1)
            self.assertIn("baz", output)

    def test_no_copyright_files_is_an_error(self):
        # A wrong path must not pass the check by finding nothing to check.
        with tempfile.TemporaryDirectory() as tmp:
            rc, _ = run([str(Path(tmp) / "missing")])
            self.assertEqual(rc, 2)
            empty = Path(tmp) / "share"
            empty.mkdir()
            rc, _ = run([str(empty)])
            self.assertEqual(rc, 2)


if __name__ == "__main__":
    unittest.main()
