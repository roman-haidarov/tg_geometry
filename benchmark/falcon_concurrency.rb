# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("falcon_concurrency")
TGGeometryBench.report("falcon_concurrency_note", note: "thread_read_baseline_only_no_falcon_dependency")

entries_count = TGGeometryBench.env_integer("TGEOMETRY_BENCH_ENTRIES", 1_000, min: 1)
threads = TGGeometryBench.env_integer("TGEOMETRY_BENCH_THREADS", 4, min: 1)
entries = TGGeometryBench.compact_entries(entries_count)
index = TGGeometryBench.build_index(entries, strategy: :rtree)
points = TGGeometryBench.points_for(:compact)

stats = TGGeometryBench.measure_counted(
  initial_iterations: TGGeometryBench.initial_iterations(2_000),
  operations_per_iteration: threads
) do |iterations_per_thread|
  threads.times.map do |thread_index|
    Thread.new do
      iterations_per_thread.times do |i|
        lon, lat = points[(i + thread_index) % points.length]
        index.find_covering(lon, lat)
      end
    end
  end.each(&:join)
end

TGGeometryBench.report(
  "falcon_concurrency",
  { threads: threads, entries: entries_count, strategy: :rtree, operation: :find_covering },
  stats: stats
)
