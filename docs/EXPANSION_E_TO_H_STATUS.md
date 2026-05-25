# Expansion Blocks E–H status

## Expansion Block E — Low-level Ring / Line / Polygon APIs

Implemented as read-only borrowed wrappers:

- `TG::Geometry::Geom#point`
- `TG::Geometry::Geom#line`
- `TG::Geometry::Geom#polygon`
- `TG::Geometry::Line`
- `TG::Geometry::Ring`
- `TG::Geometry::Polygon`

Invariant satisfied: child pointers are borrowed from the parent TG geometry and the wrapper keeps the parent Ruby `TG::Geometry::Geom` alive through `geom_owner` with compaction-aware marking.

Tests: `spec/expansion_e_low_level_geometry_spec.rb` covers accessors, private allocation, out-of-range errors, and parent survival after `GC.start` / `GC.compact`.

## Expansion Block F — Callback/search APIs

OPEN QUESTION: not implemented.

Reason: the roadmap allows callback/search APIs only with a new callback safety contract. The current contract still forbids public callback/block APIs in the first release. No `geom.search`, `index.search`, or `each_match` method is exposed.

Required before implementation:

- explicit callback exception propagation semantics;
- GVL behavior for Ruby callbacks inside C loops;
- proof that borrowed pointers cannot be invalidated by callback reentrancy;
- benchmark of callback overhead;
- tests for exceptions raised from callbacks.

## Expansion Block G — Fast no-allocation point query optimization

OPEN QUESTION: not implemented.

Reason: the current implementation intentionally constructs a temporary TG point geometry for `covers` / `contains` correctness. Replacing this with `tg_geom_intersects_xy` or a specialized helper requires proof of exact boundary semantics and a benchmark proving benefit.

Current invariant: point query semantics remain exact and boundary behavior is covered by existing tests.

## Expansion Block H — Geodesic helpers or projection integration

OPEN QUESTION: not implemented.

Reason: TG core remains planar XY. No optional geodesic/projection dependency has been approved. The gem continues to document that `length`, `area`, and `perimeter` are in input coordinate units, not meters for lon/lat.

Required before implementation:

- explicit optional dependency decision;
- public API shape;
- tests separating planar TG behavior from geodesic/projection helpers;
- documentation that TG itself does not handle geodesics.
