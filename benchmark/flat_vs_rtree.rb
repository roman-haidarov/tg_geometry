# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("flat_vs_rtree")

%i[compact long_thin overlapping].each do |kind|
  TGGeometryBench.sizes.each do |size|
    entries = TGGeometryBench.entries_for(kind, size)

    flat = nil
    rtree = nil

    flat_build = TGGeometryBench.measure_counted(initial_iterations: 1, min_seconds: 0.25) do |iterations|
      iterations.times { flat = TGGeometryBench.build_index(entries, strategy: :flat) }
    end
    rtree_build = TGGeometryBench.measure_counted(initial_iterations: 1, min_seconds: 0.25) do |iterations|
      iterations.times { rtree = TGGeometryBench.build_index(entries, strategy: :rtree) }
    end

    # Rebuild once outside the build benchmark so query numbers do not depend on
    # the last object produced by calibration/repeats.
    flat = TGGeometryBench.build_index(entries, strategy: :flat)
    rtree = TGGeometryBench.build_index(entries, strategy: :rtree)

    TGGeometryBench.report("flat_vs_rtree_build", { kind: kind, n: size, strategy: :flat }, stats: flat_build)
    TGGeometryBench.report("flat_vs_rtree_build", { kind: kind, n: size, strategy: :rtree }, stats: rtree_build)

    TGGeometryBench.points_for(kind).each_with_index do |(lon, lat), point_index|
      flat_stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(2_000)) do |iterations|
        iterations.times { flat.find_covering(lon, lat) }
      end
      rtree_stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(2_000)) do |iterations|
        iterations.times { rtree.find_covering(lon, lat) }
      end

      TGGeometryBench.report(
        "flat_vs_rtree_point",
        { kind: kind, n: size, strategy: :flat, point_index: point_index, lon: lon, lat: lat },
        stats: flat_stats
      )
      TGGeometryBench.report(
        "flat_vs_rtree_point",
        { kind: kind, n: size, strategy: :rtree, point_index: point_index, lon: lon, lat: lat },
        stats: rtree_stats
      )
    end

    TGGeometryBench.rects_for(kind).each_with_index do |rect, rect_index|
      flat_stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(200)) do |iterations|
        iterations.times { flat.intersecting_rect(*rect) }
      end
      rtree_stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(200)) do |iterations|
        iterations.times { rtree.intersecting_rect(*rect) }
      end

      TGGeometryBench.report(
        "flat_vs_rtree_rect",
        { kind: kind, n: size, strategy: :flat, rect_index: rect_index, rect: rect },
        stats: flat_stats
      )
      TGGeometryBench.report(
        "flat_vs_rtree_rect",
        { kind: kind, n: size, strategy: :rtree, rect_index: rect_index, rect: rect },
        stats: rtree_stats
      )
    end
  end
end
