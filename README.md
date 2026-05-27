# tg_geometry

`tg_geometry` is a Ruby C extension around vendored `tidwall/tg`, `tidwall/rtree.c`, and pinned `tidwall/json.c`.

It exposes the public Ruby namespace `TG::Geometry`:

```ruby
require "tg/geometry"
```

The gem is focused on fast in-process planar geometry parsing, predicates, format conversion, GeoJSON FeatureCollection imports, and immutable geofencing indexes. It is not a full GIS system.

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

## Parsing and predicates

```ruby
zone = TG::Geometry.parse_geojson(<<~JSON)
  {
    "type": "Polygon",
    "coordinates": [[[0,0], [10,0], [10,10], [0,10], [0,0]]]
  }
JSON

zone.frozen?          # => true
zone.type             # => :polygon
zone.covers_xy?(5, 5) # => true
zone.covers_xy?(0, 0) # => true, boundary is covered
zone.bbox             # => #<TG::Geometry::Rect ...>
zone.to_wkt
zone.to_wkb
```

Parse shortcuts:

```ruby
TG::Geometry.parse(str, format: :auto, index: :ystripes)
TG::Geometry.parse_geojson(str, index: :ystripes)
TG::Geometry.parse_wkt(str, index: :ystripes)
TG::Geometry.parse_wkb(bytes, index: :ystripes)
TG::Geometry.parse_hex(str, index: :ystripes)
TG::Geometry.parse_geobin(bytes, index: :ystripes)
```

`TG::Geometry::Geom` objects are immutable and cannot be manually allocated or manually freed from Ruby.

## Constructors

Construct simple planar geometries directly from Ruby arrays without parsing strings or bytes:

```ruby
line = TG::Geometry.line_string([[0.0, 0.0], [10.0, 0.0]], index: :natural, srid: 4326)
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

Constructors are strict: rings must already be closed, invalid coordinates raise `TG::Geometry::ArgumentError`, and no winding/self-intersection fixes are performed. `srid:` is metadata on the wrapper; it does not alter coordinates or perform reprojection.

## SRID and EWKB

`parse_wkb` and `parse_hex` preserve EWKB SRID metadata when the EWKB SRID flag is present:

```ruby
geom = TG::Geometry.parse_wkb(postgis_bytea)
geom.srid # => 4326, 3857, 0, or nil for plain WKB
```

`to_wkb` always writes plain WKB. Use `to_ewkb` when a PostGIS-compatible SRID-bearing payload is required:

```ruby
ewkb = geom.to_ewkb              # uses geom.srid
ewkb = geom.to_ewkb(srid: 4326)  # explicit override
```

SRID is metadata only. `tg_geometry` does not check SRID compatibility, transform coordinates, or calculate geodesic distances.

## Rect

```ruby
rect = TG::Geometry::Rect.new(0, 0, 10, 10)
rect.center                  # => [5.0, 5.0]
rect.contains_point?(5, 5)   # => true
rect.intersects?(other_rect)
rect.expand_to_include(other_rect)
rect.expand_to_include_point(x, y)
```

`Rect` rejects non-finite coordinates and invalid coordinate order. It is frozen after construction.

## Immutable Index

```ruby
entries = [
  [:zone_a, '{"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]]]}'],
  [:zone_b, '{"type":"Polygon","coordinates":[[[20,20],[30,20],[30,30],[20,30],[20,20]]]}']
]

index = TG::Geometry::Index.build(
  entries,
  via: :geojson,
  strategy: :rtree,
  predicate: :covers,
  geometry_index: :ystripes
)

index.frozen?               # => true
index.size                  # => 2
index.strategy              # => :rtree
index.predicate             # => :covers
index.find_covering(5, 5)   # => :zone_a
index.covering_ids(5, 5)    # => [:zone_a]
index.intersecting_rect(0, 0, 25, 25)
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

Accepted `via:` modes:

- `:geom` — borrow an existing `TG::Geometry::Geom` and keep its owner alive.
- `:geojson` — parse and own native TG geometries inside the index.
- `:wkb` — parse and own native TG geometries inside the index.

Accepted strategies:

- `:flat`
- `:rtree`

Accepted predicates:

- `:covers` — default for geofencing; boundary points are included.
- `:contains` — stricter containment semantics.

The `predicate:` option affects only the legacy point-based query methods (`find_covering`, `covering_ids(x, y)`, `covering_ids_batch_packed`). The geometry-based query methods (`intersecting_geom_ids`, `covering_geom_ids`, `containing_geom_ids`) use their own predicates based on the method name.

`strategy: :auto` is intentionally not exposed. Choose the strategy explicitly and benchmark on your own data.

## Geometry-based index queries

```ruby
query = TG::Geometry.polygon([[1, 1], [2, 1], [2, 2], [1, 1]])
index.intersecting_geom_ids(query)
index.covering_geom_ids(query)
index.containing_geom_ids(query)
```

Predicate direction is explicit:

| Method | Predicate direction | Boundary semantics |
| --- | --- | --- |
| `intersecting_geom_ids(query)` | stored geom intersects query | any intersection |
| `covering_geom_ids(query)` | stored geom covers query | boundary included |
| `containing_geom_ids(query)` | stored geom contains query | strict interior; boundary excluded |

Results are ids only and preserve insertion order. Duplicate ids remain possible if duplicate ids were inserted.

## GeoJSON FeatureSource

`TG::Geometry::FeatureSource` reads GeoJSON `FeatureCollection` sources without `JSON.parse` of the whole document into Ruby Hash/Array objects.

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

For imports that also need raw properties JSON:

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

For direct file-to-index loading:

```ruby
index = TG::Geometry::FeatureSource.build_index_file(
  "zones.geojson",
  id: ["properties", "@id"],
  strategy: :rtree,
  predicate: :covers
)
```

FeatureSource methods are explicit: use `_file` for paths, `_json` for raw content strings, and `_io` for IO objects. There is no path/content auto-detection.

## Packed batch point queries

```ruby
points = [5.0, 5.0, 25.0, 25.0].pack("d*")
index.covering_ids_batch_packed(points)
# => [:zone_a, :zone_b]
```

Input is a Ruby String containing native-endian doubles in `lon, lat` pairs. Length must be a multiple of 16 bytes. Empty string returns `[]`.

## Nearest segment

`Line#nearest_segment(x, y)` and `Ring#nearest_segment(x, y)` return a frozen `TG::Geometry::NearestSegment`:

```ruby
nearest = polygon.polygon.exterior_ring.nearest_segment(5, 5)
nearest.segment   # => TG::Geometry::Segment
nearest.index     # => segment index
nearest.distance  # => Float
nearest.point     # => [x, y] projection on the segment
```

Distance is planar Euclidean distance in input coordinate units. It is not meters unless your input coordinates are already meters. Equal-distance tie-breaks follow tg iteration order and are not API-stable.

## Registry helper

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

Reload builds a new immutable index first and swaps the reference only after a successful build. Existing readers keep using the previous index safely.

## ActiveRecord integration — read-only

`TG::Geometry::ActiveRecordType` is an optional read-only convenience type for PostGIS columns. It is not required by `tg/geometry`; load it explicitly:

```ruby
require "tg/geometry/active_record_type"

class Zone < ApplicationRecord
  attribute :geom, TG::Geometry::ActiveRecordType.new
end

zone.geom.srid
zone.geom.covers_xy?(lon, lat)
```

It can deserialize EWKB bytes, hex EWKB, `\x`-prefixed hex EWKB, GeoJSON, and WKT. Writing `Geom` values is intentionally unsupported in v0.3.0. User applications need `activemodel >= 6.0`; `activerecord` is not a gem dependency.

## Memory and concurrency

The implementation uses explicit allocator pairs and Ruby GC accounting for native memory. `ObjectSpace.memsize_of(index)` includes entries, owned TG geometries, and exact rtree allocation bytes. Borrowed geometries are not double-counted by the index.

`Index` and `Geom` are immutable after construction. Concurrent read-only use from normal Ruby threads is supported. Short query/parse/write paths keep the GVL. FeatureSource bulk loading uses a C-only no-GVL heavy phase for file read, JSON traversal, and TG geometry parsing, then reacquires the GVL to create Ruby objects or transfer ownership into the final Index. On Ruby versions that expose `RB_NOGVL_OFFLOAD_SAFE`, that no-GVL phase is marked offload-safe for the Ruby VM. On older Rubies it still releases the GVL for other Ruby threads, but no explicit Fiber scheduler friendliness is claimed.

No Ractor support and no universal performance claim are advertised.

## Benchmarks

Benchmark scripts live in `benchmark/`:

```bash
bundle exec ruby benchmark/parse_throughput.rb
bundle exec ruby benchmark/flat_vs_rtree.rb
bundle exec ruby benchmark/batch_packed_vs_loop.rb
bundle exec ruby benchmark/objectspace_memsize.rb
bundle exec ruby benchmark/rss_stability.rb
bundle exec ruby benchmark/gvl_threshold.rb
bundle exec ruby benchmark/falcon_concurrency.rb
bundle exec ruby benchmark/feature_source.rb
bundle exec ruby benchmark/geom_query.rb
bundle exec ruby benchmark/nearest_segment.rb
bundle exec ruby benchmark/ewkb_roundtrip.rb
```

The benchmarks are engineering tools, not marketing claims.

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

TG works in planar XY coordinates. If lon/lat coordinates are passed in, length, area, perimeter, and nearest-segment distances are in input coordinate units, not meters.

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
```

## License

MIT. Vendored upstream license files for `tidwall/tg`, `tidwall/rtree.c`, and `tidwall/json.c` are included under `ext/tg_geometry/vendor/`.
