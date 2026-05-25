# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("rss_stability")

total_queries = Integer(ENV.fetch("TGEOMETRY_RSS_QUERIES", "10_000_000"))
rebuilds = Integer(ENV.fetch("TGEOMETRY_RSS_REBUILDS", "100"))
entries_count = Integer(ENV.fetch("TGEOMETRY_RSS_ENTRIES", "1_000"))
max_drift_kb = Integer(ENV.fetch("TGEOMETRY_RSS_MAX_DRIFT_KB", "51_200"))

queries_per_rebuild = (total_queries / rebuilds).clamp(1, total_queries)
entries = TGGeometryBench.compact_entries(entries_count)
points = TGGeometryBench.points_for(:compact)
packed_batch = TGGeometryBench.packed_points(points * 10)

GC.start
GC.start
start_rss = TGGeometryBench.rss_kb
peak_rss = start_rss
samples = []

queries_executed = 0
started_at = Process.clock_gettime(Process::CLOCK_MONOTONIC)

rebuilds.times do |cycle|
  index = TGGeometryBench.build_index(entries, strategy: (cycle.even? ? :flat : :rtree))

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

  if (cycle % 10).zero?
    GC.start
    rss = TGGeometryBench.rss_kb
    peak_rss = [peak_rss, rss].max
    samples << [cycle, queries_executed, rss]
  end
end

GC.start
GC.start
finish_rss = TGGeometryBench.rss_kb
elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - started_at
drift_kb = finish_rss - start_rss

puts format(
  "queries=%d rebuilds=%d entries=%d elapsed_s=%.2f start_rss_kb=%d peak_rss_kb=%d finish_rss_kb=%d drift_kb=%d",
  queries_executed, rebuilds, entries_count, elapsed,
  start_rss, peak_rss, finish_rss, drift_kb
)

if samples.length > 1
  puts "samples (cycle, queries, rss_kb):"
  samples.each { |row| puts "  #{row.inspect}" }
end

if max_drift_kb.positive? && drift_kb > max_drift_kb
  warn "[rss_stability] RSS drift #{drift_kb} KB exceeds threshold #{max_drift_kb} KB"
  exit 1
end
