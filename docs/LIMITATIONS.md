# Limitations

`tg_geometry` is a small in-process geometry predicate and geofencing-oriented Index gem. It is not a full GIS system.

## Not included

The first release does not provide:

- geocoding;
- routing;
- projections;
- geodesic distance or area;
- buffer / union / difference / intersection-result geometry operations;
- nearest POI index;
- full PostGIS / GEOS replacement behavior;
- parsed GeoJSON Feature properties as Ruby Hashes;
- Line/Ring/Polygon public constructors from Ruby coordinate arrays;
- user callback APIs;
- Ractor support claim;
- no-allocation point query shortcuts;
- geodesic/projection helpers;
- mutable coordinate APIs;
- global TG environment configuration or allocator override APIs.

## Planar XY only

TG works in planar XY coordinates. If users pass lon/lat, area, length, and perimeter concepts are in input coordinate units, not meters. Real-world distance and area need explicit projection/geodesic tooling outside this first-release core.

## Boundary semantics

Geofencing defaults to `predicate: :covers` because boundary points should count as inside. `predicate: :contains` is stricter and may exclude boundary points.

## Point query performance

The current implementation allocates a temporary TG point geometry per point query. This is intentional for exact `covers` and `contains` semantics. A no-allocation point path can be added later only after tests prove equivalent boundary behavior and benchmarks prove value.

## Build peak memory

`TG::Geometry::Index.build` is atomic and immutable. During reload, memory peak can include both the old Index and the new Index, plus original Ruby entry arrays and temporary build state. This is deliberate: exception safety and read-only concurrency are more important than streaming mutation in the first release.

## IDs are returned by reference

Index query methods return the same Ruby id objects stored in entries. They are not duplicated, frozen, stringified, or copied. If an id object is mutable, user code owns that mutability risk.

## Windows

Windows is not supported in the first release. The intended first-release platforms are Linux and macOS on x86_64 and arm64.

## Expansion limitations

No automatic strategy resolver is enabled in the first public release. For unusual datasets, especially heavily overlapping zones or workloads dominated by first-entry hits, choose `:flat` or `:rtree` explicitly after benchmarking.

`TG::Geometry::Registry` is application sugar, not a distributed registry. It has no Redis dependency, no background reload thread, and no hidden global singleton.

`TG::Geometry::ActiveRecordSource` is optional Ruby helper code. It does not install Rails reload hooks, generators, or background jobs.

`TG::Geometry::Geom#extra_json` returns raw copied JSON text. It does not parse properties and does not expose Feature child objects. Z/M metadata is readable and point Z/M constructors exist, but broad Line/Ring/Polygon/Multi* construction remains out of scope until a separate ownership model is specified.

## Expansion Blocks F-H

Callback/search APIs, no-allocation point query optimization, and geodesic/projection helpers remain OPEN QUESTION scope. See `docs/EXPANSION_E_TO_H_STATUS.md`.
