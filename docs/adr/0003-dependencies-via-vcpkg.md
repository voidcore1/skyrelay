# 0003. Dependencies via vcpkg with a pinned baseline

- Status: Accepted
- Date: 2026-11-23

## Context

SkyRelay depends on Boost, Cyclone DDS and its C++ binding, JSON and YAML
libraries, a CLI parser, a logger and GoogleTest. The builds must be
reproducible across Ubuntu 24.04 and 26.04 and must eventually work without a
network connection. Distribution packages differ between those releases, and
neither carries Cyclone DDS 11.1, the version I am pinning. Cyclone also needs
its `idlc` and `idlcxx` code generators available to CMake at build time.

## Decision

I will use vcpkg in manifest mode.

- `vcpkg.json` lists the direct dependencies and sets `builtin-baseline` to a
  single vcpkg commit. CI bootstraps vcpkg at that same commit.
- `vcpkg-configuration.json` lists `cmake/vcpkg-ports` as an overlay port
  directory. It holds `cyclonedds` and `cyclonedds-cxx` at 11.1.0, copied from
  the registry ports at the baseline and adapted: the release tarball and its
  SHA512 are updated, patches that upstream has absorbed are dropped, and
  DDS-Security is not enabled yet. The overlays can be deleted once the
  registry catches up.
- Presets locate vcpkg through `VCPKG_ROOT`, falling back to `~/vcpkg`.
- Third-party include directories are added as SYSTEM includes so the strict
  warning set (`-Werror -Wconversion`) applies only to SkyRelay's own code.

Offline plan: the binary cache (`~/.cache/vcpkg/archives`) is persisted in CI
and can be archived. Source downloads can be mirrored through vcpkg's asset
cache (`X_VCPKG_ASSET_SOURCES`) or pre-filled in `downloads/`. Later I will
prove the build with `--network=none` against such a mirror.

## Consequences

- Builds are reproducible from one commit hash, and updating a dependency is a
  reviewable change to the baseline.
- The first build compiles Boost and Cyclone from source and is slow; the
  binary cache removes that cost afterwards.
- The overlay ports are my responsibility until the registry carries 11.1.
  Enabling DDS-Security later means turning on the `security` and `ssl`
  features in the overlay.
- A system libtool or similar tools may be required by some ports, so the CI
  images install them.
