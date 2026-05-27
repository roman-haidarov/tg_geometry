# frozen_string_literal: true

require "json"
require "spec_helper"

RSpec.describe "PostGIS EWKB fixtures" do
  FIXTURE_ROOT = File.expand_path("fixtures/postgis", __dir__)

  CASES = {
    "point_4326.ewkb" => { srid: 4326, bbox: [37.6, 55.7, 37.6, 55.7] },
    "polygon_4326_simple.ewkb" => { srid: 4326, bbox: [0.0, 0.0, 10.0, 10.0] },
    "polygon_4326_with_hole.ewkb" => { srid: 4326, bbox: [0.0, 0.0, 10.0, 10.0] },
    "polygon_3857.ewkb" => { srid: 3857, bbox: [0.0, 0.0, 10.0, 10.0] },
    "multipolygon_large.ewkb" => { srid: 4326, bbox: [99.0, 49.0, 101.0, 51.0] }
  }.freeze

  CASES.each do |filename, expected|
    it "parses and roundtrips #{filename}" do
      bytes = File.binread(File.join(FIXTURE_ROOT, filename))
      geom = TG::Geometry.parse_wkb(bytes)
      bbox = geom.bbox

      expect(geom.srid).to eq(expected[:srid])
      expect([bbox.min_x, bbox.min_y, bbox.max_x, bbox.max_y]).to all(be_a(Float))
      [bbox.min_x, bbox.min_y, bbox.max_x, bbox.max_y].zip(expected[:bbox]).each do |actual, expected_coord|
        expect(actual).to be_within(1e-9).of(expected_coord)
      end
      expect(geom.to_ewkb).to eq(bytes)
      expect(TG::Geometry.parse_wkb(geom.to_wkb).srid).to be_nil
    end
  end

  it "keeps hole coverage semantics stable" do
    geom = TG::Geometry.parse_wkb(File.binread(File.join(FIXTURE_ROOT, "polygon_4326_with_hole.ewkb")))

    expect(geom.covers_xy?(3, 3)).to be(false)
    expect(geom.covers_xy?(1, 1)).to be(true)
  end

  it "checks boundary cases from GeoJSON" do
    simple = TG::Geometry.parse_wkb(File.binread(File.join(FIXTURE_ROOT, "polygon_4326_simple.ewkb")))
    data = JSON.parse(File.read(File.join(FIXTURE_ROOT, "boundary_point_cases.geojson")))
    points = data.fetch("features").to_h do |feature|
      [feature.fetch("properties").fetch("name"), feature.fetch("geometry").fetch("coordinates")]
    end

    expect(simple.covers_xy?(*points.fetch("inside_simple"))).to be(true)
    expect(simple.covers_xy?(*points.fetch("boundary_simple"))).to be(true)
  end

  it "sanity-checks the large multipolygon quickly" do
    started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    geom = TG::Geometry.parse_wkb(File.binread(File.join(FIXTURE_ROOT, "multipolygon_large.ewkb")))
    elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - started

    expect(elapsed).to be < 1.0
    expect(geom.covers_xy?(100.0, 50.0)).to be(true)
    expect(geom.covers_xy?(200.0, 50.0)).to be(false)
  end

  it "documents SRID mismatch as user responsibility" do
    geom_4326 = TG::Geometry.parse_wkb(File.binread(File.join(FIXTURE_ROOT, "polygon_4326_simple.ewkb")))
    geom_3857 = TG::Geometry.parse_wkb(File.binread(File.join(FIXTURE_ROOT, "polygon_3857.ewkb")))

    expect(geom_4326.srid).not_to eq(geom_3857.srid)
    expect(geom_4326.intersects?(geom_3857)).to be(true)
  end
end
