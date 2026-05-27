# frozen_string_literal: true

require "benchmark"
require_relative "_support"

geom = TG::Geometry.polygon([[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], srid: 4326)
ewkb = geom.to_ewkb

Benchmark.bm(32) do |x|
  x.report("tg parse_wkb -> to_ewkb 100k") do
    100_000.times { TG::Geometry.parse_wkb(ewkb).to_ewkb }
  end
end

if ENV["WITH_RGEO"]
  begin
    require "rgeo"
    factory = RGeo::Cartesian.factory(srid: 4326)
    rgeo_geom = factory.parse_wkt("POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))")

    Benchmark.bm(32) do |x|
      x.report("rgeo WKT parse 100k") do
        100_000.times { factory.parse_wkt(rgeo_geom.as_text) }
      end
    end
  rescue LoadError
    warn "rgeo is not installed"
  end
end
