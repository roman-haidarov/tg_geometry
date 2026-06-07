# Concurrency

## Immutable read model

`TG::Geometry::Geom` and `TG::Geometry::Index` are immutable after construction. The Index stores stable entry pointers before inserting payloads into rtree, and entries are never reallocated after build.

This supports concurrent read-only use from normal Ruby threads. Public query methods do not mutate Index state and do not store persistent match marks inside the Index.

## Reload pattern

Use reference replacement, not mutation:

```ruby
old_index = @index
new_index = TG::Geometry::Index.build(entries, via: :geojson, strategy: :rtree)
@index = new_index
```

A reader that already captured `old_index` can finish safely. Later readers can use `new_index`.

## GVL policy

Most short operations keep the GVL:

- parse helpers;
- writers;
- `Geom` predicates;
- Index point queries;
- Index rect queries;
- packed batch queries;
- rtree build/free and Ruby result materialization.

FeatureSource bulk methods are the exception. Their heavy source-processing phase runs without the GVL:

- `_file` methods open/read the file in C outside the GVL;
- JSON validation and `tidwall/json.c` traversal run outside the GVL;
- geometry validation/parsing with TG runs outside the GVL.

That phase uses only C-owned memory. It does not create Ruby objects, call Ruby methods, raise Ruby exceptions, or call `rb_gc_adjust_memory_usage`. Ruby ids/strings/reports and Index ownership transfer happen only after the GVL is reacquired.

FeatureSource uses only Ruby VM no-GVL APIs for this phase. On Rubies exposing `RB_NOGVL_OFFLOAD_SAFE`, the heavy function is marked offload-safe for the VM. On older Rubies it uses `rb_thread_call_without_gvl`. The gem does not call `rb_fiber_scheduler_block` / `rb_fiber_scheduler_unblock` and does not run a manual scheduler worker from C. Therefore FeatureSource releases the GVL for other Ruby threads, but explicit Fiber scheduler friendliness is only claimed when the Ruby VM provides the offload-safe no-GVL API.

## Rtree owner thread-local

Rtree build uses a `_Thread_local` owner so exact allocation accounting can attribute rtree internals to the correct Index. The owner is saved and restored with an ensure path.

Concurrent builds on different OS threads are supported by thread-local owner separation. Re-entrant same-thread builds from callbacks are not part of the first-release API because no public callbacks are exposed.

## Rtree callback safety

Rtree search callbacks do not touch Ruby objects. They only mark candidate ordinals in C memory. Ruby arrays are built after `rtree_search` returns.

Callback rules:

- no `rb_yield`;
- no Ruby Array push;
- no Ruby exception/longjmp;
- no Ruby allocation;
- no Index mutation.

## Ractor

No Ractor support is claimed. Frozen native wrappers are not advertised as Ractor-shareable objects. Normal Ruby thread read-only access is the supported concurrency model.

## Falcon / Async

No Falcon or Async performance claim is made. `benchmark/falcon_concurrency.rb` is only a baseline placeholder that records normal Ruby thread read behavior and states the Falcon/Async benchmark as an open setup question.

## Low-level borrowed wrappers

`TG::Geometry::Line`, `TG::Geometry::Ring`, `TG::Geometry::Polygon`, and borrowed GeometryCollection child `TG::Geometry::Geom` wrappers are immutable borrowed wrappers. They do not mutate or free child TG pointers. Each wrapper marks and compacts the parent `TG::Geometry::Geom` through `geom_owner`, so the parent native geometry remains alive while a child wrapper is in use.

## Distance query concurrency

Point-to-geometry distance methods are read-only over immutable `Geom` and `Index` objects. They do not mutate geometry, mutate index entries, cache query state on receivers, or add persistent native memory. They do not call `rb_gc_adjust_memory_usage`.

`Index#within_distance_*` uses rtree callbacks only to mark candidate ordinals in C memory. Ruby arrays and `[id, distance]` pairs are materialized after `rtree_search` returns under the GVL. The callback safety rules above apply unchanged.

Distance methods deliberately keep the GVL. Do not treat them as no-GVL, Falcon/Async-aware, or Ractor-shareable APIs.
