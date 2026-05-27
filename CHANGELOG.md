# Changelog

## 0.3.0 - 27.05.2026

### Added

- `TG::Geometry.line_string`, `TG::Geometry.polygon`, and `TG::Geometry.multi_polygon` constructors from Ruby arrays.
- `TG::Geometry::Geom#srid` with automatic EWKB SRID extraction in `parse_wkb` and `parse_hex`.
- `TG::Geometry::Geom#to_ewkb` writer with explicit `srid:` override.
- `TG::Geometry::Index#intersecting_geom_ids`, `#covering_geom_ids`, and `#containing_geom_ids`.
- `TG::Geometry::Line#nearest_segment` and `TG::Geometry::Ring#nearest_segment` with `TG::Geometry::NearestSegment` result objects.
- `TG::Geometry::ActiveRecordType` read-only optional require.
- PostGIS/EWKB fixture suite in `spec/fixtures/postgis/`.

### Fixed

- Reject unsupported/unknown keywords on v0.3.0 APIs instead of silently ignoring them, including non-goals such as `release_gvl:`, `parse_wkb(srid:)`, and `parse_wkb(ewkb:)`.
- Harden constructor cleanup paths so intermediate C `tg_ring` / `tg_poly` objects are released when Ruby exceptions occur during nested polygon construction.
- Keep geometry-index query collection in a C-only status-returning phase before Ruby result materialization, preserving the intended no-GVL-safe internal shape.

### Clarified

- `Index.build(predicate:)` affects only legacy point query methods: `find_covering`, `covering_ids(x, y)`, and `covering_ids_batch_packed`.

### Not included

- Geodesic / Haversine distance.
- Projection / reprojection.
- Buffer / union / difference / convex hull.
- Index nearest_ids (KNN).
- GeoBIN bbox helpers.
- Streaming FeatureSource.
- Index serialization / mmap.
- Ractor support.
- Windows / JRuby.
- Write-side ActiveRecordType / AR scopes / migrations.
- Z/M variants of constructors.
- Public `release_gvl:` option.

## 0.1.0 - unreleased

Initial public release candidate for `tg_geometry`.

### Added

- Canonical public require path: `require "tg/geometry"`.
- Public namespace: `TG::Geometry`.
- Native extension build through `ext/tg_geometry/extconf.rb`.
- Vendored `tidwall/tg`, `tidwall/rtree.c`, and `tidwall/json.c` sources with pinned `VERSION` files and upstream license files.
- Error classes:
  - `TG::Geometry::Error`
  - `TG::Geometry::ParseError`
  - `TG::Geometry::ArgumentError < ::ArgumentError`
  - `TG::Geometry::FrozenIndexError`
- Immutable `TG::Geometry::Geom` parsing for GeoJSON, WKT, WKB, Hex, GeoBIN, and auto format detection.
- Immutable `TG::Geometry::Rect` API.
- Immutable `TG::Geometry::Index.build` with strict `[[id, object], ...]` entry format.
- Index ingestion modes: `via: :geom`, `via: :geojson`, and `via: :wkb`.
- Index strategies: `:flat` and `:rtree`.
- Deterministic insertion-order results for flat and rtree queries.
- Exact rtree memory accounting through a custom malloc/free allocator with allocation headers.
- Native-endian packed point batch API: `TG::Geometry::Index#covering_ids_batch_packed`.
- GeoJSON FeatureSource APIs for reading FeatureCollection entries/features and building an Index directly from file, JSON string, or IO.
- FeatureSource bulk execution now runs file read, JSON traversal, and TG geometry parsing in a C-only no-GVL phase before Ruby materialization / Index ownership transfer. The implementation uses Ruby VM no-GVL APIs only: `RB_NOGVL_OFFLOAD_SAFE` when available, otherwise `rb_thread_call_without_gvl`. It does not use a manual Fiber scheduler block/unblock worker path.
- Ruby-level `TG::Geometry::Registry` reload helper.
- Optional `TG::Geometry::ActiveRecordSource` helper without a Rails runtime dependency.
- Benchmark scripts under `benchmark/`.
- Minimal documentation under `docs/`.

### Not included

- `strategy: :auto`.
- Ractor support claim.
- no general no-GVL claim for short query/parse/write paths.
- Full GIS functionality such as routing, projections, geodesics, overlay result geometries, or nearest POI search.
- Public callback/search APIs.
- Universal performance claims.

- Suppressed known GCC diagnostics from vendored tidwall/tg wrapper on Linux CI without muting warnings from tg_geometry's own C code.
