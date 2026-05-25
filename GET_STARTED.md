# Getting started with tg_geometry

A 5-minute path from `gem install` to your first geofence query.

For the full API surface, see [README.md](README.md). For limitations,
performance considerations, and what this gem deliberately does NOT do, see
[docs/LIMITATIONS.md](docs/LIMITATIONS.md).

## 1. Install

Add to your `Gemfile`:

```ruby
gem "tg_geometry"
```

Then:

```
bundle install
```

The gem ships with vendored `tidwall/tg` and `tidwall/rtree.c` sources and
builds them at install time. No GEOS, PostGIS, PROJ, or GDAL are required.

Requirements:

- Ruby `>= 3.0`
- A C11 compiler (`gcc` or `clang` with `<stdatomic.h>`)
- Linux or macOS

## 2. Parse a geometry

```ruby
require "tg/geometry"

polygon = TG::Geometry.parse_geojson(<<~JSON)
  {
    "type": "Polygon",
    "coordinates": [[[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]]]
  }
JSON

polygon.frozen?            # => true
polygon.type               # => :polygon
polygon.covers_xy?(5, 5)   # => true
polygon.covers_xy?(0, 0)   # => true   (covers includes boundary)
polygon.bbox.min_x         # => 0.0
```

`TG::Geometry.parse` auto-detects GeoJSON, WKT, WKB, Hex, and GeoBIN.
`TG::Geometry.parse_geojson`, `parse_wkt`, `parse_wkb`, `parse_hex`, and
`parse_geobin` are the explicit-format shortcuts.

Bad input raises `TG::Geometry::ParseError`. The internal TG error
geometry is freed before the exception leaves Ruby.

## 3. Build an immutable index

```ruby
zones = [
  [:downtown,      polygon_geojson_string],
  [:warehouse,     other_polygon_geojson_string],
  [:delivery_area, yet_another_polygon_geojson_string]
]

index = TG::Geometry::Index.build(
  zones,
  via:      :geojson,   # also :wkb or :geom
  strategy: :rtree,     # or :flat
  predicate: :covers    # or :contains for strict OGC semantics
)

index.frozen?              # => true
index.size                 # => 3
index.find_covering(x, y)  # => :downtown / nil
index.covering_ids(x, y)   # => [:downtown, :delivery_area]
index.intersecting_rect(0, 0, 100, 100)
```

`via:` selects the ingestion mode:

- `:geojson` / `:wkb` — index owns the parsed geometries.
- `:geom` — index borrows an already-parsed `TG::Geometry::Geom`; the
  index keeps the wrapper alive automatically, so the original Ruby
  variable can go out of scope.

`strategy:` selects the lookup engine. `:rtree` builds a spatial index;
`:flat` does a linear scan with a bounding-box prefilter. There is no
`:auto` strategy in the first release — that decision is left to you
because the right threshold depends on your data.

## 4. Batch point queries

The hot path for ETL and geofencing is the packed batch API. Pack
`[lon1, lat1, lon2, lat2, ...]` as native-endian doubles and call:

```ruby
points = [37.7, -122.4, 40.7, -74.0, 51.5, -0.1].pack("d*")
index.covering_ids_batch_packed(points)
# => [:san_francisco, :new_york, :london]
```

The input length must be a multiple of 16 bytes (two doubles per point).
The result is an array with one entry per point: the first matching id or
`nil`.

## 5. Reload pattern

Indexes are immutable. To replace one, build a new index and swap the
reference. Active readers holding the old reference finish safely; the
old index is reclaimed by GC once no thread holds it.

```ruby
@current_index = TG::Geometry::Index.build(new_zones, via: :geojson, strategy: :rtree)
```

There is no `add`, `delete`, `rebuild!`, or `clear` method — by design.

## What this gem is not

- Not a full GIS system. No buffer/union/difference, no routing, no
  geocoding, no projections, no nearest-POI index.
- All coordinates are planar XY. Distances and areas are in input
  coordinate units, not meters for lon/lat inputs.
- No Ractor safety claim. Normal multi-threaded read use is supported and
  tested.

See [docs/LIMITATIONS.md](docs/LIMITATIONS.md) for the full list.

## Next steps

- [README.md](README.md) — complete API reference
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the C extension is wired
- [docs/MEMORY_OWNERSHIP.md](docs/MEMORY_OWNERSHIP.md) — allocator pair table
- [docs/CONCURRENCY.md](docs/CONCURRENCY.md) — threading rules
- [docs/ERROR_HANDLING.md](docs/ERROR_HANDLING.md) — exception hierarchy
- [docs/BENCHMARKING.md](docs/BENCHMARKING.md) — running the benchmark scripts
- [docs/REGISTRY.md](docs/REGISTRY.md) — `TG::Geometry::Registry` reload sugar
- [docs/ACTIVE_RECORD.md](docs/ACTIVE_RECORD.md) — optional ActiveRecord source
