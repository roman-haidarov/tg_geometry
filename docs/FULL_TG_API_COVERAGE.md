# Full TG API coverage status

This document covers Expansion Block J as implemented in grouped, safe increments.

Block J does not mean exposing every upstream TG function in one change. The rule is: implement in groups, define ownership for each group, add tests, and avoid environment allocator overrides or global mutable settings.

## Implemented group: predicates

`TG::Geometry::Geom` now exposes additional read-only predicates:

- `equals?(other)`
- `disjoint?(other)`
- `within?(other)`
- `covers?(other)`
- `covered_by?(other)`
- `touches?(other)`
- `intersects_xy?(x, y)`
- `intersects_rect?(rect)`
- `intersects_rect?(min_x, min_y, max_x, max_y)`

Existing methods remain:

- `contains?(other)`
- `intersects?(other)`
- `covers_xy?(x, y)`

`equals?` intentionally does not change Ruby `==`, `eql?`, or `hash`. Ruby equality remains identity-based until that open API decision is explicitly made.

## Implemented group: geometry accessors

`TG::Geometry::Geom` now exposes safe read-only accessors for upstream geometry collections and coordinate metadata:

- `feature?`
- `feature_collection?`
- `empty?`
- `dims`
- `has_z?`
- `has_m?`
- `z`
- `m`
- `extra_coords`
- `num_points`
- `point_at(index)`
- `points`
- `num_lines`
- `line_at(index)`
- `lines`
- `num_polygons`
- `polygon_at(index)`
- `polygons`
- `num_geometries`
- `geometry_at(index)`
- `geometries`

`geometry_at` and `geometries` return borrowed immutable `TG::Geometry::Geom` wrappers. They keep the parent wrapper alive through `geom_owner`, mark it for GC, update it during compaction, and do not call `tg_geom_free` for the borrowed child pointer.

## Implemented group: point and empty constructors

Safe constructors are exposed only where ownership is simple and the returned object owns exactly one `struct tg_geom *`:

- `TG::Geometry.point(x, y)`
- `TG::Geometry.point_z(x, y, z)`
- `TG::Geometry.point_m(x, y, m)`
- `TG::Geometry.point_zm(x, y, z, m)`
- `TG::Geometry.empty_point`
- `TG::Geometry.empty_linestring`
- `TG::Geometry.empty_polygon`
- `TG::Geometry.empty_multipoint`
- `TG::Geometry.empty_multilinestring`
- `TG::Geometry.empty_multipolygon`
- `TG::Geometry.empty_geometrycollection`

Numeric coordinates are converted with `NUM2DBL` and rejected if NaN or Infinity.

## Implemented group: segments

`TG::Geometry::Segment` is a frozen value wrapper over one copied `struct tg_segment`.

Created by:

- `TG::Geometry::Line#segment_at(index)`
- `TG::Geometry::Line#segments`
- `TG::Geometry::Ring#segment_at(index)`
- `TG::Geometry::Ring#segments`

Methods:

- `a -> [Float, Float]`
- `b -> [Float, Float]`
- `points -> [[Float, Float], [Float, Float]]`
- `bbox -> TG::Geometry::Rect`
- `intersects?(other_segment)`

`TG::Geometry::Segment.allocate` is disabled. Segment wrappers do not borrow parent TG memory and do not call any TG free function.

## Still not implemented

The following remain outside this grouped implementation:

- Line/Ring/Polygon public constructors from Ruby arrays;
- MultiLineString / MultiPolygon constructors from low-level wrappers;
- callback/search APIs;
- nearest segment APIs;
- environment configuration functions;
- global allocator override;
- mutable settings;
- user callbacks inside C loops.

Those require separate ownership and callback-safety contracts before implementation.
