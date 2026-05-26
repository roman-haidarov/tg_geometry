# Error handling

## Exception hierarchy

```ruby
module TG
  module Geometry
    class Error < StandardError; end
    class ParseError < Error; end
    class ArgumentError < ::ArgumentError; end
    class FrozenIndexError < Error; end
  end
end
```

`TG::Geometry::ArgumentError` inherits from Ruby `::ArgumentError` so invalid user arguments remain rescuable through the standard Ruby error class.

## Mapping

Condition | Ruby exception | Cleanup rule
--- | --- | ---
TG parse error | `TG::Geometry::ParseError` | copy error string, free TG error geometry, then raise
invalid symbol option | `TG::Geometry::ArgumentError` | no native state if validated before allocation; otherwise dispose partial state
nil id | `TG::Geometry::ArgumentError` | partial Index disposed immediately
wrong object class/type | `TypeError` | use Ruby type checks / `TypedData_Get_Struct`
non-finite coordinate | `TG::Geometry::ArgumentError` | reject before point or rect work where possible
entries allocation failure | `NoMemoryError` | no entries state installed, or partial wrapper left empty
rtree allocation failure | `NoMemoryError` | dispose rtree/entries/owned geometries immediately
match buffer allocation failure | `NoMemoryError` | free any candidate buffer / query point before raising
internal writer size mismatch | `TG::Geometry::Error` | no native ownership change

## Parse errors

TG parser failures return a geometry object that carries an error string. That object owns resources and must be freed.

The implementation copies the error string before `tg_geom_free`, then raises `TG::Geometry::ParseError` from the copied Ruby string. It never reads `tg_geom_error` after freeing the error geometry.

## Build failure atomicity

`TG::Geometry::Index.build` is atomic: users either receive a frozen fully built Index or an exception. They never observe a partially built Index.

The build body is protected with `rb_protect`. If it raises, `index_dispose` runs immediately and frees:

1. rtree internals;
2. initialized owned geometries;
3. entries array;
4. byte counters.

## Rtree callbacks

Rtree allocator callbacks return `NULL` on OOM. Rtree search callbacks record failure in C state or avoid failure by preallocating local mark buffers before search. Ruby exceptions do not longjmp through `rtree.c` traversal.

## Writer safety

Text writers allocate Ruby strings with one extra byte for the null terminator, call the TG writer with that capacity, then set the Ruby string length back to the required content length. WKB allocates exactly the required binary length and associates `ASCII-8BIT` encoding.

## FeatureSource errors and reports

FeatureSource validates the whole JSON source before traversal. Malformed JSON raises `TG::Geometry::ParseError` with the byte offset when available. A root that is not a GeoJSON `FeatureCollection`, or a `features` member that is missing/not an array, also raises `TG::Geometry::ParseError`.

Invalid feature data is fail-fast by default:

- missing/non-object geometry;
- missing/non-string `geometry.type`;
- unsupported geometry type;
- invalid properties type for `read_features_*`;
- TG parse error for a geometry range.

`on_invalid: :skip` is accepted only with `report: true`. In report mode invalid features increment `:skipped`; filtered geometry types increment `:filtered`.

Missing or `null` ids follow `on_missing_id:`:

- `:raise` raises `TG::Geometry::ArgumentError`;
- `:skip` requires `report: true` and records a skipped feature;
- `:ordinal` creates an id like `"feature/12"`.

Invalid id types raise `TG::Geometry::ArgumentError`: boolean, object, array, fractional number, and non-integer exponent number are not accepted FeatureSource ids.

Stored report errors have this shape:

```ruby
{ feature_index: Integer, byte_offset: Integer, reason: String }
```

`max_errors:` caps the number of stored error Hashes, not the exact skipped count. TG geometry parse errors copy `tg_geom_error` before freeing the temporary TG error geometry and include feature index / byte offset in the raised message.
