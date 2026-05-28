# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("batch_packed_vs_loop")
points_per_batch = TGGeometryBench.env_integer("TGEOMETRY_BENCH_BATCH_POINTS", 1_000, min: 1)

%i[compact long_thin overlapping].each do |kind|
  TGGeometryBench.sizes.each do |size|
    entries = TGGeometryBench.entries_for(kind, size)
    points = TGGeometryBench.repeated_points(kind, points_per_batch)
    packed = TGGeometryBench.packed_points(points)

    %i[flat rtree].each do |strategy|
      index = TGGeometryBench.build_index(entries, strategy: strategy)

      scalar = TGGeometryBench.measure_counted(
        initial_iterations: TGGeometryBench.initial_iterations(50),
        operations_per_iteration: points.length
      ) do |iterations|
        iterations.times do
          points.each { |lon, lat| index.find_covering(lon, lat) }
        end
      end

      batch = TGGeometryBench.measure_counted(
        initial_iterations: TGGeometryBench.initial_iterations(50),
        operations_per_iteration: points.length
      ) do |iterations|
        iterations.times { index.covering_ids_batch_packed(packed) }
      end

      TGGeometryBench.report(
        "batch_packed_vs_loop",
        {
          kind: kind,
          n: size,
          strategy: strategy,
          mode: :scalar_loop,
          points_per_batch: points.length,
          batches_per_sec: scalar[:ops_per_sec] / points.length
        },
        stats: scalar
      )
      TGGeometryBench.report(
        "batch_packed_vs_loop",
        {
          kind: kind,
          n: size,
          strategy: strategy,
          mode: :packed_batch,
          points_per_batch: points.length,
          batches_per_sec: batch[:ops_per_sec] / points.length
        },
        stats: batch
      )
    end
  end
end
