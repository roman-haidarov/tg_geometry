# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Index geometry queries" do
  def polygon(min_x, min_y, max_x, max_y)
    TG::Geometry.polygon([[min_x, min_y], [max_x, min_y], [max_x, max_y], [min_x, max_y], [min_x, min_y]])
  end

  let(:zones) do
    [
      [:a, polygon(0, 0, 10, 10)],
      [:b, polygon(8, 8, 18, 18)],
      [:c, polygon(30, 30, 40, 40)]
    ]
  end

  def build(strategy)
    TG::Geometry::Index.build(zones, via: :geom, strategy: strategy)
  end

  it "returns intersecting ids in insertion order" do
    query = polygon(9, 9, 12, 12)

    expect(build(:flat).intersecting_geom_ids(query)).to eq(%i[a b])
    expect(build(:rtree).intersecting_geom_ids(query)).to eq(%i[a b])
  end

  it "supports line query intersections" do
    query = TG::Geometry.line_string([[35, 35], [45, 45]])

    expect(build(:flat).intersecting_geom_ids(query)).to eq([:c])
  end

  it "returns empty arrays for outside query geometries" do
    expect(build(:rtree).intersecting_geom_ids(polygon(100, 100, 101, 101))).to eq([])
  end

  it "uses stored_geom covers query semantics" do
    index = build(:flat)

    expect(index.covering_geom_ids(TG::Geometry.point(5, 5))).to eq([:a])
    expect(index.covering_geom_ids(TG::Geometry.point(0, 5))).to eq([:a])
    expect(index.covering_geom_ids(polygon(9, 9, 12, 12))).to eq([:b])
  end

  it "uses stored_geom contains query semantics and excludes boundary" do
    index = build(:flat)

    expect(index.containing_geom_ids(TG::Geometry.point(5, 5))).to eq([:a])
    expect(index.containing_geom_ids(TG::Geometry.point(0, 5))).to eq([])
    expect(index.covering_geom_ids(TG::Geometry.point(0, 5))).to eq([:a])
  end

  it "does not read Index.build predicate for geometry methods" do
    index = TG::Geometry::Index.build(zones, via: :geom, strategy: :flat, predicate: :contains)

    expect(index.covering_geom_ids(TG::Geometry.point(0, 5))).to eq([:a])
  end

  it "raises TypeError for non-Geom query values" do
    index = build(:flat)

    expect { index.intersecting_geom_ids(nil) }.to raise_error(TypeError)
    expect { index.covering_geom_ids("bad") }.to raise_error(TypeError)
    expect { index.containing_geom_ids(1) }.to raise_error(TypeError)
  end
end
