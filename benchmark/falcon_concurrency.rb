# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("falcon_concurrency")
puts "No Falcon dependency is used here. This is a thread-read baseline for the immutable Index model."
puts "Falcon/Async behavior remains an OPEN QUESTION until Roman approves a dedicated dependency/setup."

entries = TGGeometryBench.compact_entries(1_000)
index = TGGeometryBench.build_index(entries, strategy: :rtree)
threads = Integer(ENV.fetch("TGEOMETRY_BENCH_THREADS", "4"))
iterations = TGGeometryBench.iterations(10_000)

elapsed = Benchmark.realtime do
  threads.times.map do
    Thread.new do
      iterations.times do |i|
        lon, lat = TGGeometryBench.points_for(:compact)[i % 3]
        index.find_covering(lon, lat)
      end
    end
  end.each(&:join)
end

puts "threads=#{threads} iterations_per_thread=#{iterations} total_queries=#{threads * iterations} seconds=%.6f qps=%.2f" % [elapsed, (threads * iterations) / elapsed]
