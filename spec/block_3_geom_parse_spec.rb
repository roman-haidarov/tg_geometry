# frozen_string_literal: true

require "spec_helper"
require "objspace"

RSpec.describe "Release Core Block 3 Geom parsing" do
  let(:geojson_point) { %({"type":"Point","coordinates":[1.0,2.0]}) }
  let(:wkt_point) { "POINT (1 2)" }
  let(:wkb_point) { [1, 1].pack("CL<") + [1.0, 2.0].pack("E2") }
  let(:wkb_point_hex) { wkb_point.unpack1("H*") }

  it "parses GeoJSON into an immutable Geom" do
    geom = TG::Geometry.parse_geojson(geojson_point)

    expect(geom).to be_a(TG::Geometry::Geom)
    expect(geom).to be_frozen
  end

  it "parses WKT into an immutable Geom" do
    geom = TG::Geometry.parse_wkt(wkt_point)

    expect(geom).to be_a(TG::Geometry::Geom)
    expect(geom).to be_frozen
  end

  it "parses WKB into an immutable Geom" do
    geom = TG::Geometry.parse_wkb(wkb_point)

    expect(geom).to be_a(TG::Geometry::Geom)
    expect(geom).to be_frozen
  end

  it "supports parse(format:) mapping for auto, GeoJSON, WKT, WKB, hex, and GeoBIN" do
    expect(TG::Geometry.parse(geojson_point, format: :auto)).to be_a(TG::Geometry::Geom)
    expect(TG::Geometry.parse(geojson_point, format: :geojson)).to be_a(TG::Geometry::Geom)
    expect(TG::Geometry.parse(wkt_point, format: :wkt)).to be_a(TG::Geometry::Geom)
    expect(TG::Geometry.parse(wkb_point, format: :wkb)).to be_a(TG::Geometry::Geom)
    expect(TG::Geometry.parse(wkb_point_hex, format: :hex)).to be_a(TG::Geometry::Geom)
    expect { TG::Geometry.parse("invalid geobin".b, format: :geobin) }
      .to raise_error(TG::Geometry::ParseError)
  end

  it "supports index: symbol mapping" do
    %i[default none natural ystripes].each do |index|
      expect(TG::Geometry.parse_geojson(geojson_point, index: index)).to be_a(TG::Geometry::Geom)
    end
  end

  it "raises TG::Geometry::ParseError for invalid GeoJSON" do
    expect { TG::Geometry.parse_geojson("not json") }.to raise_error(TG::Geometry::ParseError)
  end

  it "raises TG::Geometry::ArgumentError for invalid format symbols" do
    expect { TG::Geometry.parse(geojson_point, format: :shape) }
      .to raise_error(TG::Geometry::ArgumentError)
  end

  it "raises TG::Geometry::ArgumentError for invalid index symbols" do
    expect { TG::Geometry.parse_geojson(geojson_point, index: :packed) }
      .to raise_error(TG::Geometry::ArgumentError)
  end

  it "reports native memory through ObjectSpace.memsize_of" do
    geom = TG::Geometry.parse_geojson(geojson_point)

    expect(ObjectSpace.memsize_of(geom)).to be > ObjectSpace.memsize_of(Object.new)
  end

  it "survives GC.stress parse/free lifecycle" do
    old_stress = GC.stress
    GC.stress = true

    25.times do
      expect(TG::Geometry.parse_wkt(wkt_point)).to be_frozen
    end
  ensure
    GC.stress = old_stress
  end

  it "survives GC.compact after parse when supported" do
    geom = TG::Geometry.parse_geojson(geojson_point)

    GC.start
    GC.compact if GC.respond_to?(:compact)

    expect(geom).to be_a(TG::Geometry::Geom)
    expect(geom).to be_frozen
  end
end
