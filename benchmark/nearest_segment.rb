# frozen_string_literal: true

require "benchmark"
require_relative "_support"

[100, 1_000, 10_000].each do |count|
  points = count.times.map do |i|
    angle = 2.0 * Math::PI * i / count
    [Math.cos(angle), Math.sin(angle)]
  end
  points << points.first
  ring = TG::Geometry.polygon(points).polygon.exterior_ring

  puts "\nring segments: #{ring.num_segments}"
  Benchmark.bm(28) do |x|
    x.report("1M nearest_segment calls") do
      1_000_000.times { ring.nearest_segment(0.25, 0.33) }
    end
  end
end
