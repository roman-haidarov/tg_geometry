# FeatureSource

`TG::Geometry::FeatureSource` reads GeoJSON `FeatureCollection` sources at C level and extracts geometry ranges without building a Ruby `Hash` / `Array` tree for the whole document.

The feature exists for Rails-style import and geofencing workflows where the source data is a real GeoJSON file, not a hand-written polygon string.

## Public API

```ruby
TG::Geometry::FeatureSource.read_entries_file(path, **opts)
TG::Geometry::FeatureSource.read_entries_json(json_string, **opts)
TG::Geometry::FeatureSource.read_entries_io(io, **opts)

TG::Geometry::FeatureSource.read_features_file(path, **opts)
TG::Geometry::FeatureSource.read_features_json(json_string, **opts)
TG::Geometry::FeatureSource.read_features_io(io, **opts)

TG::Geometry::FeatureSource.build_index_file(path, **opts)
TG::Geometry::FeatureSource.build_index_json(json_string, **opts)
TG::Geometry::FeatureSource.build_index_io(io, **opts)
```

Source methods are explicit:

- `*_file(path)` treats the argument as a file path only.
- `*_json(json_string)` treats the argument as raw GeoJSON content only.
- `*_io(io)` calls `read` and treats the returned content as raw GeoJSON.

There is no path/content auto-detection.

## Return shapes

`read_entries_*` returns pairs suitable for `TG::Geometry::Index.build(..., via: :geojson)`:

```ruby
[[id, geometry_json_string], ...]
```

`read_features_*` also returns raw `properties` JSON for database imports:

```ruby
[[id, geometry_json_string, properties_json_string], ...]
```

`properties_json_string` is a JSON string, not a Ruby `Hash`.

## Report mode

With `report: true`, read methods return a Hash:

```ruby
{
  entries: [[id, geometry_json_string], ...],
  skipped: 0,
  filtered: 0,
  errors: [
    { feature_index: 1, byte_offset: 1234, reason: "missing geometry" }
  ]
}
```

For `read_features_*`, the collection key is `:features` instead of `:entries`.

`max_errors:` caps stored error Hashes only. `skipped` remains exact.

## Options

```ruby
id: ["properties", "@id"]
only: [:polygon, :multipolygon]
on_invalid: :raise
on_missing_id: :raise
report: false
max_errors: 100
geometry_index: :ystripes
```

`id:` may be an `Array<String>` or a simple dot-separated `String`. Array form is the primary API and is required for keys that cannot be represented safely with dot syntax.

Accepted ids:

- JSON string -> Ruby `String`
- JSON integer number -> Ruby `Integer`

Rejected ids:

- fractional/exponent non-integer number
- boolean
- object
- array

Missing or `null` ids follow `on_missing_id:`:

- `:raise` raises `TG::Geometry::ArgumentError`
- `:skip` skips the feature and requires `report: true`
- `:ordinal` creates a string id such as `"feature/12"`

`only:` defaults to polygons and multipolygons. `only: nil` disables geometry type filtering. A mismatch with `only:` is counted as `filtered`, not invalid.

`on_invalid: :skip` requires `report: true`. Silent loss of invalid features is not allowed.

## Direct index build

```ruby
index = TG::Geometry::FeatureSource.build_index_file(
  "zones.geojson",
  id: ["properties", "@id"],
  strategy: :rtree,
  predicate: :covers,
  geometry_index: :ystripes
)
```

`build_index_*` returns a frozen `TG::Geometry::Index`.

Unlike `read_entries_* + Index.build`, direct build does not allocate Ruby geometry JSON strings. It parses accepted geometry JSON ranges directly into owned native TG geometries and then builds the immutable index.

`build_index_*` does not accept `report: true` in this release.

## Validation

FeatureSource validates in two layers:

1. JSON syntax and FeatureCollection shape.
2. Each returned or indexed geometry is parsed with TG via `tg_parse_geojsonn_ix`.

A JSON object being syntactically valid is not enough for index correctness.

## Execution and GVL model

FeatureSource bulk work is split into two phases. The heavy phase runs without the Ruby GVL and uses only C-owned memory:

- `_file` methods copy the Ruby path, then open/read the file in C outside the GVL;
- JSON validation and `tidwall/json.c` traversal run outside the GVL;
- geometry validation/parsing with `tg_parse_geojsonn_ix` runs outside the GVL;
- no Ruby objects are created, no Ruby exceptions are raised, and no `rb_gc_adjust_memory_usage` calls happen in that phase.

After the heavy phase finishes, the GVL is reacquired to materialize Ruby ids/strings/reports or to transfer owned native geometries into a frozen `TG::Geometry::Index`.

When a Fiber scheduler is active, FeatureSource uses an internal worker thread for the heavy C-only phase and blocks/unblocks the current fiber through Ruby's scheduler API. The public API does not require an `offload:` option; bulk FeatureSource operations are scheduler-friendly by default.

## Memory model

FeatureSource reads the whole source into one owned C buffer. `tidwall/json.c` traversal is backed by that buffer.

For `read_entries_*` / `read_features_*`:

- returned geometry strings are copied into Ruby strings;
- returned properties strings are copied into Ruby strings or literal `"null"`;
- no returned value points into the freed C buffer.

For `build_index_*`:

- the source buffer remains alive until all geometry ranges are parsed;
- the final index owns native TG geometries and does not depend on the source buffer;
- Ruby ids are stored in native entries and marked/compacted by the index wrapper.

## Non-goals in this release

- no streaming parser;
- no NDGeoJSON / GeoJSONSeq;
- no gzip decompression;
- no FlatGeobuf;
- no Ruby callback/yield API;
- no property parsing into Ruby Hashes;
- no general JSON query API.
