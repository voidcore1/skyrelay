import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "codegen"))
import idl2gen  # noqa: E402

SAMPLE_IDL = """
module a {
@appendable
struct Header { uint64 seq; // unit: -
};
@appendable
enum Kind { KIND_A, KIND_B };
@appendable
struct Pose {
  @key uint32 vehicle_id;  // unit: -
  Header header;
  double lat_deg;          // unit: deg
  Kind kind;               // unit: -
  string<8> name;          // unit: -
};
};
"""


class ParseRestricted(unittest.TestCase):
    def test_struct_fields_and_bits(self):
        m = idl2gen.parse(SAMPLE_IDL)
        s = m.struct("Pose")
        self.assertEqual([f.name for f in s.data_fields], ["lat_deg", "kind", "name"])
        self.assertEqual([f.bit for f in s.data_fields], [0, 1, 2])
        self.assertEqual(s.data_fields[0].unit, "deg")
        self.assertTrue(s.fields[0].key)

    def test_enum_literals(self):
        m = idl2gen.parse(SAMPLE_IDL)
        self.assertEqual(m.enum("Kind").literals, ["KIND_A", "KIND_B"])

    def test_rejects_union(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line 3: union"):
            idl2gen.parse("module a {\n\n union U switch(long) { case 1: long x; };\n};")

    def test_rejects_stray_close_brace(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line 3: unexpected"):
            idl2gen.parse("module a {\n};\n}\n")

    def test_rejects_trailing_tokens(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line 3: "):
            idl2gen.parse("module a {\n};\nfoo\n")

    def test_shared_field_names_across_structs(self):
        idl = SAMPLE_IDL.replace("};\n};\n", """};
@appendable
struct Other {
  @key uint32 vehicle_id;  // unit: -
  Header header;
  float x;                 // unit: -
  double lat_deg;          // unit: deg
};
};
""")
        out = idl2gen.generate(idl2gen.parse(idl))
        self.assertIn("struct Other {", out[idl2gen.VALID_HPP])
        self.assertIn("static constexpr uint64_t lat_deg = 1ull << 1;", out[idl2gen.VALID_HPP])

    def test_rejects_optional(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line 3: .*optional"):
            idl2gen.parse("module a {\n@appendable\nstruct S { @optional long x; };\n};")

    def test_rejects_unknown_type(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line 3: unknown type"):
            idl2gen.parse("module a {\n@appendable\nstruct S { Nope x; // unit: -\n};\n};")

    def test_struct_without_appendable_is_error(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line 3: struct S must be annotated @appendable"):
            idl2gen.parse("module a {\n\nstruct S { long x; // unit: -\n};\n};")
        # @key alone is not enough, and the line is the struct's own.
        with self.assertRaisesRegex(idl2gen.IdlError, r"line 5: struct T must be annotated @appendable"):
            idl2gen.parse("module a {\n@appendable\nstruct S { long x; // unit: -\n};\nstruct T { long y; // unit: -\n};\n};")

    def test_missing_unit_is_error(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line 3: .*unit"):
            idl2gen.parse("module a {\n@appendable\nstruct S { long x;\n};\n};")


SEQ_IDL = """
module a {
@appendable
struct Header { uint64 seq; // unit: -
};
@appendable
enum Kind { KIND_A, KIND_B };
@appendable
struct W {
  double lat_deg;          // unit: deg
};
@appendable
struct Plan {
  @key uint32 vehicle_id;  // unit: -
  Header header;
  sequence<W, 500> waypoints;   // unit: -
  Kind mode;                    // unit: -
  uint64 command_id;            // unit: id
  double a;                     // unit: deg, core
  double b;                     // unit: deg
};
};
"""


def seq_idl(member_line):
    return SEQ_IDL.replace("  sequence<W, 500> waypoints;   // unit: -\n", member_line + "\n")


class Sequences(unittest.TestCase):
    def test_bounded_sequence_of_struct(self):
        f = idl2gen.parse(SEQ_IDL).struct("Plan").fields[2]
        self.assertEqual((f.kind, f.elem, f.bound), ("sequence", "W", 500))
        out = idl2gen.generate(idl2gen.parse(SEQ_IDL))
        self.assertIn("waypoints: W[];", out[idl2gen.TS_PATH])
        self.assertIn("`sequence<W, 500>`", out[idl2gen.MD_PATH])

    def test_unbounded_sequence_rejected(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line \d+: sequence must be bounded"):
            idl2gen.parse(seq_idl("  sequence<W> waypoints;   // unit: -"))

    def test_sequence_of_primitive_rejected(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line \d+: sequence element must be a struct"):
            idl2gen.parse(seq_idl("  sequence<long, 4> waypoints;   // unit: -"))

    def test_json_converters_for_sequences(self):
        out = idl2gen.generate(idl2gen.parse(SEQ_IDL))[idl2gen.JSON_HPP]
        self.assertIn("more than 500 elements", out)
        self.assertIn("unknown literal", out)
        self.assertIn("class JsonError : public std::invalid_argument", out)


class Units(unittest.TestCase):
    def test_id_unit_only_on_uint64(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line \d+: unit 'id' needs uint64"):
            idl2gen.parse(SEQ_IDL.replace("double b;                     // unit: deg",
                                          "uint32 b;                     // unit: id"))

    def test_id_is_ts_string(self):
        out = idl2gen.generate(idl2gen.parse(SEQ_IDL))
        self.assertIn("command_id: string;", out[idl2gen.TS_PATH])
        self.assertIn("std::to_string", out[idl2gen.JSON_HPP])

    def test_core_marker(self):
        m = idl2gen.parse(SEQ_IDL)
        s = m.struct("Plan")
        self.assertTrue(next(f for f in s.fields if f.name == "a").core)
        self.assertFalse(next(f for f in s.fields if f.name == "b").core)
        self.assertEqual(next(f for f in s.fields if f.name == "a").unit, "deg")
        out = " ".join(idl2gen.generate(m)[idl2gen.VALID_HPP].split())
        self.assertIn("kCore = (1ull << 3);", out)
        self.assertIn("kAll = (1ull << 0) | (1ull << 1) | (1ull << 2) | (1ull << 3) | (1ull << 4);", out)

    def test_unknown_unit_option_rejected(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line \d+: unknown unit option 'cor'"):
            idl2gen.parse(SEQ_IDL.replace("unit: deg, core", "unit: deg, cor"))

    def test_core_needs_a_valid_bit(self):
        with self.assertRaisesRegex(idl2gen.IdlError, r"line \d+: .*core.*no valid bit"):
            idl2gen.parse(SEQ_IDL.replace("uint32 vehicle_id;  // unit: -", "uint32 vehicle_id;  // unit: -, core"))

    def test_ts_enum_fields_nullable(self):
        out = idl2gen.generate(idl2gen.parse(SEQ_IDL))
        self.assertIn("mode: Kind | null;", out[idl2gen.TS_PATH])

    def test_md_class_column(self):
        out = idl2gen.generate(idl2gen.parse(SAMPLE_IDL.replace("// unit: deg", "// unit: deg, core")))[idl2gen.MD_PATH]
        self.assertIn("| field | type | bit | unit | class |", out)
        self.assertIn("| `lat_deg` | `double` | 0 | deg | core |", out)
        self.assertIn("| `kind` | `Kind` | 1 | - | extended |", out)
        self.assertIn("| `vehicle_id` | `uint32` | - | - | - |", out)


class Generate(unittest.TestCase):
    def test_outputs_have_header_and_are_deterministic(self):
        idl = (ROOT / "idl/skyrelay/v1/types.idl").read_text()
        a = idl2gen.generate(idl2gen.parse(idl))
        b = idl2gen.generate(idl2gen.parse(idl))
        self.assertEqual(a, b)
        for path, text in a.items():
            self.assertIn("Generated by tools/codegen/idl2gen.py", text.splitlines()[0], path)

    def test_pose_heading_is_bit_11(self):
        idl = (ROOT / "idl/skyrelay/v1/types.idl").read_text()
        out = idl2gen.generate(idl2gen.parse(idl))
        self.assertIn("static constexpr uint64_t heading_deg = 1ull << 11", out[idl2gen.VALID_HPP])


class Check(unittest.TestCase):
    def run_tool(self, root, *args):
        return subprocess.run(
            [sys.executable, str(ROOT / "tools/codegen/idl2gen.py"), "--root", str(root), *args],
            capture_output=True, text=True)

    def test_check_detects_stale(self):
        with tempfile.TemporaryDirectory() as d:
            idl = Path(d) / "idl/skyrelay/v1/types.idl"
            idl.parent.mkdir(parents=True)
            shutil.copy(ROOT / "idl/skyrelay/v1/types.idl", idl)
            self.assertEqual(self.run_tool(d).returncode, 0)
            self.assertEqual(self.run_tool(d, "--check").returncode, 0)
            target = Path(d) / idl2gen.TS_PATH
            target.write_text(target.read_text() + "// edited\n")
            r = self.run_tool(d, "--check")
            self.assertEqual(r.returncode, 1)
            self.assertIn(idl2gen.TS_PATH, r.stderr)

    def test_check_detects_missing(self):
        with tempfile.TemporaryDirectory() as d:
            idl = Path(d) / "idl/skyrelay/v1/types.idl"
            idl.parent.mkdir(parents=True)
            shutil.copy(ROOT / "idl/skyrelay/v1/types.idl", idl)
            r = self.run_tool(d, "--check")
            self.assertEqual(r.returncode, 1)

    def test_committed_files_are_current(self):
        r = self.run_tool(ROOT, "--check")
        self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == "__main__":
    unittest.main()
