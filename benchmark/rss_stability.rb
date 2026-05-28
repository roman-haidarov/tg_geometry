# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("rss_stability")

total_queries = TGGeometryBench.env_integer("TGEOMETRY_RSS_QUERIES", 10_000_000, min: 1)
rebuilds = TGGeometryBench.env_integer("TGEOMETRY_RSS_REBUILDS", 100, min: 1)
entries_count = TGGeometryBench.env_integer("TGEOMETRY_RSS_ENTRIES", 1_000, min: 1)
max_drift_kb = TGGeometryBench.env_integer("TGEOMETRY_RSS_MAX_DRIFT_KB", 51_200, min: 0)

queries_per_rebuild = (total_queries / rebuilds).clamp(1, total_queries)
entries = TGGeometryBench.compact_entries(entries_count)
points = TGGeometryBench.points_for(:compact)
packed_batch = TGGeometryBench.packed_points(points * 10)

TGGeometryBench.gc_start
TGGeometryBench.gc_start
start_rss = TGGeometryBench.rss_kb
peak_rss = start_rss
samples = []

queries_executed = 0
started_at = TGGeometryBench.monotonic

rebuilds.times do |cycle|
  strategy = cycle.even? ? :flat : :rtree
  index = TGGeometryBench.build_index(entries, strategy: strategy)

  queries_per_rebuild.times do |q|
    lon, lat = points[(q + cycle) % points.length]
    index.find_covering(lon, lat)
    queries_executed += 1

    if (q & 0xff).zero?
      index.covering_ids_batch_packed(packed_batch)
      queries_executed += points.length * 10
    end
  end

  index = nil

  next unless (cycle % 10).zero?

  TGGeometryBench.gc_start
  rss = TGGeometryBench.rss_kb
  peak_rss = [peak_rss, rss].max
  samples << [cycle, queries_executed, rss]
end

TGGeometryBench.gc_start
TGGeometryBench.gc_start
finish_rss = TGGeometryBench.rss_kb
elapsed = TGGeometryBench.monotonic - started_at
drift_kb = finish_rss - start_rss

TGGeometryBench.report(
  "rss_stability",
  {
    queries: queries_executed,
    rebuilds: rebuilds,
    entries: entries_count,
    elapsed_sec: elapsed,
    qps: queries_executed / elapsed,
    start_rss_kb: start_rss,
    peak_rss_kb: peak_rss,
    finish_rss_kb: finish_rss,
    drift_kb: drift_kb,
    max_drift_kb: max_drift_kb,
    sample_count: samples.length
  }
)

samples.each do |cycle, queries, rss|
  TGGeometryBench.report("rss_stability_sample", cycle: cycle, queries: queries, rss_kb: rss)
end

if max_drift_kb.positive? && drift_kb > max_drift_kb
  warn "[rss_stability] RSS drift #{drift_kb} KB exceeds threshold #{max_drift_kb} KB"
  exit 1
end
