# Changelog

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
