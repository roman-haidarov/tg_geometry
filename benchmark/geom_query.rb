# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("geom_query")

COUNTS = (TGGeometryBench.full? ? [1_000, 10_000, 50_000] : [1_000, 10_000]).freeze
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

  queries = {
    small: box(10.5, 10.5, 2.0),
    large: box(0.5, 0.5, 120.0),
    miss: box(-1_000.0, -1_000.0, 1.0)
  }

  STRATEGIES.each do |strategy|
    index = TG::Geometry::Index.build(entries, via: :geom, strategy: strategy)

    queries.each do |query_name, query_geom|
      initial = query_name == :large ? 100 : 1_000
      stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(initial)) do |iterations|
        iterations.times { index.intersecting_geom_ids(query_geom) }
      end

      TGGeometryBench.report(
        "geom_query",
        { n: count, strategy: strategy, query: query_name, method: :intersecting_geom_ids },
        stats: stats
      )
    end
  end
end
