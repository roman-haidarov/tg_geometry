# Geometry-based index queries

v0.3.0 adds geometry query methods to `TG::Geometry::Index`:

| Method | Predicate direction | Boundary semantics |
| --- | --- | --- |
| `intersecting_geom_ids(query)` | `stored_geom` intersects `query` | any intersection |
| `covering_geom_ids(query)` | `stored_geom` covers `query` | boundary included |
| `containing_geom_ids(query)` | `stored_geom` contains `query` | strict interior; boundary excluded |

The direction is always from the stored geometry to the query geometry. For example, `covering_geom_ids(query)` asks which indexed geometries cover the query.

The `predicate:` option on `Index.build` affects only the legacy point-based methods:

- `find_covering(x, y)`
- `covering_ids(x, y)`
- `covering_ids_batch_packed(packed_doubles)`

It does not affect `intersecting_geom_ids`, `covering_geom_ids`, or `containing_geom_ids`.

Results are arrays of ids in insertion order. Duplicate ids are preserved if duplicate ids were inserted.

In v0.3.0 these operations run under the GVL. The heavy C phase is structured without Ruby API calls so it can be made no-GVL-safe later after benchmarking, but there is no public `release_gvl:` knob.

## Point-to-geometry distance queries

Distance APIs are intentionally split by unit/model:

| Receiver | Method | Units / result |
| --- | --- | --- |
| `Geom` | `distance_to_lnglat_meters(lng, lat)` | `Float`, approximate meters |
| `Geom` | `boundary_distance_to_lnglat_meters(lng, lat)` | `Float`, approximate meters to nearest boundary/segment/point |
| `Geom` | `nearest_point_lnglat(lng, lat)` | `[lng, lat]`, raw planar nearest point |
| `Geom` | `distance_to_xy(x, y)` | `Float`, input coordinate units |
| `Geom` | `boundary_distance_to_xy(x, y)` | `Float`, input coordinate units |
| `Geom` | `nearest_point_xy(x, y)` | `[x, y]`, input coordinate units |
| `Index` | `within_distance_lnglat_meters(lng, lat, radius_m, sort: false)` | `[[id, distance_m], ...]` |
| `Index` | `within_distance_ids_lnglat_meters(lng, lat, radius_m)` | `[id, ...]` |
| `Index` | `within_distance_xy(x, y, radius, sort: false)` | `[[id, distance], ...]` |
| `Index` | `within_distance_ids_xy(x, y, radius)` | `[id, ...]` |

Coordinate order is always `(lng, lat)` for `*_lnglat_meters` and `(x, y)` for `*_xy`. There is no `metric:` keyword and no automatic coordinate-system detection.

Areal geometry semantics:

- `Polygon`, `MultiPolygon`, and areal `GeometryCollection` members return `0.0` from `distance_to_*` when the query point is inside the covered area or on the boundary.
- Holes are excluded: a point inside a hole measures to the nearest hole-ring segment.
- `boundary_distance_to_*` always measures to the nearest boundary/ring/segment. For an interior point it does not return `0.0` merely because the point is covered.
- `nearest_point_*` returns the nearest boundary point for areal types, including interior queries.

Non-areal geometry semantics:

- `Point` / `MultiPoint` measure to the nearest point.
- `LineString` / `MultiLineString` measure to the nearest segment.
- `GeometryCollection` returns the minimum over measurable members; empty members are skipped. A geometry with no measurable component raises `TG::Geometry::ArgumentError`.

Distance methods for lng/lat geometries return approximate meters using a per-query local equirectangular frame. Segments are GeoJSON straight coordinate segments, not great-circle arcs. This is geofencing-grade metric distance, not geodesy. Accuracy is intended for local geofencing and degrades with latitude separation.

The lng/lat metric is raw planar lng/lat. It does not wrap longitude at `+/-180`, does not split antimeridian boxes, and does not normalize returned `nearest_point_lnglat` longitudes. A geometry near `179.9` and a query near `-179.9` are treated as far apart in raw planar coordinates, consistent with `covers_xy?`. Cut antimeridian-crossing data at `+/-180` before import.

`Index#within_distance_*` uses two phases: one bbox prefilter through the existing index path and one exact distance filter over candidates. The returned distance is the exact filter value; it is not discarded and recomputed later. `sort: true` sorts the filtered pair array by ascending distance. The ids-only variants intentionally reject `sort:`.

Radius-query benchmarks should be read as `rtree prefilter + exact filter` versus brute-force full index scan. They measure the value of avoiding a full scan for selective radii, not a standalone speedup of the exact distance primitive. Tiny indexes and radii covering the whole data extent are benchmarked separately because the prefilter can become neutral or slower there.

No kNN, `nearest_ids`, rtree nearest traversal, projection/reprojection, signed distance, geometry-to-geometry distance, or geodesic distance is implemented by these APIs.
