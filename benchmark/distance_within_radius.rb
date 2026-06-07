# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("distance_within_radius")

COUNTS = (TGGeometryBench.full? ? [1_000, 10_000, 50_000] : [1_000, 10_000]).freeze
# Contract-required honesty case: tiny index + radius covering the whole data extent.
# This shows the crossover zone where paying for the rtree prefilter may not help
# compared to a direct full scan. The result is empirical: on some Ruby/CPU builds
# rtree may still win, but the benchmark must expose the scenario instead of
# only showing selective-radius wins.
TINY_COUNTS = (TGGeometryBench.full? ? [50, 100, 250] : [50, 100]).freeze

def box(x, y, size = 0.8)
  TG::Geometry.polygon([[x, y], [x + size, y], [x + size, y + size], [x, y + size], [x, y]])
end

def entries(count)
  count.times.map do |i|
    x = (i % 250).to_f * 2.0
    y = (i / 250).to_f * 2.0
    [i, box(x, y)]
  end
end

def brute_force(entries, x, y, radius)
  entries.filter_map do |id, geom|
    distance = geom.distance_to_xy(x, y)
    [id, distance] if distance <= radius
  end
end

def measure_radius_case(data, index, count, query_name, x, y, radius, initial:)
  [
    [:rtree_prefilter_exact_filter, -> { index.within_distance_xy(x, y, radius) }],
    [:brute_force_full_scan, -> { brute_force(data, x, y, radius) }]
  ].each do |method, callable|
    stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(initial)) do |iterations|
      iterations.times { callable.call }
    end

    TGGeometryBench.report(
      "distance_within_radius",
      { n: count, query: query_name, method: method, note: "prefilter vs full scan; distance radius search keeps the GVL" },
      stats: stats
    )
  end
end

COUNTS.each do |count|
  data = entries(count)
  index = TG::Geometry::Index.build(data, via: :geom, strategy: :rtree)

  {
    selective: [10.5, 10.5, 1.0, 250],
    broad: [150.0, 20.0, 180.0, 25]
  }.each do |query_name, (x, y, radius, initial)|
    measure_radius_case(data, index, count, query_name, x, y, radius, initial: initial)
  end
end

TINY_COUNTS.each do |count|
  data = entries(count)
  index = TG::Geometry::Index.build(data, via: :geom, strategy: :rtree)

  # Query at the tiny grid center with a radius that covers the whole generated extent.
  max_x = ((count - 1) % 250).to_f * 2.0 + 0.8
  max_y = ((count - 1) / 250).to_f * 2.0 + 0.8
  x = max_x / 2.0
  y = max_y / 2.0
  radius = Math.hypot(max_x, max_y) + 1.0

  measure_radius_case(data, index, count, :tiny_full_extent, x, y, radius, initial: 100)
end
