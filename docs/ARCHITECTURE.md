# tg_geometry architecture

`tg_geometry` is a Ruby C extension for the public namespace `TG::Geometry`. It vendors the upstream `tidwall/tg` geometry engine and `tidwall/rtree.c`; it does not depend on GEOS, PostGIS, PROJ, GDAL, system TG, or system rtree libraries.

The gem targets a small production-grade core:

- parsing and writing TG geometries from Ruby;
- exact planar geometry predicates;
- immutable rectangles for bounding boxes and query windows;
- immutable geofencing-oriented indexes that return user ids;
- flat and rtree collection-level search strategies;
- native-endian packed point batches for high-throughput same-process calls;
- read-only borrowed low-level Line/Ring/Polygon wrappers;
- grouped TG API coverage for predicates, accessors, point/empty constructors, Segment values, and GeometryCollection children.

The gem is not a full GIS system. See `docs/LIMITATIONS.md`.

## Public namespace

The canonical require path is:

```ruby
require "tg/geometry"
```

The public API lives under `TG::Geometry`. The top-level `TG` module is only a namespace container, not the public gem API.

## Native extension shape

The current extension is built by `ext/tg_geometry/extconf.rb` and loaded as `tg_geometry_ext_geometry_ext`. The build requires Ruby >= 3.0 and a C11 compiler. Vendor sources are compiled into the extension through small wrapper files:

- `ext/tg_geometry/tg_geometry_vendor_tg.c` includes `vendor/tg/tg.c`;
- `ext/tg_geometry/tg_geometry_vendor_rtree.c` includes `vendor/rtree/rtree.c`.

No visibility-hiding flag is enabled unless the Init symbol is explicitly exported. The Init function is exported with `RUBY_FUNC_EXPORTED`.

## Immutable `TG::Geometry::Geom`

`TG::Geometry::Geom` usually wraps one owned `struct tg_geom *`. Expansion Block J also allows internal borrowed `TG::Geometry::Geom` wrappers for GeometryCollection children. Borrowed wrappers keep a parent `geom_owner`, report no owned native bytes, and do not call `tg_geom_free` on the borrowed child pointer.

Rules:

- public `.allocate` is disabled;
- objects are created only by parse APIs, safe constructors, or internal borrowed child wrappers;
- the native pointer is never replaced;
- there is no `close!`, `free!`, `detach!`, or mutation API;
- parsed objects are frozen before being returned.

This immutability is required because `TG::Geometry::Index` can borrow a native geometry pointer from a `TG::Geometry::Geom` and keep the Ruby owner alive.

## Immutable `TG::Geometry::Rect`

`TG::Geometry::Rect` is a small Ruby object around four finite coordinates. It is constructible from Ruby and frozen after initialization. The first release exposes only unambiguous rectangle APIs:

- coordinate readers;
- `center`;
- `intersects?`;
- `contains_point?`;
- expansion methods returning new Rect objects.

`Rect#contains?` is intentionally not exposed because the name is ambiguous.

## Immutable `TG::Geometry::Index`

`TG::Geometry::Index` is built once and read-only afterwards.

```ruby
index = TG::Geometry::Index.build(
  [[id1, object1], [id2, object2]],
  via: :geojson,
  strategy: :rtree,
  predicate: :covers,
  geometry_index: :ystripes
)
```

Accepted `via:` modes:

- `:geom` borrows native geometry from existing `TG::Geometry::Geom` wrappers and marks the owner Ruby objects;
- `:geojson` parses entry strings into Index-owned TG geometries;
- `:wkb` parses entry strings as raw WKB bytes into Index-owned TG geometries.

Accepted strategies:

- `:flat` scans entries in insertion order with bbox prefiltering and exact TG geometry filtering;
- `:rtree` uses vendored `rtree.c` over entry bboxes, then applies exact TG geometry filtering.

`strategy: :auto` is not implemented in the first public release. Use explicit `:flat` or `:rtree` and validate with repository benchmarks.

## Result order

Insertion order is public behavior.

Each entry stores a unique `ordinal`. Flat strategy naturally scans entries in ordinal order. Rtree strategy uses rtree only as a candidate prefilter; candidate marks are local to the query and results are emitted by scanning entries in ordinal order. Rtree traversal order never leaks into Ruby results.

## Point predicate implementation

Point queries allocate a temporary TG point geometry and use exact TG predicates:

- `:covers` calls `tg_geom_covers(entry_geom, point_geom)`;
- `:contains` calls `tg_geom_contains(entry_geom, point_geom)`.

This is intentionally not the fastest possible point path. The first release chooses exact `covers` / `contains` semantics over a no-allocation shortcut. A faster path such as `tg_geom_intersects_xy` can only replace it after boundary and hole-boundary equivalence tests plus benchmarks are added.

## Reload pattern

The intended application reload pattern is atomic reference replacement:

```ruby
new_index = TG::Geometry::Index.build(entries, via: :geojson, strategy: :rtree)
@index = new_index
```

Old readers keep using the old immutable object until they release it. New readers see the new object after the Ruby reference swap. There is no in-place reload, mutation, add, delete, or builder API in the first release.

## Expansion Blocks A-E and I-J

Expansion Block A (`strategy: :auto`) is not enabled in the first public release. The native Index stores only explicit concrete strategies (`:flat` or `:rtree`).

Expansion Block B adds `TG::Geometry::Registry` in Ruby. Registry wraps an immutable Index reference and reloads by building a new Index before swapping the reference.

Expansion Block C adds `TG::Geometry::ActiveRecordSource` as optional Ruby-only source sugar. The native extension does not depend on Rails or ActiveRecord.

Expansion Block D adds Hex/GeoBIN parse/write helpers and raw `extra_json` copying. It does not parse properties into Ruby Hashes.

Expansion Block E adds read-only borrowed wrappers for `TG::Geometry::Line`, `TG::Geometry::Ring`, and `TG::Geometry::Polygon`. These wrappers keep the parent `TG::Geometry::Geom` alive through `geom_owner`, mark it for GC, update it during compaction, and never free borrowed TG child pointers directly.

Expansion Block I documents and tests the current Ractor boundary: native wrappers are not advertised as Ractor-shareable objects. Normal thread read-only access remains the supported concurrency model.

Expansion Block J adds grouped TG API coverage without exposing global mutable environment settings or callback-based APIs. Implemented groups are additional predicates, geometry metadata/collection accessors, point and empty geometry constructors, value `TG::Geometry::Segment`, and borrowed GeometryCollection child `Geom` wrappers. See `docs/FULL_TG_API_COVERAGE.md`.
