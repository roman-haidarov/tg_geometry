# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("nearest_segment")

[100, 1_000, 10_000].each do |count|
  points = count.times.map do |i|
    angle = 2.0 * Math::PI * i / count
    [Math.cos(angle), Math.sin(angle)]
  end
  points << points.first
  ring = TG::Geometry.polygon(points).polygon.exterior_ring

  stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(10_000)) do |iterations|
    iterations.times { ring.nearest_segment(0.25, 0.33) }
  end

  TGGeometryBench.report(
    "nearest_segment",
    { segments: ring.num_segments, query: "0.25:0.33" },
    stats: stats
  )
end
