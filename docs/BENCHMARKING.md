# Benchmarking

Benchmarks are engineering tools for this gem. They are not marketing claims.

Do not copy upstream TG C benchmark numbers into `tg_geometry` docs. Ruby C extension boundary cost, Ruby object handling, Index construction, batch result arrays, and GC behavior must be measured in this project.

## Required scripts

The repository includes these benchmark entry points:

- `benchmark/parse_throughput.rb`
- `benchmark/gvl_threshold.rb`
- `benchmark/flat_vs_rtree.rb`
- `benchmark/batch_packed_vs_loop.rb`
- `benchmark/falcon_concurrency.rb`
- `benchmark/objectspace_memsize.rb`
- `benchmark/rss_stability.rb`

Run after compiling the extension:

```sh
bundle exec rake compile
ruby benchmark/flat_vs_rtree.rb
```

By default, scripts use a reduced local set of entry sizes so they can be run quickly while developing. Full first-release benchmark scenarios are enabled with:

```sh
TGEOMETRY_BENCH_FULL=1 ruby benchmark/flat_vs_rtree.rb
```

## Scenarios

Benchmark generators cover:

- entry counts: 100, 500, 1K, 5K, 50K;
- compact bboxes;
- long thin bboxes;
- overlapping zones;
- point queries;
- viewport rect queries;
- flat vs rtree;
- scalar vs packed batch;
- parse small/medium/large geometry strings;
- RSS stability over repeated build/query/free.

## Output format

Scripts print line-oriented key/value records such as:

```text
kind=compact n=1000 query=point lon=0.4 lat=0.4 flat_sec=... rtree_sec=... flat_qps=... rtree_qps=...
```

These records are intentionally plain text so they can be redirected to files and compared across machines.

## No `:auto` strategy yet

The first release does not expose `strategy: :auto`. Choosing a threshold requires project-owned benchmark output across the required scenario matrix. Internal rtree constants such as leaf capacity are not a flat-vs-rtree crossover threshold.

## GVL threshold

`benchmark/gvl_threshold.rb` records baseline parse wall time for several valid WKT payload sizes while the first release keeps the GVL. It does not enable no-GVL execution. A future no-GVL implementation requires a separate design because Ruby C API calls and `RSTRING_PTR` lifetimes are not valid outside the GVL.

## RSS stability

`benchmark/rss_stability.rb` reports start, peak, finish, and delta RSS while repeatedly building, querying, and releasing indexes. CI thresholds should be chosen from observed baseline data on the target CI image, not guessed.

## Falcon / Async

The first release does not claim Falcon or Async behavior. A dedicated Falcon/Async benchmark remains an open setup item until the dependency and scenario are approved.

## Expansion Block A: auto strategy threshold

`strategy: :auto` remains postponed for the first public release. A future implementation must use a complete project-owned benchmark matrix and document the selected threshold before exposing the public option.
