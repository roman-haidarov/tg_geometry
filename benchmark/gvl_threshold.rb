# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("gvl_threshold")
puts "First release intentionally performs parse/write/batch/query with GVL held."
puts "This harness records baseline parse wall time only; it does not enable no-GVL execution."

# Build one valid WKT polygon close to the requested byte size.  The previous
# implementation accidentally benchmarked the same tiny 39-byte polygon for all
# target sizes because it used Array(...).first.
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

sizes = [128, 1_024, 16_384, 262_144]
iterations = TGGeometryBench.iterations(2_000)

sizes.each do |target_bytes|
  payload = polygon_wkt_at_least(target_bytes)

  time = Benchmark.realtime do
    iterations.times { TG::Geometry.parse_wkt(payload) }
  end

  puts "target_bytes=#{target_bytes} payload_bytes=#{payload.bytesize} iterations=#{iterations} seconds=%.6f ops_per_sec=%.2f" % [time, iterations / time]
end
