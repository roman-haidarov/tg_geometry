# tg_geometry

`tg_geometry` is a Ruby C extension around the vendored `tidwall/tg` geometry
library and `tidwall/rtree.c`.

It exposes the public Ruby namespace `TG::Geometry` and the canonical require
path:

```ruby
require "tg/geometry"
```

The gem targets fast in-process planar geometry parsing, predicates,
format conversion, and geofencing-oriented immutable indexes. It does not try
to be a full GIS system.

## Status

This repository is prepared as a first public release candidate with an
expanded API surface:

- release-core `Geom`, `Rect`, and immutable `Index` APIs;
- expanded format coverage for Hex and GeoBIN;
- read-only borrowed wrappers for lower-level TG geometry components;
- `Registry` reload/swap sugar;
- optional ActiveRecord-style source helpers that do not add a Rails runtime
  dependency.

`strategy: :auto`, Ractor support, callback/search APIs, no-allocation point
query optimization, geodesic helpers, projections, and no-GVL execution are not
claimed in this release.

## Installation

Add this line to your application's Gemfile:

```ruby
gem "tg_geometry"
```

Then run:

```bash
bundle install
```

The extension is built from vendored C sources. There is no GEOS, PostGIS,
PROJ, GDAL, system TG, or system rtree dependency.

Supported first-release platforms are Linux and macOS on x86_64/aarch64.
Windows is not supported in this release.

## Basic parsing and predicates

```ruby
require "tg/geometry"

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

wkt = zone.to_wkt
wkb = zone.to_wkb
```

`TG::Geometry::Geom` objects are immutable. They cannot be manually allocated or
manually freed from Ruby. Native memory is released by Ruby GC through the typed
data wrapper.

## Parse API

```ruby
TG::Geometry.parse(str, format: :auto, index: :ystripes)
TG::Geometry.parse_geojson(str, index: :ystripes)
TG::Geometry.parse_wkt(str, index: :ystripes)
TG::Geometry.parse_wkb(bytes, index: :ystripes)
TG::Geometry.parse_hex(str, index: :ystripes)
TG::Geometry.parse_geobin(bytes, index: :ystripes)
```

Accepted `format:` values for `parse` are:

- `:auto`
- `:geojson`
- `:wkt`
- `:wkb`
- `:hex`
- `:geobin`

Accepted TG internal polygon index values are:

- `:default`
- `:none`
- `:natural`
- `:ystripes`

Parse failures raise `TG::Geometry::ParseError`. Invalid options raise
`TG::Geometry::ArgumentError`, which inherits from Ruby's `::ArgumentError`.

## Geom API

Release-core methods:

```ruby
geom.type
geom.bbox
geom.covers_xy?(x, y)
geom.contains?(other_geom)
geom.intersects?(other_geom)
geom.to_geojson
geom.to_wkt
geom.to_wkb
```

Expanded methods include additional predicates, format writers, metadata
accessors, and read-only borrowed child wrappers. See:

- `docs/FORMAT_COVERAGE.md`
- `docs/LOW_LEVEL_GEOMETRY.md`
- `docs/FULL_TG_API_COVERAGE.md`

For point predicates, this release prioritizes exact `covers` / `contains`
semantics over the fastest possible no-allocation path. Query methods construct
a temporary TG point geometry and free it before returning. A future optimized
point path requires boundary and hole-boundary equivalence tests plus benchmark
proof.

## Rect API

```ruby
rect = TG::Geometry::Rect.new(0, 0, 10, 10)

rect.min_x
rect.min_y
rect.max_x
rect.max_y
rect.center                  # => [5.0, 5.0]
rect.contains_point?(5, 5)   # => true
rect.intersects?(other_rect)
rect.expand_to_include(other_rect)
rect.expand_to_include_point(x, y)
```

`Rect` rejects non-finite coordinates and invalid coordinate order. It is frozen
after construction.

There is intentionally no first-release `Rect#contains?` method because the name
is ambiguous. Use `contains_point?`.

## Immutable Index

`TG::Geometry::Index` is built once and then read-only forever.

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

Accepted input format:

```ruby
[[id1, object1], [id2, object2], ...]
```

Rules:

- `entries` must be an Array.
- Every entry must be a two-element Array.
- `id` may be any Ruby object except `nil`.
- `false` ids are accepted, but discouraged because `find_covering` uses `nil`
  for no match.
- Duplicate ids are allowed.
- Returned ids are the same Ruby objects stored in the index; they are not
  copied, frozen, stringified, or duplicated.
- Result order is insertion order for both `:flat` and `:rtree`.

Accepted `via:` modes:

- `:geom` — borrow an existing `TG::Geometry::Geom`; the index marks the owner
  wrapper so the borrowed native pointer remains valid.
- `:geojson` — parse and own native TG geometries inside the index.
- `:wkb` — parse and own native TG geometries inside the index.

Accepted strategies:

- `:flat`
- `:rtree`

`strategy: :auto` is not exposed in this release. The benchmark output does not
support a single universal threshold: flat scan may win for early insertion-order
hits or heavily overlapping datasets, while rtree may win for misses, later hits,
or selective rectangle queries. Choose the strategy explicitly and benchmark on
your own data.

Accepted predicates:

- `:covers` — default for geofencing; boundary points are included.
- `:contains` — stricter OGC-style containment semantics.

## Packed batch point queries

For high-throughput same-process point lookups, the index supports a packed
native-endian double input format:

```ruby
points = [5.0, 5.0, 25.0, 25.0].pack("d*")
index.covering_ids_batch_packed(points)
# => [:zone_a, :zone_b]
```

Input format:

- Ruby String treated as raw bytes.
- Native-endian doubles.
- Pairs of `lon, lat`.
- Length must be a multiple of 16 bytes.
- Empty string returns `[]`.

This format is intentionally native-endian for same-process speed and simplicity.
Do not use it as a cross-platform serialized file format.

## Registry reload pattern

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

Reload builds a new full immutable index first and swaps the reference only after
successful build:

```ruby
new_index = TG::Geometry::Index.build(entries, via: :geojson, strategy: :rtree)
@index = new_index
```

Old indexes remain alive while existing readers hold references to them. There
is no in-place mutation, no public `add`, `delete`, `clear`, or `rebuild!` API on
`Index`.

See `docs/REGISTRY.md` and `docs/ACTIVE_RECORD.md` for the expanded helpers.

## Memory ownership model

The implementation uses explicit allocator pairs and GC accounting:

| Resource | Allocator | Deallocator | Owner |
|---|---|---|---|
| `tg_geom_wrapper_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby `Geom` object |
| TG geometry in `Geom` | TG parser/constructor | `tg_geom_free` | `Geom` wrapper |
| `tg_index_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby `Index` object |
| Index entries array | `calloc` | `free` | `Index` |
| TG geometry via `:geojson` / `:wkb` | TG parser | `tg_geom_free` | `Index` |
| TG geometry via `:geom` | Existing `Geom` wrapper | Existing `Geom` wrapper | Borrowed by `Index` through `geom_owner` |
| rtree internals | custom `tg_rtree_malloc` with header | custom `tg_rtree_free` | rtree / `Index` accounting |
| Ruby ids | Ruby VM | Ruby GC | Marked and compacted by `Index` |

`ObjectSpace.memsize_of(index)` includes entries, owned TG geometries, and exact
rtree allocation bytes. Borrowed geometries are not double-counted by the index.

See `docs/MEMORY_OWNERSHIP.md` for the full table and cleanup rules.

## Concurrency model

`Index` and `Geom` are immutable after construction. Concurrent read-only use
from normal Ruby threads is supported by design and covered by tests.

The first release keeps GVL for parse, write, query, batch, and rtree build/free
paths. This is intentional: the rtree allocator calls Ruby GC accounting APIs,
and no-GVL execution would require separate input-copying and allocator-accounting
design.

No Ractor support is claimed.

See `docs/CONCURRENCY.md` and `docs/RACTOR.md`.

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
```

By default, benchmarks use a fast local matrix. Set `TGEOMETRY_BENCH_FULL=1` for
the larger matrix where supported.

The repository benchmarks are engineering tools, not universal marketing claims.
Do not copy upstream TG C benchmark numbers as Ruby gem performance claims.

## Limitations

`tg_geometry` is not a full GIS system.

Not included in this release:

- geocoding;
- routing;
- projections;
- geodesic distance/area;
- buffer / union / difference / overlay result geometry operations;
- nearest POI index;
- Rails dependency in the core extension;
- Redis or external service dependency;
- public callback/search APIs;
- Ractor support claim;
- no-GVL execution claim;
- universal `:auto` strategy.

TG works in planar XY coordinates. If lon/lat coordinates are passed in, length,
area, and perimeter-style values are in input coordinate units, not meters.
Use PostGIS, GEOS, PROJ, or other GIS tooling when full GIS functionality is
needed.

## Development

```bash
bundle install
bundle exec rake compile
bundle exec rake spec
```

Useful targeted checks:

```bash
bundle exec rspec spec/block_12_batch_packed_spec.rb
bundle exec rspec spec/block_14_memory_gc_hardening_spec.rb
bundle exec rspec spec/block_20_concurrency_spec.rb
bundle exec rspec spec/block_20_fuzz_spec.rb
```

Memory-tool CI jobs for ASAN and Valgrind are intentionally left as OPEN QUESTION
placeholders until the exact setup is approved. Do not replace them with guessed
configuration.

## License

MIT. Vendored upstream license files for `tidwall/tg` and `tidwall/rtree.c` are
included under `ext/tg_geometry/vendor/`.
