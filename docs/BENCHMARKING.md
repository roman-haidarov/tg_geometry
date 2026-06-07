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
- `benchmark/distance_point_geom.rb`
- `benchmark/distance_within_radius.rb`
- `benchmark/distance_memory_accounting.rb`

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
- RSS stability over repeated build/query/free;
- point-to-geometry distance over different vertex counts;
- radius search as `rtree prefilter + exact filter` versus brute-force full index scan;
- tiny-index/full-extent radius cases where the rtree prefilter may not help;
- distance receiver memory accounting before and after repeated calls.

## Output format

Scripts print line-oriented key/value records such as:

```text
kind=compact n=1000 query=point lon=0.4 lat=0.4 flat_sec=... rtree_sec=... flat_qps=... rtree_qps=...
```

These records are intentionally plain text so they can be redirected to files and compared across machines.


## Distance benchmarks

`benchmark/distance_point_geom.rb` measures point-to-geometry distance method cost over geometries with different vertex counts. The `*_lnglat_meters` rows measure the local equirectangular frame overhead; they are not geodesic benchmarks.

`benchmark/distance_within_radius.rb` compares two query strategies:

- `rtree_prefilter_exact_filter`: existing rtree bbox prefilter followed by exact distance filtering;
- `brute_force_full_scan`: direct scan over every geometry with the same exact distance method.

Any speedup ratio from this script is a prefilter-vs-full-scan result. It must not be described as “distance is N times faster”. The benchmark intentionally includes selective-radius cases where the index should help and tiny-index/full-extent-radius cases where the prefilter may be neutral or slower. If rtree still wins on a machine, document that measured result instead of inventing a crossover.

`benchmark/distance_memory_accounting.rb` checks `ObjectSpace.memsize_of` before and after repeated distance calls. The expected receiver `delta B` is `0`; Ruby result allocations are still expected for returned arrays and `[id, distance]` pairs.

For noisy rows, especially short selective radius runs, increase timing before publishing numbers:

```bash
TGEOMETRY_BENCH_MIN_SECONDS=1.0 bundle exec ruby benchmark/distance_within_radius.rb
```

## No `:auto` strategy yet

The first release does not expose `strategy: :auto`. Choosing a threshold requires project-owned benchmark output across the required scenario matrix. Internal rtree constants such as leaf capacity are not a flat-vs-rtree crossover threshold.

## GVL threshold

`benchmark/gvl_threshold.rb` records baseline parse wall time for several valid WKT payload sizes while the first release keeps the GVL. It does not enable no-GVL execution. A future no-GVL implementation requires a separate design because Ruby C API calls and `RSTRING_PTR` lifetimes are not valid outside the GVL.

## RSS stability

`benchmark/rss_stability.rb` reports start, peak, finish, and delta RSS while repeatedly building, querying, and releasing indexes. CI thresholds should be chosen from observed baseline data on the target CI image, not guessed.

## Falcon / Async

The first release does not claim Falcon or Async behavior. A dedicated Falcon/Async benchmark remains an open setup item until the dependency and scenario are approved.

## Planned API areas: auto strategy threshold

`strategy: :auto` remains postponed for the first public release. A future implementation must use a complete project-owned benchmark matrix and document the selected threshold before exposing the public option.

## FeatureSource benchmark

`benchmark/feature_source.rb` compares:

- `JSON.parse + Ruby extraction` baseline;
- `FeatureSource.read_entries_file`;
- `FeatureSource.read_features_file`;
- direct `FeatureSource.build_index_file`;
- `Index.build(read_entries, via: :geojson)`.

Run:

```bash
bundle exec ruby benchmark/feature_source.rb
TGEOMETRY_BENCH_FULL=1 bundle exec ruby benchmark/feature_source.rb
```

Do not publish FeatureSource performance claims unless they come from this benchmark on the target dataset and environment.
