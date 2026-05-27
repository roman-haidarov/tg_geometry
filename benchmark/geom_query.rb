# frozen_string_literal: true

require "benchmark"
require_relative "_support"

COUNTS = [1_000, 10_000].freeze
STRATEGIES = %i[flat rtree].freeze


def box(x, y, size = 1.0)
  TG::Geometry.polygon([[x, y], [x + size, y], [x + size, y + size], [x, y + size], [x, y]])
end

COUNTS.each do |count|
  entries = count.times.map do |i|
    x = (i % 100).to_f * 2.0
    y = (i / 100).to_f * 2.0
    [i, box(x, y)]
  end

  small_query = box(10.5, 10.5, 2.0)
  large_query = box(0.5, 0.5, 120.0)

  puts "\n#{count} polygons"
  STRATEGIES.each do |strategy|
    index = TG::Geometry::Index.build(entries, via: :geom, strategy: strategy)

    Benchmark.bm(32) do |x|
      x.report("#{strategy} small intersecting_geom_ids") { 10_000.times { index.intersecting_geom_ids(small_query) } }
      x.report("#{strategy} large intersecting_geom_ids") { 1_000.times { index.intersecting_geom_ids(large_query) } }
    end
  end
end
