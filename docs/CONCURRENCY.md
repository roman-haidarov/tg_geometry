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

The first release does not release the GVL for:

- parse;
- writers;
- `Geom` predicates;
- Index point queries;
- Index rect queries;
- packed batch queries;
- index build/free.

This is intentional. Incorrect no-GVL code is worse than keeping GVL. Future no-GVL work requires benchmark evidence, input copying rules, and no Ruby C API calls outside the GVL.

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

No Ractor support is claimed. Expansion Block I records the current boundary: frozen native wrappers are not treated as Ractor-shareable objects, and tests assert that `Ractor.shareable?` remains false for `TG::Geometry::Geom`, `TG::Geometry::Rect`, and `TG::Geometry::Index`.

See `docs/RACTOR.md` for the unsupported-boundary notes and the requirements before this status can change.

## Falcon / Async

No Falcon or Async performance claim is made. `benchmark/falcon_concurrency.rb` is only a baseline placeholder that records normal Ruby thread read behavior and states the Falcon/Async benchmark as an open setup question.

## Low-level borrowed wrappers

`TG::Geometry::Line`, `TG::Geometry::Ring`, `TG::Geometry::Polygon`, and borrowed GeometryCollection child `TG::Geometry::Geom` wrappers are immutable borrowed wrappers. They do not mutate or free child TG pointers. Each wrapper marks and compacts the parent `TG::Geometry::Geom` through `geom_owner`, so the parent native geometry remains alive while a child wrapper is in use.
