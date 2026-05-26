# frozen_string_literal: true

require "spec_helper"

RSpec.describe "TG API coverage" do
  it "adds safe point and empty geometry constructors without public allocation" do
    point = TG::Geometry.point_zm(1, 2, 3, 4)

    expect(point).to be_a(TG::Geometry::Geom)
    expect(point).to be_frozen
    expect(point.type).to eq(:point)
    expect(point.point).to eq([1.0, 2.0])
    expect(point.dims).to eq(4)
    expect(point.has_z?).to be(true)
    expect(point.has_m?).to be(true)
    expect(point.z).to eq(3.0)
    expect(point.m).to eq(4.0)
    expect(point.extra_coords).to eq([])
    expect(point.to_wkt).to eq("POINT(1 2 3 4)")

    expect(TG::Geometry.point(1, 2).dims).to eq(2)
    expect(TG::Geometry.point_z(1, 2, 3).z).to eq(3.0)
    expect(TG::Geometry.point_m(1, 2, 4).m).to eq(4.0)

    expect(TG::Geometry.empty_point.to_wkt).to eq("POINT EMPTY")
    expect(TG::Geometry.empty_linestring.to_wkt).to eq("LINESTRING EMPTY")
    expect(TG::Geometry.empty_polygon.to_wkt).to eq("POLYGON EMPTY")
    expect(TG::Geometry.empty_multipoint.to_wkt).to eq("MULTIPOINT EMPTY")
    expect(TG::Geometry.empty_multilinestring.to_wkt).to eq("MULTILINESTRING EMPTY")
    expect(TG::Geometry.empty_multipolygon.to_wkt).to eq("MULTIPOLYGON EMPTY")
    expect(TG::Geometry.empty_geometrycollection.to_wkt).to eq("GEOMETRYCOLLECTION EMPTY")

    expect { TG::Geometry.point(Float::NAN, 2) }.to raise_error(TG::Geometry::ArgumentError)
    expect { TG::Geometry::Geom.allocate }.to raise_error(TypeError)
  end

  it "exposes additional TG predicates without changing == semantics" do
    square = TG::Geometry.parse_wkt("POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))")
    same_square = TG::Geometry.parse_wkt("POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))")
    inner = TG::Geometry.parse_wkt("POINT (5 5)")
    boundary = TG::Geometry.parse_wkt("POINT (0 0)")
    outside = TG::Geometry.parse_wkt("POINT (20 20)")

    expect(square.equals?(same_square)).to be(true)
    expect(square.covers?(boundary)).to be(true)
    expect(square.contains?(boundary)).to be(false)
    expect(inner.within?(square)).to be(true)
    expect(boundary.covered_by?(square)).to be(true)
    expect(square.disjoint?(outside)).to be(true)
    expect(square.intersects_xy?(5, 5)).to be(true)
    expect(square.intersects_rect?(TG::Geometry::Rect.new(9, 9, 11, 11))).to be(true)
    expect(square.intersects_rect?(20, 20, 21, 21)).to be(false)
    expect(square == same_square).to be(false)
  end

  it "exposes collection accessors through borrowed immutable Geom wrappers" do
    geom = TG::Geometry.parse_wkt(
      "GEOMETRYCOLLECTION (POINT (1 2), LINESTRING (0 0, 3 4), POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0)))"
    )

    expect(geom.type).to eq(:geometrycollection)
    expect(geom.num_geometries).to eq(3)
    expect(geom.geometries.map(&:type)).to eq(%i[point linestring polygon])
    expect(geom.geometry_at(0).point).to eq([1.0, 2.0])
    expect(geom.geometry_at(1).line.length).to eq(5.0)
    expect(geom.geometry_at(2).polygon.exterior_ring.area).to eq(1.0)

    child = geom.geometry_at(2)
    index = TG::Geometry::Index.build([[:child, child]], via: :geom, strategy: :flat)
    geom = nil
    child = nil
    GC.start
    GC.compact if GC.respond_to?(:compact)

    expect(index.find_covering(0.5, 0.5)).to eq(:child)
  end

  it "exposes multipoint, multiline, and multipolygon child accessors" do
    multipoint = TG::Geometry.parse_wkt("MULTIPOINT ((1 2), (3 4))")
    multiline = TG::Geometry.parse_wkt("MULTILINESTRING ((0 0, 3 4), (10 10, 11 11))")
    multipolygon = TG::Geometry.parse_wkt(
      "MULTIPOLYGON (((0 0, 1 0, 1 1, 0 1, 0 0)), ((10 10, 11 10, 11 11, 10 11, 10 10)))"
    )

    expect(multipoint.num_points).to eq(2)
    expect(multipoint.points).to eq([[1.0, 2.0], [3.0, 4.0]])
    expect(multiline.num_lines).to eq(2)
    expect(multiline.lines.map(&:length)).to eq([5.0, Math.sqrt(2)])
    expect(multipolygon.num_polygons).to eq(2)
    expect(multipolygon.polygon_at(1).bbox.center).to eq([10.5, 10.5])
  end

  it "exposes value Segment wrappers from Line and Ring" do
    line = TG::Geometry.parse_wkt("LINESTRING (0 0, 3 4, 6 4)").line
    ring = TG::Geometry.parse_wkt("POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))").polygon.exterior_ring

    segment = line.segment_at(0)
    other = line.segment_at(1)

    expect(segment).to be_a(TG::Geometry::Segment)
    expect(segment).to be_frozen
    expect(segment.a).to eq([0.0, 0.0])
    expect(segment.b).to eq([3.0, 4.0])
    expect(segment.points).to eq([[0.0, 0.0], [3.0, 4.0]])
    expect(segment.bbox.center).to eq([1.5, 2.0])
    expect(segment.intersects?(segment)).to be(true)
    expect(segment.intersects?(other)).to be(true)
    expect(line.segments.size).to eq(2)
    expect(ring.segments.size).to eq(4)

    expect { TG::Geometry::Segment.allocate }.to raise_error(TypeError)
    expect { line.segment_at(2) }.to raise_error(TG::Geometry::ArgumentError)
  end
end
