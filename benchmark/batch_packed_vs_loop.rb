# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("batch_packed_vs_loop")
iterations = TGGeometryBench.iterations(500)

%i[compact long_thin overlapping].each do |kind|
  TGGeometryBench.sizes.each do |size|
    entries = TGGeometryBench.entries_for(kind, size)
    points = Array.new(1_000) { |i| TGGeometryBench.points_for(kind)[i % TGGeometryBench.points_for(kind).length] }
    packed = TGGeometryBench.packed_points(points)

    %i[flat rtree].each do |strategy|
      index = TGGeometryBench.build_index(entries, strategy: strategy)

      scalar_time = Benchmark.realtime do
        iterations.times { points.map { |lon, lat| index.find_covering(lon, lat) } }
      end
      batch_time = Benchmark.realtime do
        iterations.times { index.covering_ids_batch_packed(packed) }
      end

      puts "kind=#{kind} n=#{size} strategy=#{strategy} points=#{points.length} scalar_sec=%.6f batch_sec=%.6f scalar_batches_per_sec=%.2f batch_batches_per_sec=%.2f" % [scalar_time, batch_time, iterations / scalar_time, iterations / batch_time]
    end
  end
end
