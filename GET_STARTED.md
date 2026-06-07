# Getting started with tg_geometry

A practical path from `gem install` to parsing geometries, building an immutable
geofence index, running point/radius queries, and using the common helpers.

For the compact project overview, see [README.md](README.md). For limitations,
performance considerations, and what this gem deliberately does NOT do, see
[docs/LIMITATIONS.md](docs/LIMITATIONS.md).

## 1. Install

Add to your `Gemfile`:

```ruby
gem "tg_geometry"
```

Then:

```bash
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
polygon.to_wkt             # => "POLYGON..."
polygon.to_wkb             # => binary WKB String
```

`TG::Geometry.parse` auto-detects GeoJSON, WKT, WKB, Hex, and GeoBIN:

```ruby
TG::Geometry.parse(str, format: :auto, index: :ystripes)
TG::Geometry.parse_geojson(str, index: :ystripes)
TG::Geometry.parse_wkt(str, index: :ystripes)
TG::Geometry.parse_wkb(bytes, index: :ystripes)
TG::Geometry.parse_hex(str, index: :ystripes)
TG::Geometry.parse_geobin(bytes, index: :ystripes)
```

Bad input raises `TG::Geometry::ParseError`. The internal TG error geometry is
freed before the exception leaves Ruby. Parsed `TG::Geometry::Geom` objects are
immutable.

## 3. Construct geometries directly

Use constructors when your data is already in Ruby arrays and you do not want to
serialize to GeoJSON/WKT just to parse it back:

```ruby
line = TG::Geometry.line_string(
  [[0.0, 0.0], [10.0, 0.0]],
  index: :natural,
  srid: 4326
)

poly = TG::Geometry.polygon(
  [[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]],
  holes: [[[2, 2], [4, 2], [4, 4], [2, 4], [2, 2]]],
  index: :ystripes,
  srid: 4326
)

mp = TG::Geometry.multi_polygon([
  { exterior: [[0, 0], [1, 0], [1, 1], [0, 0]], holes: [] },
  [[10, 10], [11, 10], [11, 11], [10, 10]]
])
```

Constructors are strict: rings must already be closed, invalid coordinates raise
`TG::Geometry::ArgumentError`, and no winding/self-intersection fixes are
performed. `srid:` is metadata on the wrapper; it does not alter coordinates or
perform reprojection.

## 4. Use Rect helpers

```ruby
rect = TG::Geometry::Rect.new(0, 0, 10, 10)

rect.center                  # => [5.0, 5.0]
rect.contains_point?(5, 5)   # => true
rect.intersects?(TG::Geometry::Rect.new(9, 9, 20, 20))
rect.expand_to_include_point(12, 5)
rect.expand_to_include(TG::Geometry::Rect.new(-1, -1, 2, 2))
```

`Rect` rejects non-finite coordinates and invalid coordinate order. It is frozen
after construction.

## 5. Build an immutable index

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

Accepted input shape:

```ruby
[[id1, object1], [id2, object2], ...]
```

Rules:

- `entries` must be an Array.
- Every entry must be a two-element Array.
- `id` may be any Ruby object except `nil`.
- Duplicate ids are allowed.
- Returned ids are the same Ruby objects stored in the index.
- Result order is insertion order for both `:flat` and `:rtree`.

`via:` selects the ingestion mode:

- `:geojson` / `:wkb` — index owns the parsed geometries.
- `:geom` — index borrows an already-parsed `TG::Geometry::Geom`; the index
  keeps the wrapper alive automatically, so the original Ruby variable can go
  out of scope.

`strategy:` selects the lookup engine. `:rtree` builds a spatial index; `:flat`
does a linear scan with a bounding-box prefilter. There is no `:auto` strategy —
that decision is left to you because the right threshold depends on your data.

`predicate:` affects point-based geofencing methods:

- `:covers` — default for geofencing; boundary points are included.
- `:contains` — stricter containment semantics.

Geometry-based methods use their own predicate based on the method name:

```ruby
query = TG::Geometry.polygon([[1, 1], [2, 1], [2, 2], [1, 1]])

index.intersecting_geom_ids(query)
index.covering_geom_ids(query)
index.containing_geom_ids(query)
```

## 6. Batch point queries

The hot path for ETL and geofencing is the packed batch API. Pack
`[lon1, lat1, lon2, lat2, ...]` as native-endian doubles and call:

```ruby
points = [37.7, -122.4, 40.7, -74.0, 51.5, -0.1].pack("d*")
index.covering_ids_batch_packed(points)
# => [:san_francisco, :new_york, :london]
```

The input length must be a multiple of 16 bytes (two doubles per point). The
result is an array with one entry per point: the first matching id or `nil`.

## 7. Measure distance to a zone

Use `*_xy` when your coordinates are already planar. The distance is returned in
the same units as the input coordinates:

```ruby
polygon.distance_to_xy(12, 5)          # => 2.0
polygon.boundary_distance_to_xy(5, 5)  # => 5.0
polygon.nearest_point_xy(12, 5)        # => [10.0, 5.0]
```

For lon/lat GeoJSON, use the explicit `*_lnglat_meters` methods. Coordinate
order is always `(lng, lat)`, not `(lat, lng)`:

```ruby
park = TG::Geometry.parse_geojson(<<~JSON)
  {
    "type": "Polygon",
    "coordinates": [[[76.9450, 43.2380], [76.9550, 43.2380],
                     [76.9550, 43.2460], [76.9450, 43.2460],
                     [76.9450, 43.2380]]]
  }
JSON

park.distance_to_lnglat_meters(76.9500, 43.2420)
# => 0.0        # point is inside the polygon

park.boundary_distance_to_lnglat_meters(76.9500, 43.2420)
# => approximate meters to the nearest boundary

park.nearest_point_lnglat(76.9600, 43.2420)
# => [76.955, 43.242]   # nearest boundary point, raw planar lng/lat
```

To query an index by radius, use `within_distance_*`:

```ruby
nearby = index.within_distance_lnglat_meters(76.9500, 43.2420, 250.0, sort: true)
# => [[:downtown, 0.0], [:delivery_area, 184.3]]

ids = index.within_distance_ids_lnglat_meters(76.9500, 43.2420, 250.0)
# => [:downtown, :delivery_area]
```

Lon/lat distance methods return approximate meters using a per-query local
equirectangular frame. Segments are GeoJSON straight coordinate segments, not
great-circle arcs. This is intended for local geofencing, warning before a
boundary, GPS hysteresis, and similar nearby-zone checks — not geodesy.
Longitude is not wrapped at `+/-180`; cross-antimeridian proximity is out of
scope and data that crosses the antimeridian should be cut before import.

## 8. Preserve SRID and write EWKB

`parse_wkb` and `parse_hex` preserve EWKB SRID metadata when the EWKB SRID flag
is present:

```ruby
geom = TG::Geometry.parse_wkb(postgis_bytea)
geom.srid # => 4326, 3857, 0, or nil for plain WKB
```

`to_wkb` always writes plain WKB. Use `to_ewkb` when a PostGIS-compatible
SRID-bearing payload is required:

```ruby
ewkb = geom.to_ewkb              # uses geom.srid
ewkb = geom.to_ewkb(srid: 4326)  # explicit override
```

SRID is metadata only. `tg_geometry` does not check SRID compatibility,
transform coordinates, or calculate geodesic distances.

## 9. Import a GeoJSON FeatureCollection

`TG::Geometry::FeatureSource` reads GeoJSON `FeatureCollection` sources without
`JSON.parse` of the whole document into Ruby Hash/Array objects.

Read entries for an index:

```ruby
entries = TG::Geometry::FeatureSource.read_entries_file(
  "zones.geojson",
  id: ["properties", "@id"],
  only: [:polygon, :multipolygon]
)

index = TG::Geometry::Index.build(
  entries,
  via: :geojson,
  strategy: :rtree,
  predicate: :covers
)
```

Read features with raw properties JSON:

```ruby
features = TG::Geometry::FeatureSource.read_features_file(
  "zones.geojson",
  id: ["properties", "@id"],
  report: true,
  on_invalid: :skip
)

features[:features].each do |id, geometry_json, properties_json|
  # Store geometry_json and parse properties_json in application code if needed.
end
```

Build an index directly from a file:

```ruby
index = TG::Geometry::FeatureSource.build_index_file(
  "zones.geojson",
  id: ["properties", "@id"],
  strategy: :rtree,
  predicate: :covers
)
```

FeatureSource methods are explicit: use `_file` for paths, `_json` for raw
content strings, and `_io` for IO objects. There is no path/content
auto-detection.

## 10. Use nearest segment helpers

`Line#nearest_segment(x, y)` and `Ring#nearest_segment(x, y)` return a frozen
`TG::Geometry::NearestSegment`:

```ruby
nearest = polygon.polygon.exterior_ring.nearest_segment(5, 5)

nearest.segment   # => TG::Geometry::Segment
nearest.index     # => segment index
nearest.distance  # => Float
nearest.point     # => [x, y] projection on the segment
```

Distance is planar Euclidean distance in input coordinate units. It is not
meters unless your input coordinates are already meters. Equal-distance
tie-breaks follow tg iteration order and are not API-stable.

## 11. Wrap an index in a Registry

`Registry` is Ruby-level sugar over immutable indexes:

```ruby
class DeliveryZones < TG::Geometry::Registry
  source do
    [
      [:zone_a, '{"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]]]}']
    ]
  end

  index_options via: :geojson, strategy: :rtree, predicate: :covers
end

registry = DeliveryZones.new
registry.reload!
registry.find_covering(5, 5)
```

Reload builds a new immutable index first and swaps the reference only after a
successful build. Existing readers keep using the previous index safely.

## 12. Optional ActiveRecord read-only type

`TG::Geometry::ActiveRecordType` is an optional read-only convenience type for
PostGIS columns. It is not required by `tg/geometry`; load it explicitly:

```ruby
require "tg/geometry/active_record_type"

class Zone < ApplicationRecord
  attribute :geom, TG::Geometry::ActiveRecordType.new
end

zone.geom.srid
zone.geom.covers_xy?(lon, lat)
```

It can deserialize EWKB bytes, hex EWKB, `\x`-prefixed hex EWKB, GeoJSON, and
WKT. Writing `Geom` values is intentionally unsupported. User applications need
`activemodel >= 6.0`; `activerecord` is not a gem dependency.

## 13. Reload pattern

Indexes are immutable. To replace one, build a new index and swap the reference.
Active readers holding the old reference finish safely; the old index is
reclaimed by GC once no thread holds it.

```ruby
@current_index = TG::Geometry::Index.build(new_zones, via: :geojson, strategy: :rtree)
```

There is no `add`, `delete`, `rebuild!`, or `clear` method — by design.

## What this gem is not

- Not a full GIS system. No buffer/union/difference, no routing, no geocoding,
  no projections, no nearest-POI index.
- Not geodesy. Lon/lat distance methods return approximate local meters; XY
  distance methods return input coordinate units. No projections, reprojections,
  or great-circle segment distances are performed.
- No Ractor safety claim. Normal multi-threaded read use is supported and
  tested.

See [docs/LIMITATIONS.md](docs/LIMITATIONS.md) for the full list.

## Next steps

- [README.md](README.md) — compact project overview and API map
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the C extension is wired
- [docs/MEMORY_OWNERSHIP.md](docs/MEMORY_OWNERSHIP.md) — allocator pair table
- [docs/CONCURRENCY.md](docs/CONCURRENCY.md) — threading rules
- [docs/ERROR_HANDLING.md](docs/ERROR_HANDLING.md) — exception hierarchy
- [docs/BENCHMARKING.md](docs/BENCHMARKING.md) — running the benchmark scripts
- [docs/FEATURE_SOURCE.md](docs/FEATURE_SOURCE.md) — GeoJSON FeatureCollection imports
- [docs/GEOMETRY_QUERIES.md](docs/GEOMETRY_QUERIES.md) — geometry and distance query details
