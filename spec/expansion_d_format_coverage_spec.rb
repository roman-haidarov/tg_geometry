# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Expansion Block D format coverage" do
  it "roundtrips Hex" do
    geom = TG::Geometry.parse_wkt("POINT (1 2)")
    hex = geom.to_hex

    expect(hex.encoding).to eq(Encoding::UTF_8)
    expect(TG::Geometry.parse_hex(hex).to_wkt).to eq("POINT(1 2)")
  end

  it "roundtrips GeoBIN" do
    geom = TG::Geometry.parse_wkt("POINT (1 2)")
    geobin = geom.to_geobin

    expect(geobin.encoding).to eq(Encoding::ASCII_8BIT)
    expect(TG::Geometry.parse_geobin(geobin).to_wkt).to eq("POINT(1 2)")
  end

  it "returns raw extra_json without JSON parsing" do
    geom = TG::Geometry.parse_geojson(
      '{"type":"Feature","properties":{"name":"a"},"geometry":{"type":"Point","coordinates":[1,2]}}'
    )

    expect(geom.extra_json).to eq('{"properties":{"name":"a"}}')
    expect(geom.extra_json).to be_a(String)
  end
end
