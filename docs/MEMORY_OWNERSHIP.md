# Memory ownership

This document describes allocator pairs, ownership, cleanup, and GC accounting for `tg_geometry`.

## Ownership table

Resource | Allocator | Deallocator | Owner | Notes
--- | --- | --- | --- | ---
`tg_index_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby object | freed in `index_free`
entries array | `calloc` | `free` | `TG::Geometry::Index` | exact-size, stable after build; accounted in `entries_bytes`
rtree internals | `tg_rtree_malloc` / plain `malloc` + header | `tg_rtree_free` / plain `free` | rtree | exact `rtree_bytes`; strict non-NULL current owner
TG geometry owned | `tg_parse_*_ix` | `tg_geom_free` | `TG::Geometry::Index` | `via: :geojson` / `via: :wkb`; accounted in `owned_geom_bytes_total`
TG geometry borrowed | `TG::Geometry::Geom` wrapper | `TG::Geometry::Geom` dfree | `TG::Geometry::Geom` | Index holds `geom_owner`; Index does not free borrowed geometry
TG parse error geometry | `tg_parse_*` | `tg_geom_free` | local parse scope | copy error string before free
Ruby id | Ruby VM | Ruby GC | `TG::Geometry::Index` entry | mark movable + compact
`geom_owner` | Ruby VM | Ruby GC | `TG::Geometry::Index` entry | mark movable + compact
`tg_geom_wrapper_t` owned | `TypedData_Make_Struct` | `ruby_xfree` | Ruby object | owns one `struct tg_geom *`; `geom_free` calls `tg_geom_free`
`tg_geom_wrapper_t` borrowed | `TypedData_Make_Struct` | `ruby_xfree` | Ruby object | borrowed child `struct tg_geom *`; marks/compacts parent `geom_owner`; does not call `tg_geom_free`
TG geometry inside owned wrapper | `tg_parse_*` / constructor | `tg_geom_free` in `geom_free` | `TG::Geometry::Geom` wrapper | one `tg_geom_free` per parse/constructor
query point geometry | `tg_geom_new_point` | `tg_geom_free` | local query scope | one point allocation per point query in first release
match mark buffer | `calloc` | `free` | local query scope | rtree callback writes C marks only; no Ruby objects touched
`tg_line_wrapper_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby object | borrowed child pointer; marks/compacts parent `geom_owner`; does not call `tg_line_free`
`tg_ring_wrapper_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby object | borrowed child pointer; marks/compacts parent `geom_owner`; does not call `tg_ring_free`
`tg_polygon_wrapper_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby object | borrowed child pointer; marks/compacts parent `geom_owner`; does not call `tg_poly_free`
`tg_segment_wrapper_t` | `TypedData_Make_Struct` / Ruby allocator | `ruby_xfree` | Ruby object | owns a by-value `struct tg_segment`; no borrowed pointer and no TG free call
TG child pointers | parent `struct tg_geom` | parent `TG::Geometry::Geom` dfree | parent geometry | borrowed by Geom/Line/Ring/Polygon wrappers; never freed directly

## GC memory pressure

Native memory that is invisible to Ruby object slots is reported through `rb_gc_adjust_memory_usage` and exposed diagnostically through `ObjectSpace.memsize_of` where Ruby asks the data type for native size.

Tracked state:

- `TG::Geometry::Geom`: `geom_bytes` only for owned wrappers; borrowed child wrappers report only wrapper size;
- `TG::Geometry::Index`: `entries_bytes`, `owned_geom_bytes_total`, `rtree_bytes`.

Every successful `+N` adjustment has one matching `-N` in the relevant dispose/free path. Dispose is idempotent: after freeing, pointers and byte counters are zeroed.

## Partial build cleanup

`TG::Geometry::Index.build` uses an explicit `initialized` counter. Only fully written entries are marked, compacted, queried, or disposed.

Entry write order:

1. validate id and value type;
2. parse or borrow the native geometry into a local variable;
3. build a complete local `tg_index_entry_t`;
4. write the local entry into the entries array;
5. increment `initialized`;
6. update bbox and byte accounting.

If build raises after native allocation starts, `index_dispose` runs immediately. Failed builds do not wait for Ruby GC to eventually clean large partial native state.

## Rtree allocator

`rtree.c` expects malloc-style allocation callbacks, so the rtree allocator uses plain `malloc` and returns `NULL` on OOM. It must not call `ruby_xmalloc` and must not raise Ruby exceptions from inside rtree callbacks.

Each rtree allocation stores this header before the returned pointer:

```c
typedef struct {
    tg_index_t *owner;
    size_t size;
} tg_rtree_alloc_header_t;
```

The current owner is `_Thread_local`. It is saved before rtree build, set for `rtree_new_with_allocator` and every `rtree_insert`, then restored with `rb_ensure`.

Allocator calls use `rb_gc_adjust_memory_usage`, so rtree build and free must run with the GVL held.

## Borrowed geometry path

For `via: :geom`, the Index does not clone or copy the native geometry. It stores:

- `geom_owner = original TG::Geometry::Geom Ruby object`;
- `geom = borrowed native pointer`;
- `owned = false`;
- `geom_bytes = 0`.

The Index marks and compacts `geom_owner`, which keeps the owning Ruby wrapper alive after the caller drops its local variable. Borrowed geometry memory is not double-counted in `ObjectSpace.memsize_of(index)`.

## Owned geometry path

For `via: :geojson` and `via: :wkb`, the Index owns each parsed TG geometry. The Index calls `tg_geom_free` during dispose/free and subtracts exactly the bytes previously added to `owned_geom_bytes_total` and Ruby GC pressure.

## Low-level child wrappers

Expansion Block E exposes read-only borrowed wrappers for selected TG child types. `TG::Geometry::Line`, `TG::Geometry::Ring`, and `TG::Geometry::Polygon` keep the original parent `TG::Geometry::Geom` Ruby object in `geom_owner`. Their GC callbacks use `rb_gc_mark_movable` and `rb_gc_location`, matching the Index borrowed-geometry model.

Expansion Block J extends this model to borrowed `TG::Geometry::Geom` wrappers returned from GeometryCollection accessors. A borrowed `Geom` stores `owned = false`, `geom_bytes = 0`, and a `geom_owner` reference to the parent wrapper. Its free path only releases the Ruby wrapper struct; it never calls `tg_geom_free` on the borrowed child pointer. This keeps `ObjectSpace.memsize_of` from double-counting parent-owned native geometry.

`TG::Geometry::Segment` is different: it stores a `struct tg_segment` by value, not a borrowed pointer. It has no parent owner to mark and no TG deallocator to call.

Line/Ring/Polygon wrappers own only their small Ruby-allocated wrapper structs. They do not own the underlying `const struct tg_line *`, `const struct tg_ring *`, or `const struct tg_poly *`. Cleanup therefore only calls `ruby_xfree` for the wrapper; the parent `TG::Geometry::Geom` remains responsible for the single `tg_geom_free`.
