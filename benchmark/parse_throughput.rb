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
  "wkt_small" => [small, :wkt, 5_000],
  "geojson_small" => [geojson, :geojson, 5_000],
  "wkb_small" => [wkb, :wkb, 5_000],
  "wkt_medium" => [medium, :wkt, 500],
  "wkt_large" => [large, :wkt, 50]
}

cases.each do |name, (payload, format, initial)|
  stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(initial)) do |iterations|
    iterations.times { TG::Geometry.parse(payload, format: format) }
  end

  TGGeometryBench.report(
    "parse_throughput",
    { case: name, format: format, payload_bytes: payload.bytesize },
    stats: stats
  )
end
