# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Expansion Block E low-level geometry wrappers" do
  it "exposes Point coordinates without constructing a mutable point wrapper" do
    geom = TG::Geometry.parse_wkt("POINT (1 2)")

    expect(geom.point).to eq([1.0, 2.0])
    expect(geom.line).to be_nil
    expect(geom.polygon).to be_nil
  end

  it "exposes borrowed LineString accessors while keeping the parent Geom alive" do
    geom = TG::Geometry.parse_wkt("LINESTRING (0 0, 3 4)")
    line = geom.line

    expect(line).to be_a(TG::Geometry::Line)
    expect(line).to be_frozen
    expect(line.num_points).to eq(2)
    expect(line.num_segments).to eq(1)
    expect(line.point_at(1)).to eq([3.0, 4.0])
    expect(line.points).to eq([[0.0, 0.0], [3.0, 4.0]])
    expect(line.length).to eq(5.0)
    expect(line.bbox.center).to eq([1.5, 2.0])

    geom = nil
    GC.start
    GC.compact if GC.respond_to?(:compact)

    expect(line.length).to eq(5.0)
    expect(line.points).to eq([[0.0, 0.0], [3.0, 4.0]])
  end

  it "exposes borrowed Polygon and Ring accessors without freeing child pointers" do
    geom = TG::Geometry.parse_wkt(
      "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0), (2 2, 4 2, 4 4, 2 4, 2 2))"
    )
    polygon = geom.polygon
    exterior = polygon.exterior_ring
    hole = polygon.hole_at(0)

    expect(polygon).to be_a(TG::Geometry::Polygon)
    expect(polygon).to be_frozen
    expect(polygon.num_holes).to eq(1)
    expect(polygon.holes.map(&:class)).to eq([TG::Geometry::Ring])
    expect(polygon.bbox.center).to eq([5.0, 5.0])

    expect(exterior).to be_a(TG::Geometry::Ring)
    expect(exterior).to be_frozen
    expect(exterior.num_points).to eq(5)
    expect(exterior.num_segments).to eq(4)
    expect(exterior.point_at(2)).to eq([10.0, 10.0])
    expect(exterior.points.first).to eq([0.0, 0.0])
    expect(exterior.area).to eq(100.0)
    expect(exterior.perimeter).to eq(40.0)
    expect(exterior.convex?).to be(true)
    expect(hole.area).to eq(4.0)

    geom = nil
    polygon = nil
    GC.start
    GC.compact if GC.respond_to?(:compact)

    expect(exterior.area).to eq(100.0)
    expect(hole.perimeter).to eq(8.0)
  end

  it "keeps low-level wrapper allocation private" do
    expect { TG::Geometry::Line.allocate }.to raise_error(TypeError)
    expect { TG::Geometry::Ring.allocate }.to raise_error(TypeError)
    expect { TG::Geometry::Polygon.allocate }.to raise_error(TypeError)
  end

  it "rejects out-of-range child indexes" do
    line = TG::Geometry.parse_wkt("LINESTRING (0 0, 1 1)").line
    polygon = TG::Geometry.parse_wkt("POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))").polygon

    expect { line.point_at(2) }.to raise_error(TG::Geometry::ArgumentError)
    expect { polygon.hole_at(0) }.to raise_error(TG::Geometry::ArgumentError)
  end
end
