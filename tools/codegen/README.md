# Contract codegen

`idl2gen.py` reads `idl/skyrelay/v1/types.idl` and writes everything that has to
agree with it, so the data contract lives in exactly one place:

| Output | Used by |
|---|---|
| `lib/dds/include/skyrelay/dds/gen/v1_json.hpp` | the gateway: `to_json_value()` for every struct and enum |
| `lib/dds/include/skyrelay/dds/gen/v1_valid_bits.hpp` | the vehicle-state to DDS mapping: `skyrelay::v1::valid::Pose::lat_deg` etc. |
| `web/station/src/contract/v1.ts` | the web station: interfaces, enum unions, `PoseValid` bit masks |
| `docs/contract/v1-fields.md` | the docs: one field table per struct |

```
python3 tools/codegen/idl2gen.py           # rewrite the generated files
python3 tools/codegen/idl2gen.py --check   # exit 1 and name any stale file
python3 -m unittest tests/unit/tools/test_idl2gen.py
```

CI runs `--check`, so edit the IDL, rerun the tool and commit the result
together. The files are generated deterministically (no timestamps).

## What it accepts

Only the subset the IDL rules allow: modules, `@appendable` and `@key`,
structs, enums, the primitive types, `string<N>`, and nested struct members.
Unions, sequences, maps, typedefs and `@optional` are errors with a line
number. Every data field needs a trailing `// unit: <unit>` comment on the
same line, because the field table reads it.

## Conventions

- `valid_mask` bit *i* is the *i*-th field after `header` (the `@key` field is
  skipped). The bit numbers come from declaration order, so the IDL stays
  append-only.
- NaN floats become JSON `null`; the TypeScript type for them is
  `number | null`. Enums become their literal name; a value this build does not
  know (a newer peer) becomes `null`.
- The valid-bit constants are one struct of `static constexpr uint64_t` per
  IDL struct inside `skyrelay::v1::valid`, so two structs may share a field
  name (`valid::Pose::lat_deg`, `valid::LinkStatus::state`).
- 64-bit integers are JSON numbers, so JavaScript loses precision above 2^53.
  `t_bridge_ns` is already past that, so the web station should treat it as
  approximate. Bit masks in TypeScript stop at bit 52 and the tool refuses more.
