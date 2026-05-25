# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Release Core Block 9 flat query engine" do
  let(:zone_a) { TG::Geometry.parse_wkt("POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))") }
  let(:zone_b) { TG::Geometry.parse_wkt("POLYGON ((5 5, 12 5, 12 12, 5 12, 5 5))") }
  let(:zone_c) { TG::Geometry.parse_wkt("POLYGON ((20 20, 22 20, 22 22, 20 22, 20 20))") }

  it "finds the first covering id by insertion order" do
    index = TG::Geometry::Index.build([[:a, zone_a], [:b, zone_b]], via: :geom, strategy: :flat)

    expect(index.find_covering(6, 6)).to eq(:a)
    expect(index.find_covering(11, 11)).to eq(:b)
    expect(index.find_covering(100, 100)).to be_nil
  end

  it "returns all covering ids in insertion order, including duplicates and false ids" do
    index = TG::Geometry::Index.build([[false, zone_a], [:dup, zone_a], [:dup, zone_b]],
                                      via: :geom,
                                      strategy: :flat)

    expect(index.covering_ids(6, 6)).to eq([false, :dup, :dup])
    expect(index.covering_ids(1, 1)).to eq([false, :dup])
  end

  it "distinguishes :covers from strict :contains on boundaries" do
    covers_index = TG::Geometry::Index.build([[:zone, zone_a]], via: :geom, strategy: :flat)
    contains_index = TG::Geometry::Index.build([[:zone, zone_a]],
                                               via: :geom,
                                               strategy: :flat,
                                               predicate: :contains)

    expect(covers_index.find_covering(0, 5)).to eq(:zone)
    expect(contains_index.find_covering(0, 5)).to be_nil
    expect(contains_index.find_covering(5, 5)).to eq(:zone)
  end

  it "returns exact-filtered intersecting rect ids in insertion order" do
    index = TG::Geometry::Index.build([[:a, zone_a], [:b, zone_b], [:c, zone_c]],
                                      via: :geom,
                                      strategy: :flat)

    expect(index.intersecting_rect(9, 9, 11, 11)).to eq(%i[a b])
    expect(index.intersecting_rect(30, 30, 31, 31)).to eq([])
  end

  it "handles empty indexes" do
    index = TG::Geometry::Index.build([], via: :geojson, strategy: :flat)

    expect(index.find_covering(1, 1)).to be_nil
    expect(index.covering_ids(1, 1)).to eq([])
    expect(index.intersecting_rect(0, 0, 1, 1)).to eq([])
  end

  it "rejects non-finite point and rect query coordinates" do
    index = TG::Geometry::Index.build([[:a, zone_a]], via: :geom, strategy: :flat)

    expect { index.find_covering(Float::NAN, 1) }.to raise_error(TG::Geometry::ArgumentError)
    expect { index.covering_ids(1, Float::INFINITY) }.to raise_error(TG::Geometry::ArgumentError)
    expect { index.intersecting_rect(0, 0, Float::INFINITY, 1) }
      .to raise_error(TG::Geometry::ArgumentError)
    expect { index.intersecting_rect(2, 0, 1, 1) }.to raise_error(TG::Geometry::ArgumentError)
  end
end
