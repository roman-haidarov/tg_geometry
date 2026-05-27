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
