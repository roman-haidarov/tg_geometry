# frozen_string_literal: true

require "spec_helper"
require "json"
require "objspace"
require "weakref"

RSpec.describe "Index borrowed geometry ingestion" do
  let(:polygon_a_wkt) { "POLYGON ((0 0, 2 0, 2 2, 0 2, 0 0))" }
  let(:polygon_b_wkt) { "POLYGON ((10 10, 12 10, 12 12, 10 12, 10 10))" }

  it "builds an immutable index via: :geom using borrowed TG::Geometry::Geom objects" do
    geom_a = TG::Geometry.parse_wkt(polygon_a_wkt)
    geom_b = TG::Geometry.parse_wkt(polygon_b_wkt)

    index = TG::Geometry::Index.build([[:a, geom_a], [:b, geom_b]],
                                      via: :geom,
                                      strategy: :flat)

    expect(index).to be_a(TG::Geometry::Index)
    expect(index).to be_frozen
    expect(index.size).to eq(2)
    expect(index.strategy).to eq(:flat)
    expect(index.predicate).to eq(:covers)

    bbox = index.bbox
    expect(bbox.min_x).to eq(0.0)
    expect(bbox.min_y).to eq(0.0)
    expect(bbox.max_x).to eq(12.0)
    expect(bbox.max_y).to eq(12.0)
  end

  it "keeps borrowed geometry owners alive after caller drops local references" do
    geom = TG::Geometry.parse_wkt(polygon_a_wkt)
    weak_geom = WeakRef.new(geom)

    index = TG::Geometry::Index.build([[:zone, geom]], via: :geom, strategy: :flat)
    geom = nil

    GC.start(full_mark: true, immediate_sweep: true)
    GC.compact if GC.respond_to?(:compact)
    GC.start(full_mark: true, immediate_sweep: true)

    expect(weak_geom.weakref_alive?).to be(true)
    expect(index.bbox.max_x).to eq(2.0)
  end

  it "allows the borrowed geometry wrapper to be collected only after the index is released" do
    geom = TG::Geometry.parse_wkt(polygon_a_wkt)
    weak_geom = WeakRef.new(geom)

    index = TG::Geometry::Index.build([[:zone, geom]], via: :geom, strategy: :flat)
    geom = nil

    GC.start(full_mark: true, immediate_sweep: true)
    GC.compact if GC.respond_to?(:compact)
    expect(weak_geom.weakref_alive?).to be(true)

    index = nil
    5.times do
      GC.start(full_mark: true, immediate_sweep: true)
      GC.compact if GC.respond_to?(:compact)
    end

    expect(weak_geom.weakref_alive?).to be_falsey
  end

  it "does not free borrowed native geometry when the index is disposed" do
    geom = TG::Geometry.parse_wkt(polygon_a_wkt)

    index = TG::Geometry::Index.build([[:zone, geom]], via: :geom, strategy: :flat)
    expect(index.size).to eq(1)

    index = nil
    GC.start(full_mark: true, immediate_sweep: true)
    GC.compact if GC.respond_to?(:compact)

    expect(geom.bbox.max_x).to eq(2.0)
    expect(geom.covers_xy?(1.0, 1.0)).to be(true)
  end

  it "does not count borrowed native geometry bytes in Index ObjectSpace.memsize_of" do
    point_count = 1_000
    coordinates = Array.new(point_count) do |i|
      angle = (2.0 * Math::PI * i) / point_count
      [Math.cos(angle), Math.sin(angle)]
    end
    coordinates << coordinates.first

    geojson = JSON.generate(type: "Polygon", coordinates: [coordinates])
    geom = TG::Geometry.parse_geojson(geojson)

    borrowed_index = TG::Geometry::Index.build([[:large, geom]], via: :geom, strategy: :flat)
    owned_index = TG::Geometry::Index.build([[:large, geojson]], via: :geojson, strategy: :flat)

    expect(ObjectSpace.memsize_of(geom)).to be > 10_000
    expect(ObjectSpace.memsize_of(borrowed_index)).to be < ObjectSpace.memsize_of(geom)
    expect(ObjectSpace.memsize_of(owned_index)).to be > ObjectSpace.memsize_of(borrowed_index)
  end

  it "requires TG::Geometry::Geom values for via: :geom" do
    expect do
      TG::Geometry::Index.build([[:bad, polygon_a_wkt]], via: :geom, strategy: :flat)
    end.to raise_error(TypeError)
  end
end
