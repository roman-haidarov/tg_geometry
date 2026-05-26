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

`strategy: :auto` is intentionally not exposed. Choose the strategy explicitly and benchmark on your own data.

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
```

The benchmarks are engineering tools, not marketing claims.

## Limitations

`tg_geometry` is not a full GIS system.

Not included:

- geocoding;
- routing;
- projections;
- geodesic distance/area;
- buffer / union / difference / overlay result geometry operations;
- nearest POI index;
- Rails dependency in the native extension;
- Redis or external service dependency;
- public callback/search APIs;
- Ractor support claim;
- no-GVL execution claim;
- universal `:auto` strategy.

TG works in planar XY coordinates. If lon/lat coordinates are passed in, length, area, and perimeter-style values are in input coordinate units, not meters.

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
