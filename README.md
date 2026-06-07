# tg_geometry

`tg_geometry` is a Ruby C extension around vendored `tidwall/tg`, `tidwall/rtree.c`, and pinned `tidwall/json.c`.

It exposes the public Ruby namespace `TG::Geometry`:

```ruby
require "tg/geometry"
```

The gem is focused on fast in-process planar geometry parsing, predicates, format conversion, GeoJSON FeatureCollection imports, point-to-geometry distance queries, and immutable geofencing indexes. It is not a full GIS system.

## Start here

For runnable examples, use **[GET_STARTED.md](GET_STARTED.md)**.

That guide shows the normal path from install to real queries:

- parsing GeoJSON/WKT/WKB/Hex/GeoBIN;
- direct Ruby constructors;
- immutable `Index` builds;
- point geofencing queries;
- packed batch point queries;
- point-to-geometry distance in planar XY and approximate lon/lat meters;
- GeoJSON FeatureCollection imports;
- SRID/EWKB conversion;
- nearest segment helpers;
- Registry and optional ActiveRecord integration;
- safe immutable-index reload pattern.

## Installation

```ruby
gem "tg_geometry"
```

Then run:

```bash
bundle install
```

The extension builds from vendored C sources. It does not require GEOS, PostGIS, PROJ, GDAL, system TG, or system rtree.

Supported platforms: Linux and macOS on x86_64/aarch64. Windows is not supported for the first release.

## API map

This README intentionally stays compact. The runnable examples live in [GET_STARTED.md](GET_STARTED.md); deeper design notes live in `docs/`.

### Parsing and format conversion

`TG::Geometry.parse` auto-detects GeoJSON, WKT, WKB, Hex, and GeoBIN. Explicit shortcuts are also available: `parse_geojson`, `parse_wkt`, `parse_wkb`, `parse_hex`, and `parse_geobin`.

`TG::Geometry::Geom` objects are immutable and can be converted back to WKT/WKB/GeoJSON where supported.

See [GET_STARTED.md#2-parse-a-geometry](GET_STARTED.md#2-parse-a-geometry).

### Constructors

Planar geometries can be constructed directly from Ruby arrays without parsing strings or bytes: point, multipoint, line string, polygon, multiline, multipolygon, and geometry collection helpers are available.

See [GET_STARTED.md#3-construct-geometries-directly](GET_STARTED.md#3-construct-geometries-directly).

### SRID and EWKB

EWKB SRID metadata is preserved on parse when present. `to_wkb` writes plain WKB; `to_ewkb` writes a PostGIS-compatible SRID-bearing payload. SRID is metadata only: no coordinate transformation is performed.

See [GET_STARTED.md#8-preserve-srid-and-write-ewkb](GET_STARTED.md#8-preserve-srid-and-write-ewkb) and [docs/SRID_AND_EWKB.md](docs/SRID_AND_EWKB.md).

### Rect

`TG::Geometry::Rect` is an immutable helper for bounding boxes, containment checks, intersections, and expansion.

See [GET_STARTED.md#4-use-rect-helpers](GET_STARTED.md#4-use-rect-helpers).

### Immutable Index

`TG::Geometry::Index` stores immutable entries with explicit `via:`, `strategy:`, and `predicate:` options. `:rtree` builds a spatial index; `:flat` does a linear scan with bbox prefiltering. There is no `:auto` strategy by design.

See [GET_STARTED.md#5-build-an-immutable-index](GET_STARTED.md#5-build-an-immutable-index).

### Point and geometry queries

Index methods cover point geofencing, packed batch point queries, rectangle intersection, and geometry-vs-geometry predicates.

See:

- [GET_STARTED.md#5-build-an-immutable-index](GET_STARTED.md#5-build-an-immutable-index)
- [GET_STARTED.md#6-batch-point-queries](GET_STARTED.md#6-batch-point-queries)
- [docs/GEOMETRY_QUERIES.md](docs/GEOMETRY_QUERIES.md)

### Point-to-geometry distance

Distance APIs are explicit about units:

- `*_xy` returns input coordinate units;
- `*_lnglat_meters` returns approximate local meters for lon/lat geofencing.

There is no `metric:` option, no automatic lon/lat-vs-XY detection, no kNN index, and no geodesic/great-circle segment distance.

See [GET_STARTED.md#7-measure-distance-to-a-zone](GET_STARTED.md#7-measure-distance-to-a-zone) and [docs/GEOMETRY_QUERIES.md](docs/GEOMETRY_QUERIES.md).

### GeoJSON FeatureSource

`TG::Geometry::FeatureSource` reads GeoJSON `FeatureCollection` sources without `JSON.parse` of the whole document into Ruby Hash/Array objects. It can return index entries, feature triples with raw properties JSON, reports, or build an index directly.

See [GET_STARTED.md#9-import-a-geojson-featurecollection](GET_STARTED.md#9-import-a-geojson-featurecollection) and [docs/FEATURE_SOURCE.md](docs/FEATURE_SOURCE.md).

### Nearest segment

`Line#nearest_segment(x, y)` and `Ring#nearest_segment(x, y)` expose low-level planar nearest-segment helpers.

See [GET_STARTED.md#10-use-nearest-segment-helpers](GET_STARTED.md#10-use-nearest-segment-helpers) and [docs/NEAREST_SEGMENT.md](docs/NEAREST_SEGMENT.md).

### Registry helper

`TG::Geometry::Registry` is Ruby-level sugar over immutable indexes with safe reload semantics.

See [GET_STARTED.md#11-wrap-an-index-in-a-registry](GET_STARTED.md#11-wrap-an-index-in-a-registry).

### Optional ActiveRecord integration

`TG::Geometry::ActiveRecordType` is an optional read-only convenience type for PostGIS-like geometry columns. It is not required by `tg/geometry`; load it explicitly.

See [GET_STARTED.md#12-optional-activerecord-read-only-type](GET_STARTED.md#12-optional-activerecord-read-only-type).

## Memory and concurrency

The implementation uses explicit allocator pairs and Ruby GC accounting for native memory. `ObjectSpace.memsize_of(index)` includes entries, owned TG geometries, and exact rtree allocation bytes. Borrowed geometries are not double-counted by the index.

`Index` and `Geom` are immutable after construction. Concurrent read-only use from normal Ruby threads is supported. Short query/parse/write paths keep the GVL. FeatureSource bulk loading uses a C-only no-GVL heavy phase for file read, JSON traversal, and TG geometry parsing, then reacquires the GVL to create Ruby objects or transfer ownership into the final Index. On Ruby versions that expose `RB_NOGVL_OFFLOAD_SAFE`, that no-GVL phase is marked offload-safe for the Ruby VM. On older Rubies it still releases the GVL for other Ruby threads, but no explicit Fiber scheduler friendliness is claimed.

No Ractor support and no universal performance claim are advertised.

See [docs/MEMORY_OWNERSHIP.md](docs/MEMORY_OWNERSHIP.md) and [docs/CONCURRENCY.md](docs/CONCURRENCY.md).

## Benchmarks

Benchmark scripts live in `benchmark/`. They are engineering tools, not marketing claims.

Start with [docs/BENCHMARKING.md](docs/BENCHMARKING.md).

## Limitations

`tg_geometry` is not a full GIS system.

Not included:

- Geodesic / Haversine distance;
- Projection / reprojection;
- Buffer / union / difference / convex hull;
- Index nearest_ids (KNN);
- GeoBIN bbox helpers;
- Streaming FeatureSource;
- Index serialization / mmap;
- Ractor support;
- Windows / JRuby;
- Write-side ActiveRecordType / AR scopes / migrations;
- Z/M variants of array constructors;
- Public `release_gvl:` option.

TG works in planar XY coordinates. If lon/lat coordinates are passed in, length, area, perimeter, and low-level nearest-segment distances are in input coordinate units, not meters. The explicit `*_lnglat_meters` point-to-geometry APIs are the exception: they return approximate local meters using a query-local equirectangular frame, not geodesic meters.

See [docs/LIMITATIONS.md](docs/LIMITATIONS.md) for the full list.

## Documentation

- [GET_STARTED.md](GET_STARTED.md) — runnable examples
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the C extension is wired
- [docs/MEMORY_OWNERSHIP.md](docs/MEMORY_OWNERSHIP.md) — allocator pair table
- [docs/CONCURRENCY.md](docs/CONCURRENCY.md) — threading rules
- [docs/ERROR_HANDLING.md](docs/ERROR_HANDLING.md) — exception hierarchy
- [docs/BENCHMARKING.md](docs/BENCHMARKING.md) — running the benchmark scripts
- [docs/FEATURE_SOURCE.md](docs/FEATURE_SOURCE.md) — GeoJSON FeatureCollection imports
- [docs/GEOMETRY_QUERIES.md](docs/GEOMETRY_QUERIES.md) — geometry query details
- [docs/LIMITATIONS.md](docs/LIMITATIONS.md) — non-goals and accuracy boundaries

## Development

```bash
bundle install
bundle exec rake compile
bundle exec rake spec
```

Useful targeted checks:

```bash
bundle exec rspec spec/batch_packed_spec.rb
bundle exec rspec spec/memory_gc_spec.rb
bundle exec rspec spec/concurrency_spec.rb
bundle exec rspec spec/fuzz_spec.rb
bundle exec rspec spec/distance_spec.rb
```

## License

MIT. Vendored upstream license files for `tidwall/tg`, `tidwall/rtree.c`, and `tidwall/json.c` are included under `ext/tg_geometry/vendor/`.
