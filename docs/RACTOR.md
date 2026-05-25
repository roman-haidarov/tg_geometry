# Ractor investigation

This document covers Expansion Block I.

## Current result

`tg_geometry` does not claim Ractor support.

The current native wrappers are frozen and immutable for normal Ruby thread reads, but they are not Ractor-shareable Ruby objects. The test suite records this boundary with `Ractor.shareable?` and `Ractor.make_shareable` checks for:

- `TG::Geometry::Geom`
- `TG::Geometry::Rect`
- `TG::Geometry::Index`

This is an explicit unsupported boundary, not a partial support claim.

## Shareability rules

Current rule for public docs and code comments:

- Normal Ruby thread read-only use is supported by immutable object design and tests.
- Ractor support is not supported and not advertised.
- Do not use `TG::Geometry::Geom`, `TG::Geometry::Rect`, `TG::Geometry::Index`, or borrowed low-level wrappers as cross-Ractor shareable objects.
- Do not add Ractor-specific code paths without a new explicit design and tests.

## Why no support claim is made

The extension stores native pointers and Ruby `VALUE` references inside TypedData wrappers. The current contract validates GC marking, compaction, ownership, and normal thread read-only access. It does not define or validate cross-Ractor transfer/share semantics for those wrappers.

## Required before changing this status

Before any future Ractor support claim:

1. Define shareability rules for owned and borrowed `TG::Geometry::Geom` wrappers.
2. Define shareability rules for `TG::Geometry::Index` entries and id `VALUE`s.
3. Define behavior for borrowed Line/Ring/Polygon/Segment wrappers.
4. Add Ractor tests that pass on the supported Ruby matrix.
5. Document whether objects are shareable directly, require duplication, or are unsupported.

Until those items exist, Ractor remains unsupported.
