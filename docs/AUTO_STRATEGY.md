# Auto strategy status

`strategy: :auto` is not exposed in the first public release.

The release-core contract only enables explicit `strategy: :flat` and `strategy: :rtree`. Automatic threshold selection requires a complete project-owned benchmark matrix and explicit approval before it can become public API.

Use `benchmark/flat_vs_rtree.rb` to compare strategies for a workload, then pass the chosen strategy explicitly:

```ruby
index = TG::Geometry::Index.build(entries, via: :geojson, strategy: :rtree)
# or
index = TG::Geometry::Index.build(entries, via: :geojson, strategy: :flat)
```

Do not infer a universal crossover from rtree internals or from a partial benchmark run.
