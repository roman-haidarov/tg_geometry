# Architecture

`tg_geometry` is a Ruby C extension around vendored `tidwall/tg`, `tidwall/rtree.c`, and `tidwall/json.c`.

The public Ruby API is under `TG::Geometry`. The native extension owns the hot paths for parsing, predicates, indexing, batch point lookup, and GeoJSON FeatureCollection extraction.

## Immutable geometry wrappers

`TG::Geometry::Geom` wraps one TG geometry pointer. It is frozen before being returned to Ruby and does not expose manual free, close, detach, or replacement APIs.

Owned wrappers call `tg_geom_free` from their typed-data free function and adjust Ruby GC memory pressure using the cached TG memory size.

Borrowed child wrappers, where exposed, keep the parent `Geom` alive through a Ruby owner reference and do not free the borrowed TG pointer directly.

## Rect

`TG::Geometry::Rect` is a small immutable Ruby object for bounding boxes and rectangle queries. It rejects non-finite coordinates and invalid coordinate order.

## Immutable Index

`TG::Geometry::Index` is built once and then read-only. Reloading is done by building a new index and swapping the Ruby reference.

Index entries store:

- Ruby id;
- optional Ruby geometry owner for borrowed `via: :geom` entries;
- TG geometry pointer;
- cached bbox;
- insertion ordinal;
- ownership flag and native byte count.

For owned inputs (`via: :geojson` / `via: :wkb`), the index owns the parsed TG geometry. For borrowed inputs (`via: :geom`), the index marks the source `Geom` wrapper so the native pointer remains valid.

## Flat and rtree strategies

`:flat` scans entries in insertion order.

`:rtree` uses `tidwall/rtree.c` as a bbox prefilter. Query results are still returned in insertion order, not rtree traversal order. Every bbox/rtree candidate is filtered with exact TG geometry predicates before returning ids.

Rtree memory is accounted exactly through a custom malloc/free allocator with per-allocation headers.

## FeatureSource

`TG::Geometry::FeatureSource` reads a full GeoJSON FeatureCollection into an owned C buffer, validates it with `json_validn`, traverses it with `json.c`, and extracts raw geometry/properties ranges while the buffer is alive.

`read_entries_*` and `read_features_*` copy returned geometry/properties strings into Ruby strings before the source buffer is freed.

`build_index_*` parses geometry ranges directly into native index entries and does not allocate Ruby geometry strings.

## Concurrency

`Geom` and `Index` are immutable after construction. Concurrent read-only use from normal Ruby threads is supported. The implementation keeps the GVL for parse, write, query, batch, FeatureSource, and rtree build/free paths.
