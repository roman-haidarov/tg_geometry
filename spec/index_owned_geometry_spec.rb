# frozen_string_literal: true

require "spec_helper"
require "objspace"

RSpec.describe "Index owned geometry ingestion" do
  let(:geojson_a) do
    '{"type":"Polygon","coordinates":[[[0,0],[2,0],[2,2],[0,2],[0,0]]]}'
  end

  let(:geojson_b) do
    '{"type":"Polygon","coordinates":[[[10,10],[12,10],[12,12],[10,12],[10,10]]]}'
  end

  let(:geojson_c) do
    '{"type":"Polygon","coordinates":[[[-5,-4],[-2,-4],[-2,-1],[-5,-1],[-5,-4]]]}'
  end

  let(:wkb_a) { TG::Geometry.parse_geojson(geojson_a).to_wkb }
  let(:wkb_b) { TG::Geometry.parse_geojson(geojson_b).to_wkb }
  let(:wkb_c) { TG::Geometry.parse_geojson(geojson_c).to_wkb }

  it "builds an immutable index via: :geojson and computes the union bbox" do
    index = TG::Geometry::Index.build([[:a, geojson_a], [:b, geojson_b], [:c, geojson_c]],
                                      via: :geojson,
                                      strategy: :flat)

    expect(index).to be_frozen
    expect(index.size).to eq(3)
    expect(index.strategy).to eq(:flat)
    expect(index.predicate).to eq(:covers)

    bbox = index.bbox
    expect(bbox.min_x).to eq(-5.0)
    expect(bbox.min_y).to eq(-4.0)
    expect(bbox.max_x).to eq(12.0)
    expect(bbox.max_y).to eq(12.0)
  end

  it "builds an immutable index via: :wkb and treats input strings as bytes" do
    index = TG::Geometry::Index.build([[:a, wkb_a], [:b, wkb_b], [:c, wkb_c]],
                                      via: :wkb,
                                      strategy: :flat,
                                      geometry_index: :natural)

    expect(index).to be_frozen
    expect(index.size).to eq(3)

    bbox = index.bbox
    expect(bbox.min_x).to eq(-5.0)
    expect(bbox.min_y).to eq(-4.0)
    expect(bbox.max_x).to eq(12.0)
    expect(bbox.max_y).to eq(12.0)
  end

  it "raises ParseError for malformed geojson at first, middle, and last positions" do
    malformed = "not geojson"

    expect do
      TG::Geometry::Index.build([[1, malformed], [2, geojson_a], [3, geojson_b]],
                                via: :geojson,
                                strategy: :flat)
    end.to raise_error(TG::Geometry::ParseError)

    expect do
      TG::Geometry::Index.build([[1, geojson_a], [2, malformed], [3, geojson_b]],
                                via: :geojson,
                                strategy: :flat)
    end.to raise_error(TG::Geometry::ParseError)

    expect do
      TG::Geometry::Index.build([[1, geojson_a], [2, geojson_b], [3, malformed]],
                                via: :geojson,
                                strategy: :flat)
    end.to raise_error(TG::Geometry::ParseError)

    GC.start
    GC.compact if GC.respond_to?(:compact)
  end

  it "raises ParseError for malformed wkb at first, middle, and last positions" do
    malformed = "not wkb".b

    expect do
      TG::Geometry::Index.build([[1, malformed], [2, wkb_a], [3, wkb_b]],
                                via: :wkb,
                                strategy: :flat)
    end.to raise_error(TG::Geometry::ParseError)

    expect do
      TG::Geometry::Index.build([[1, wkb_a], [2, malformed], [3, wkb_b]],
                                via: :wkb,
                                strategy: :flat)
    end.to raise_error(TG::Geometry::ParseError)

    expect do
      TG::Geometry::Index.build([[1, wkb_a], [2, wkb_b], [3, malformed]],
                                via: :wkb,
                                strategy: :flat)
    end.to raise_error(TG::Geometry::ParseError)

    GC.start
    GC.compact if GC.respond_to?(:compact)
  end

  it "disposes already parsed owned geometries after partial build failure" do
    20.times do
      expect do
        TG::Geometry::Index.build([[1, geojson_a], [2, geojson_b], [3, "bad geojson"]],
                                  via: :geojson,
                                  strategy: :flat)
      end.to raise_error(TG::Geometry::ParseError)
    end

    GC.start
    GC.compact if GC.respond_to?(:compact)
  end

  it "reports owned geometry native memory through ObjectSpace.memsize_of" do
    borrowed_geom = TG::Geometry.parse_geojson(geojson_a)
    borrowed = TG::Geometry::Index.build([[:a, borrowed_geom]], via: :geom, strategy: :flat)
    owned = TG::Geometry::Index.build([[:a, geojson_a]], via: :geojson, strategy: :flat)

    expect(ObjectSpace.memsize_of(owned)).to be > ObjectSpace.memsize_of(borrowed)
  end

  it "survives GC.stress and GC.compact for owned geometry build/free" do
    old_stress = GC.stress
    GC.stress = true

    5.times do
      index = TG::Geometry::Index.build([[:a, geojson_a], [:b, geojson_b]],
                                        via: :geojson,
                                        strategy: :flat)
      expect(index.size).to eq(2)
      expect(index.bbox).to be_a(TG::Geometry::Rect)
    end
  ensure
    GC.stress = old_stress
    GC.start
    GC.compact if GC.respond_to?(:compact)
  end
end
