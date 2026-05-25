# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("parse_throughput")

def polygon_with_points(count)
  points = Array.new(count) do |i|
    angle = (2.0 * Math::PI * i) / count
    [Math.cos(angle) * 10.0, Math.sin(angle) * 10.0]
  end
  points << points.first
  coordinates = points.map { |x, y| "#{x} #{y}" }.join(", ")
  "POLYGON ((#{coordinates}))"
end

small = TGGeometryBench.box_wkt(0, 0, 10, 10)
medium = polygon_with_points(250)
large = polygon_with_points(2_500)
geojson = '{"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]]]}'
wkb = TG::Geometry.parse_wkt(small).to_wkb

cases = {
  "wkt:small" => [small, :wkt],
  "geojson:small" => [geojson, :geojson],
  "wkb:small" => [wkb, :wkb],
  "wkt:medium" => [medium, :wkt],
  "wkt:large" => [large, :wkt]
}

iterations = TGGeometryBench.iterations(10_000)

cases.each do |name, (payload, format)|
  time = Benchmark.realtime do
    iterations.times { TG::Geometry.parse(payload, format: format) }
  end
  puts "%s iterations=%d seconds=%.6f ops_per_sec=%.2f" % [name, iterations, time, iterations / time]
end
