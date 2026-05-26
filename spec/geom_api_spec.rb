# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Geom API" do
  let(:polygon_wkt) { "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))" }
  let(:inner_point_wkt) { "POINT (5 5)" }
  let(:boundary_point_wkt) { "POINT (0 5)" }
  let(:outside_point_wkt) { "POINT (11 5)" }

  it "maps geometry types to Ruby symbols" do
    expect(TG::Geometry.parse_wkt("POINT (1 2)").type).to eq(:point)
    expect(TG::Geometry.parse_wkt(polygon_wkt).type).to eq(:polygon)
  end

  it "returns a frozen Rect bbox with coordinate accessors" do
    bbox = TG::Geometry.parse_wkt(polygon_wkt).bbox

    expect(bbox).to be_a(TG::Geometry::Rect)
    expect(bbox).to be_frozen
    expect([bbox.min_x, bbox.min_y, bbox.max_x, bbox.max_y]).to eq([0.0, 0.0, 10.0, 10.0])
  end

  it "checks covers_xy? for inside, outside, and boundary points" do
    geom = TG::Geometry.parse_wkt(polygon_wkt)

    expect(geom.covers_xy?(5, 5)).to be(true)
    expect(geom.covers_xy?(0, 5)).to be(true)
    expect(geom.covers_xy?(11, 5)).to be(false)
  end

  it "keeps contains? strict while covers_xy? includes boundary" do
    geom = TG::Geometry.parse_wkt(polygon_wkt)
    inner = TG::Geometry.parse_wkt(inner_point_wkt)
    boundary = TG::Geometry.parse_wkt(boundary_point_wkt)
    outside = TG::Geometry.parse_wkt(outside_point_wkt)

    expect(geom.contains?(inner)).to be(true)
    expect(geom.contains?(boundary)).to be(false)
    expect(geom.contains?(outside)).to be(false)
  end

  it "checks intersects? for basic geometry pairs" do
    geom = TG::Geometry.parse_wkt(polygon_wkt)
    overlapping = TG::Geometry.parse_wkt("POLYGON ((5 5, 12 5, 12 12, 5 12, 5 5))")
    disjoint = TG::Geometry.parse_wkt("POLYGON ((20 20, 30 20, 30 30, 20 30, 20 20))")

    expect(geom.intersects?(overlapping)).to be(true)
    expect(geom.intersects?(disjoint)).to be(false)
  end

  it "roundtrips through GeoJSON, WKT, and WKB writers" do
    geom = TG::Geometry.parse_wkt(polygon_wkt)

    geojson = geom.to_geojson
    wkt = geom.to_wkt
    wkb = geom.to_wkb

    expect(geojson.encoding).to eq(Encoding::UTF_8)
    expect(wkt.encoding).to eq(Encoding::UTF_8)
    expect(wkb.encoding).to eq(Encoding::BINARY)

    expect(TG::Geometry.parse_geojson(geojson).type).to eq(:polygon)
    expect(TG::Geometry.parse_wkt(wkt).bbox.max_x).to eq(10.0)
    expect(TG::Geometry.parse_wkb(wkb).covers_xy?(10, 10)).to be(true)
  end

  it "serializes WKB point output without losing binary bytes" do
    point = TG::Geometry.parse_wkt("POINT (1 2)")
    wkb = point.to_wkb

    expect(wkb.bytesize).to eq(21)
    expect(wkb.encoding).to eq(Encoding::BINARY)
    expect(TG::Geometry.parse_wkb(wkb).bbox.min_x).to eq(1.0)
  end

  it "raises TypeError for predicate arguments that are not Geom" do
    geom = TG::Geometry.parse_wkt(polygon_wkt)

    expect { geom.contains?("not a geom") }.to raise_error(TypeError)
    expect { geom.intersects?(Object.new) }.to raise_error(TypeError)
  end

  it "rejects non-finite covers_xy? coordinates" do
    geom = TG::Geometry.parse_wkt(polygon_wkt)

    expect { geom.covers_xy?(Float::NAN, 1.0) }.to raise_error(TG::Geometry::ArgumentError)
    expect { geom.covers_xy?(1.0, Float::INFINITY) }.to raise_error(TG::Geometry::ArgumentError)
  end
end
