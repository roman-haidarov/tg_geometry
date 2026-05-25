# Low-level geometry wrappers

This document covers Expansion Block E.

## Scope

The first low-level API exposes borrowed read-only views over TG child types:

- `TG::Geometry::Line`
- `TG::Geometry::Ring`
- `TG::Geometry::Polygon`
- `TG::Geometry::Segment`

These wrappers are created only from `TG::Geometry::Geom`, `TG::Geometry::Polygon`, `TG::Geometry::Line`, and `TG::Geometry::Ring` methods. Public `.allocate` is disabled for all four classes.

## Ownership model

TG child accessors return borrowed pointers owned by the parent `struct tg_geom`. The Line/Ring/Polygon Ruby wrappers therefore store:

- `geom_owner`: the original `TG::Geometry::Geom` Ruby object;
- a borrowed `const struct tg_line *`, `const struct tg_ring *`, or `const struct tg_poly *` pointer.

The wrappers mark `geom_owner` with `rb_gc_mark_movable` and update it in `dcompact` with `rb_gc_location`. They do not call `tg_line_free`, `tg_ring_free`, or `tg_poly_free` for borrowed children.

`TG::Geometry::Segment` is a value wrapper over a copied `struct tg_segment`, so it does not store `geom_owner` and does not free any TG-owned pointer.

Allocator pairs:

Resource | Allocator | Deallocator | Owner | Notes
--- | --- | --- | --- | ---
`tg_line_wrapper_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby object | borrowed pointer, marks parent `geom_owner`
`tg_ring_wrapper_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby object | borrowed pointer, marks parent `geom_owner`
`tg_polygon_wrapper_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby object | borrowed pointer, marks parent `geom_owner`
`tg_segment_wrapper_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby object | owns copied `struct tg_segment` value
TG child pointers | parent `struct tg_geom` | parent `TG::Geometry::Geom` dfree | parent geometry | never freed by child wrappers

## Public API

### `TG::Geometry::Geom#point`

Returns `[x, y]` for point geometries. Returns `nil` for non-point geometries.

### `TG::Geometry::Geom#line`

Returns a frozen `TG::Geometry::Line` for LineString geometries. Returns `nil` otherwise.

### `TG::Geometry::Geom#polygon`

Returns a frozen `TG::Geometry::Polygon` for Polygon geometries. Returns `nil` otherwise.

### `TG::Geometry::Line`

Methods:

- `bbox -> TG::Geometry::Rect`
- `num_points -> Integer`
- `point_at(index) -> [Float, Float]`
- `points -> Array<[Float, Float]>`
- `num_segments -> Integer`
- `segment_at(index) -> TG::Geometry::Segment`
- `segments -> Array<TG::Geometry::Segment>`
- `length -> Float`
- `clockwise? -> Boolean`

`length` is measured in input coordinate units. For lon/lat data this is not meters.

### `TG::Geometry::Ring`

Methods:

- `bbox -> TG::Geometry::Rect`
- `num_points -> Integer`
- `point_at(index) -> [Float, Float]`
- `points -> Array<[Float, Float]>`
- `num_segments -> Integer`
- `segment_at(index) -> TG::Geometry::Segment`
- `segments -> Array<TG::Geometry::Segment>`
- `area -> Float`
- `perimeter -> Float`
- `clockwise? -> Boolean`
- `convex? -> Boolean`

`area` and `perimeter` are measured in input coordinate units. For lon/lat data these are not square meters or meters.

### `TG::Geometry::Polygon`

Methods:

- `bbox -> TG::Geometry::Rect`
- `exterior_ring -> TG::Geometry::Ring`
- `num_holes -> Integer`
- `hole_at(index) -> TG::Geometry::Ring`
- `holes -> Array<TG::Geometry::Ring>`
- `clockwise? -> Boolean`

`hole_at` rejects out-of-range indexes with `TG::Geometry::ArgumentError`.

### `TG::Geometry::Segment`

Methods:

- `a -> [Float, Float]`
- `b -> [Float, Float]`
- `points -> [[Float, Float], [Float, Float]]`
- `bbox -> TG::Geometry::Rect`
- `intersects?(other_segment) -> Boolean`

Segments are copied by value from a line or ring. They do not keep or free borrowed TG pointers.

## Error paths

- Wrong wrapper type is handled by `TypedData_Get_Struct` and raises `TypeError`.
- Out-of-range child indexes raise `TG::Geometry::ArgumentError`.
- Child wrappers are immutable and cannot be constructed directly from Ruby.

## Not implemented in this block

- Low-level constructors for Ring, Line, or Polygon.
- Mutable coordinate access.
- Polygon `area` / `perimeter` convenience aggregation. Ring-level values are exposed first to avoid undocumented assumptions about hole orientation and aggregation semantics.
- Geodesic length/area.
