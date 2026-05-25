# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Release Core Block 13 error hardening" do
  let(:geojson) { '{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}' }
  let(:wkb) { TG::Geometry.parse_geojson(geojson).to_wkb }

  it "raises ParseError for malformed GeoJSON at first, middle, and last positions" do
    bad = "not geojson"

    [
      [[1, bad], [2, geojson], [3, geojson]],
      [[1, geojson], [2, bad], [3, geojson]],
      [[1, geojson], [2, geojson], [3, bad]]
    ].each do |entries|
      expect { TG::Geometry::Index.build(entries, via: :geojson, strategy: :flat) }
        .to raise_error(TG::Geometry::ParseError)
    end
  end

  it "raises ParseError for malformed WKB at first, middle, and last positions" do
    bad = "not wkb".b

    [
      [[1, bad], [2, wkb], [3, wkb]],
      [[1, wkb], [2, bad], [3, wkb]],
      [[1, wkb], [2, wkb], [3, bad]]
    ].each do |entries|
      expect { TG::Geometry::Index.build(entries, via: :wkb, strategy: :flat) }
        .to raise_error(TG::Geometry::ParseError)
    end
  end

  it "supports debug OOM simulation for entries allocation" do
    skip "TG_DEBUG_TEST hooks are not enabled" unless TG::Geometry.respond_to?(:_debug_fail_next_entries_alloc!)

    geom = TG::Geometry.parse_geojson(geojson)
    TG::Geometry._debug_reset_test_hooks!
    TG::Geometry._debug_fail_next_entries_alloc!

    expect do
      TG::Geometry::Index.build([[1, geom]], via: :geom, strategy: :flat)
    end.to raise_error(NoMemoryError)
  ensure
    TG::Geometry._debug_reset_test_hooks! if TG::Geometry.respond_to?(:_debug_reset_test_hooks!)
  end

  it "keeps dispose idempotent and clears byte counters in debug builds" do
    skip "TG_DEBUG_TEST hooks are not enabled" unless TG::Geometry::Index.method_defined?(:_force_dispose_for_test!)

    index = TG::Geometry::Index.build([[1, geojson]], via: :geojson, strategy: :rtree)

    expect(index._entries_bytes_for_test).to be > 0
    expect(index._owned_geom_bytes_for_test).to be > 0
    expect(index._rtree_bytes_for_test).to be > 0

    index._force_dispose_for_test!
    index._force_dispose_for_test!

    expect(index._entries_bytes_for_test).to eq(0)
    expect(index._owned_geom_bytes_for_test).to eq(0)
    expect(index._rtree_bytes_for_test).to eq(0)
  end
end
