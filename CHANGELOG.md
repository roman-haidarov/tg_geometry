# Changelog

## 0.1.0 - unreleased

Initial release-core implementation for `tg_geometry`.

### Added

- Canonical public require path: `require "tg/geometry"`.
- Public namespace: `TG::Geometry`.
- Native extension build through `ext/tg_geometry/extconf.rb`.
- Vendored `tidwall/tg` and `tidwall/rtree.c` sources with pinned `VERSION` files and upstream license files.
- Error classes:
  - `TG::Geometry::Error`
  - `TG::Geometry::ParseError`
  - `TG::Geometry::ArgumentError < ::ArgumentError`
  - `TG::Geometry::FrozenIndexError`
- Immutable `TG::Geometry::Geom` parsing for GeoJSON, WKT, WKB, Hex, GeoBIN, and auto format detection.
- `TG::Geometry::Geom` methods:
  - `#type`
  - `#bbox`
  - `#covers_xy?`
  - `#contains?`
  - `#intersects?`
  - `#to_geojson`
  - `#to_wkt`
  - `#to_wkb`
- Immutable `TG::Geometry::Rect` API.
- Immutable `TG::Geometry::Index.build` with strict `[[id, object], ...]` entry format.
- Index ingestion modes:
  - `via: :geom` borrowed geometry with `geom_owner` lifetime protection;
  - `via: :geojson` owned geometry;
  - `via: :wkb` owned geometry.
- Index strategies:
  - `:flat`
  - `:rtree`
- Deterministic insertion-order results for flat and rtree queries.
- Exact rtree memory accounting through a custom malloc/free allocator with headers.
- Native-endian packed point batch API: `TG::Geometry::Index#covering_ids_batch_packed`.
- Debug-only test hooks under `TG_DEBUG_TEST=1` for allocation failure simulation and byte counter inspection.
- Block 14 memory/GC/compaction hardening specs.
- Benchmark harnesses:
  - `benchmark/parse_throughput.rb`
  - `benchmark/gvl_threshold.rb`
  - `benchmark/flat_vs_rtree.rb`
  - `benchmark/batch_packed_vs_loop.rb`
  - `benchmark/falcon_concurrency.rb`
  - `benchmark/objectspace_memsize.rb`
  - `benchmark/rss_stability.rb`
- Documentation:
  - `docs/ARCHITECTURE.md`
  - `docs/MEMORY_OWNERSHIP.md`
  - `docs/CONCURRENCY.md`
  - `docs/ERROR_HANDLING.md`
  - `docs/BENCHMARKING.md`
  - `docs/LIMITATIONS.md`
  - `docs/RELEASE_CHECKLIST.md`

### Not included

- `strategy: :auto` is not part of the release-core contract; it is tracked as Expansion Block A below.
- No Ractor support claim.
- No no-GVL execution.
- No full GIS, routing, geocoding, projections, geodesic helpers, nearest POI index, or result-geometry overlay operations.
- No public performance claims until benchmark results are produced by this gem.

### OPEN QUESTION

- Final ASAN setup requires Roman approval before replacing the placeholder CI job.
- Final Valgrind setup requires Roman approval before replacing the placeholder CI job.

## Unreleased

### Added

- Expansion Block A status: `strategy: :auto` remains postponed for the first public release; explicit `:flat` / `:rtree` strategies are required.
- Expansion Block B: `TG::Geometry::Registry` Ruby helper for immutable Index reload/swap workflows.
- Expansion Block C: optional `TG::Geometry::ActiveRecordSource` helper that converts relation-like records into strict `[[id, object], ...]` entries without adding a Rails dependency.
- Expansion Block D: `TG::Geometry.parse_hex`, `TG::Geometry.parse_geobin`, `TG::Geometry::Geom#to_hex`, `#to_geobin`, and `#extra_json`.
- Expansion Block E: read-only borrowed low-level wrappers: `TG::Geometry::Line`, `TG::Geometry::Ring`, `TG::Geometry::Polygon`, plus `TG::Geometry::Geom#point`, `#line`, and `#polygon`.
- Expansion Block I: Ractor unsupported-boundary investigation documented in `docs/RACTOR.md` with specs asserting native wrappers are not treated as shareable Ractor objects.
- Expansion Block J grouped API coverage:
  - safe point and empty geometry constructors;
  - additional `TG::Geometry::Geom` predicates;
  - geometry metadata and Z/M read accessors;
  - MultiPoint/MultiLineString/MultiPolygon and GeometryCollection accessors;
  - borrowed child `TG::Geometry::Geom` wrappers with `geom_owner`;
  - value `TG::Geometry::Segment` wrappers from Line/Ring segment accessors.

### Fixed

- Corrected `benchmark/gvl_threshold.rb` so each target size uses a valid WKT payload near that size instead of repeatedly benchmarking the same tiny polygon.

### Documentation

- Added docs for Registry, ActiveRecord source helper, additional format coverage, low-level borrowed geometry wrappers, Ractor unsupported-boundary status, grouped full TG API coverage, Auto Strategy postponed status, and Expansion Blocks E–H status.

### OPEN QUESTION

- Expansion Block F callback/search APIs remain blocked until a callback safety contract, exception semantics, GVL rules, and callback overhead benchmarks are approved.
- Expansion Block G no-allocation point query optimization remains blocked until boundary/hole-boundary equivalence tests and benchmarks prove it preserves `:covers` / `:contains` semantics.
- Expansion Block H geodesic/projection helpers remain blocked until an explicit optional dependency/API decision is approved.
- Remaining Expansion Block J scope such as Line/Ring/Polygon constructors, callback/search APIs, nearest segment APIs, global environment configuration, and allocator override APIs remains blocked until separate ownership/thread-safety contracts are approved.
