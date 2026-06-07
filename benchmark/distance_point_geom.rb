# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("distance_point_geom")

VERTEX_COUNTS = (TGGeometryBench.full? ? [16, 128, 1_024, 8_192] : [16, 128, 1_024]).freeze

def regular_polygon(vertex_count, radius: 1.0)
  points = vertex_count.times.map do |i|
    angle = (2.0 * Math::PI * i) / vertex_count
    [Math.cos(angle) * radius, Math.sin(angle) * radius]
  end
  points << points.first
  TG::Geometry.polygon(points)
end

def zigzag_line(vertex_count, scale: 1.0)
  points = vertex_count.times.map { |i| [i.to_f * scale, (i.even? ? 0.0 : 1.0) * scale] }
  TG::Geometry.line_string(points)
end

VERTEX_COUNTS.each do |vertices|
  geometries = {
    polygon_xy: regular_polygon(vertices, radius: 10.0),
    line_xy: zigzag_line(vertices),
    polygon_lnglat_meters: regular_polygon(vertices, radius: 0.01),
    line_lnglat_meters: zigzag_line(vertices, scale: 0.0001)
  }

  geometries.each do |kind, geom|
    method, args = case kind
                   when :polygon_xy then [:distance_to_xy, [20.0, 0.0]]
                   when :line_xy then [:distance_to_xy, [vertices / 2.0, 5.0]]
                   when :polygon_lnglat_meters then [:distance_to_lnglat_meters, [0.02, 0.0]]
                   when :line_lnglat_meters then [:distance_to_lnglat_meters, [vertices / 20_000.0, 0.002]]
                   end

    stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(1_000)) do |iter|
      iter.times { geom.public_send(method, *args) }
    end

    TGGeometryBench.report(
      "distance_point_geom",
      { kind: kind, n: vertices, method: method, note: "distance methods keep the GVL" },
      stats: stats
    )
  end
end
