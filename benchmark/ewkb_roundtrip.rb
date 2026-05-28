# frozen_string_literal: true

require_relative "_support"

TGGeometryBench.say_header("ewkb_roundtrip")

geom = TG::Geometry.polygon([[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], srid: 4326)
ewkb = geom.to_ewkb

stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(5_000)) do |iterations|
  iterations.times { TG::Geometry.parse_wkb(ewkb).to_ewkb }
end

TGGeometryBench.report(
  "ewkb_roundtrip",
  { library: :tg_geometry, operation: :parse_wkb_to_ewkb, payload_bytes: ewkb.bytesize },
  stats: stats
)

if ENV["WITH_RGEO"]
  begin
    require "rgeo"
    factory = RGeo::Cartesian.factory(srid: 4326)
    rgeo_geom = factory.parse_wkt("POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))")

    rgeo_stats = TGGeometryBench.measure_counted(initial_iterations: TGGeometryBench.initial_iterations(5_000)) do |iterations|
      iterations.times { factory.parse_wkt(rgeo_geom.as_text) }
    end

    TGGeometryBench.report(
      "ewkb_roundtrip",
      { library: :rgeo, operation: :wkt_parse, payload_bytes: rgeo_geom.as_text.bytesize },
      stats: rgeo_stats
    )
  rescue LoadError
    warn "rgeo is not installed"
  end
end
