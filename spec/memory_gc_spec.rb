# frozen_string_literal: true

require "spec_helper"
require "objspace"

RSpec.describe "Memory, GC, and compaction hardening" do
  let(:small_polygon_wkt) { "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))" }
  let(:small_polygon_geojson) { '{"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]]]}' }
  let(:malformed_geojson) { '{"type":"Polygon","coordinates":[' }

  def compact_if_supported
    GC.start
    GC.compact if GC.respond_to?(:compact)
  end

  it "survives a parse/free loop under GC.stress" do
    old_stress = GC.stress
    GC.stress = true

    50.times do
      geom = TG::Geometry.parse_wkt(small_polygon_wkt)
      expect(geom).to be_frozen
      expect(geom.covers_xy?(5.0, 5.0)).to be(true)
    end
  ensure
    GC.stress = old_stress
  end

  it "survives index build/free loops for owned and borrowed geometry" do
    geom = TG::Geometry.parse_wkt(small_polygon_wkt)

    25.times do
      owned = TG::Geometry::Index.build([["owned", small_polygon_geojson]], via: :geojson, strategy: :rtree)
      borrowed = TG::Geometry::Index.build([["borrowed", geom]], via: :geom, strategy: :rtree)

      expect(owned.find_covering(5, 5)).to eq("owned")
      expect(borrowed.find_covering(5, 5)).to eq("borrowed")
    end

    compact_if_supported
  end

  it "survives failed build loops without leaving initialized native state observable" do
    20.times do
      expect do
        TG::Geometry::Index.build([[1, small_polygon_geojson], [2, malformed_geojson]], via: :geojson, strategy: :rtree)
      end.to raise_error(TG::Geometry::ParseError)
    end

    compact_if_supported
  end

  it "survives repeated query loops for flat and rtree strategies" do
    entries = [
      [:a, TG::Geometry.parse_wkt(small_polygon_wkt)],
      [:b, TG::Geometry.parse_wkt("POLYGON ((20 20, 30 20, 30 30, 20 30, 20 20))")]
    ]

    %i[flat rtree].each do |strategy|
      index = TG::Geometry::Index.build(entries, via: :geom, strategy: strategy)

      250.times do
        expect(index.find_covering(5, 5)).to eq(:a)
        expect(index.covering_ids(25, 25)).to eq([:b])
        expect(index.intersecting_rect(0, 0, 1, 1)).to eq([:a])
      end
    end
  end

  it "keeps via: :geom borrowed owners alive through GC.compact" do
    geom = TG::Geometry.parse_wkt(small_polygon_wkt)
    index = TG::Geometry::Index.build([["zone", geom]], via: :geom, strategy: :rtree)

    geom = nil
    compact_if_supported

    expect(index.find_covering(5, 5)).to eq("zone")
    expect(index.covering_ids(0, 5)).to eq(["zone"])
  end

  it "keeps via: :geojson owned geometries usable through GC.compact" do
    index = TG::Geometry::Index.build([["zone", small_polygon_geojson]], via: :geojson, strategy: :rtree)

    compact_if_supported

    expect(index.find_covering(5, 5)).to eq("zone")
    expect(index.covering_ids(10, 10)).to eq(["zone"])
  end

  it "reports native memory through ObjectSpace.memsize_of for geom and index" do
    geom = TG::Geometry.parse_wkt(small_polygon_wkt)
    borrowed = TG::Geometry::Index.build([["zone", geom]], via: :geom, strategy: :flat)
    owned = TG::Geometry::Index.build([["zone", small_polygon_geojson]], via: :geojson, strategy: :rtree)

    expect(ObjectSpace.memsize_of(geom)).to be > ObjectSpace.memsize_of(Object.new)
    expect(ObjectSpace.memsize_of(borrowed)).to be > ObjectSpace.memsize_of(Object.new)
    expect(ObjectSpace.memsize_of(owned)).to be > ObjectSpace.memsize_of(borrowed)
  end

  it "returns rtree, owned geometry, and entries bytes to zero after dispose in debug builds" do
    skip "TG_DEBUG_TEST hooks are not enabled" unless TG::Geometry::Index.method_defined?(:_force_dispose_for_test!)

    index = TG::Geometry::Index.build([["zone", small_polygon_geojson]], via: :geojson, strategy: :rtree)

    expect(index._entries_bytes_for_test).to be > 0
    expect(index._owned_geom_bytes_for_test).to be > 0
    expect(index._rtree_bytes_for_test).to be > 0

    index._force_dispose_for_test!

    expect(index._entries_bytes_for_test).to eq(0)
    expect(index._owned_geom_bytes_for_test).to eq(0)
    expect(index._rtree_bytes_for_test).to eq(0)
    expect(index._initialized_entries_for_test).to eq(0)
  end
end
