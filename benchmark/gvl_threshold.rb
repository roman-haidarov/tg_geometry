# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("gvl_threshold")
TGGeometryBench.report("gvl_threshold_note", note: "baseline_only_gvl_is_held_no_public_release_gvl_knob")

# Build one valid WKT polygon close to the requested byte size.
def polygon_wkt_at_least(target_bytes)
  points_count = [4, target_bytes / 38].max

  loop do
    points = Array.new(points_count) do |i|
      angle = (2.0 * Math::PI * i) / points_count
      [Math.cos(angle) * 10.0, Math.sin(angle) * 10.0]
    end
    points << points.first

    coordinates = points.map { |x, y| "#{x} #{y}" }.join(", ")
    payload = "POLYGON ((#{coordinates}))"
    return payload if payload.bytesize >= target_bytes

    points_count = (points_count * 1.25).ceil
  end
end

[128, 1_024, 16_384, 262_144].each do |target_bytes|
  payload = polygon_wkt_at_least(target_bytes)
  initial = target_bytes >= 262_144 ? 10 : 500

  stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(initial)) do |iterations|
    iterations.times { TG::Geometry.parse_wkt(payload) }
  end

  TGGeometryBench.report(
    "gvl_threshold_parse_wkt",
    { target_bytes: target_bytes, payload_bytes: payload.bytesize },
    stats: stats
  )
end
