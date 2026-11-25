# 0008. Contract JSON: ids as strings, strict parsing, core fields in the IDL

- Status: Accepted
- Date: 2026-11-25

## Context

M2 adds commands and missions. That needs three things the M1 codegen did not
have: 64-bit identifiers that a browser can carry, a way for the gateway to
turn a client's JSON into a DDS sample, and one place that says which fields
are core. Until now `docs/contract.md` kept the core/extended split in
hand-written tables next to the generated ones.

## Decision

I will keep all three in the IDL and let `tools/codegen/idl2gen.py` generate
the rest.

- **64-bit ids are decimal strings.** A field with `// unit: id` (only allowed
  on `uint64`) is a JSON string and a TypeScript `string`. `command_id` is
  random over the whole 64 bits, so a JSON number would silently lose its low
  bits in the browser and the id would no longer match the DDS key. Every other
  integer stays a number. `t_bridge_ns` also exceeds 2^53 but is only compared
  approximately, so it is unchanged.
- **`from_json_value` is strict.** It wants an object with exactly the
  struct's fields. A missing or unknown key, a float where an integer goes, an
  out-of-range integer, an over-long string, an unknown enum literal or an
  over-long sequence all throw `JsonError("<path>: <problem>")`. The gateway
  takes commands from a browser, so a typo should be an error the user sees,
  not a default that goes to a vehicle. The `<path>: ` prefix and the texts
  `unknown literal` and `more than N elements` are stable, so callers can map
  them to a status.
- **Sequences are bounded and hold structs.** `sequence<Waypoint, 500>` maps to
  `std::vector` in C++ and `Waypoint[]` in TypeScript. The bound is checked
  when parsing, so the gateway cannot be made to build an oversized sample.
- **`core` lives in the IDL.** `// unit: m, core` marks a field. The generator
  writes `kCore` and `kAll` masks next to the valid bits and a class column in
  `docs/contract/v1-fields.md`. `docs/contract.md` links to those tables
  instead of repeating them.
- **Enum fields are `Literal | null` in TypeScript.** The JSON side already
  wrote `null` for a value this build does not know; the type now says so.

## Consequences

The M1 wire format is unchanged: no existing field is an id, so every M1 field
serialises as before. The station's types changed for enum fields, and the few
call sites that took an enum or `undefined` now also accept `null`. Adding a
field now means editing the IDL and rerunning the tool, including its core
marker, and `--check` in CI catches a stale output. The parser is strict about
what it accepts from a client, which also means an old client that omits a
newly appended field is rejected until it is updated.
