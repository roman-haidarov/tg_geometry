# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("flat_vs_rtree")
iterations = TGGeometryBench.iterations(1_000)

%i[compact long_thin overlapping].each do |kind|
  TGGeometryBench.sizes.each do |size|
    entries = TGGeometryBench.entries_for(kind, size)

    build_flat = Benchmark.realtime { @flat = TGGeometryBench.build_index(entries, strategy: :flat) }
    build_rtree = Benchmark.realtime { @rtree = TGGeometryBench.build_index(entries, strategy: :rtree) }

    TGGeometryBench.points_for(kind).each do |lon, lat|
      flat_time = Benchmark.realtime { iterations.times { @flat.find_covering(lon, lat) } }
      rtree_time = Benchmark.realtime { iterations.times { @rtree.find_covering(lon, lat) } }
      puts "kind=#{kind} n=#{size} query=point lon=#{lon} lat=#{lat} flat_sec=%.6f rtree_sec=%.6f flat_qps=%.2f rtree_qps=%.2f build_flat=%.6f build_rtree=%.6f" % [flat_time, rtree_time, iterations / flat_time, iterations / rtree_time, build_flat, build_rtree]
    end

    TGGeometryBench.rects_for(kind).each do |rect|
      flat_time = Benchmark.realtime { iterations.times { @flat.intersecting_rect(*rect) } }
      rtree_time = Benchmark.realtime { iterations.times { @rtree.intersecting_rect(*rect) } }
      puts "kind=#{kind} n=#{size} query=rect rect=#{rect.join(',')} flat_sec=%.6f rtree_sec=%.6f flat_qps=%.2f rtree_qps=%.2f" % [flat_time, rtree_time, iterations / flat_time, iterations / rtree_time]
    end
  end
end
