# frozen_string_literal: true

require "spec_helper"

RSpec.describe "nearest_segment" do
  it "finds nearest line segments and projection points" do
    line = TG::Geometry.line_string([[0, 0], [10, 0], [10, 10]]).line
    nearest = line.nearest_segment(5, 3)

    expect(nearest).to be_a(TG::Geometry::NearestSegment)
    expect(nearest).to be_frozen
    expect(nearest.segment).to be_a(TG::Geometry::Segment)
    expect(nearest.index).to be >= 0
    expect(nearest.distance).to be_within(1e-12).of(3.0)
    expect(nearest.point).to eq([5.0, 0.0])
  end

  it "returns zero distance for a point on a segment" do
    nearest = TG::Geometry.line_string([[0, 0], [10, 0]]).line.nearest_segment(5, 0)

    expect(nearest.index).to eq(0)
    expect(nearest.distance).to be_within(1e-12).of(0.0)
    expect(nearest.point).to eq([5.0, 0.0])
  end

  it "handles outside-bbox and degenerate segments" do
    outside = TG::Geometry.line_string([[0, 0], [10, 0]]).line.nearest_segment(20, 5)
    degenerate = TG::Geometry.line_string([[1, 1], [1, 1]]).line.nearest_segment(4, 5)

    expect(outside.index).to eq(0)
    expect(outside.point).to eq([10.0, 0.0])
    expect(degenerate.index).to eq(0)
    expect(degenerate.point).to eq([1.0, 1.0])
  end

  it "finds nearest ring segments without depending on equal-distance tie break" do
    ring = TG::Geometry.polygon([[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]]).polygon.exterior_ring
    center = ring.nearest_segment(5, 5)
    corner = ring.nearest_segment(0, 0)

    expect(center.index).to be_between(0, 3).inclusive
    expect(center.distance).to be_within(1e-12).of(5.0)
    expect(corner.distance).to be_within(1e-12).of(0.0)
  end

  it "keeps nearest result valid after parent GC" do
    nearest = TG::Geometry.line_string([[0, 0], [10, 0]]).line.nearest_segment(5, 2)

    GC.start
    GC.compact if GC.respond_to?(:compact)

    expect(nearest.segment.points).to eq([[0.0, 0.0], [10.0, 0.0]])
    expect(nearest.point).to eq([5.0, 0.0])
  end

  it "rejects non-finite coordinates" do
    line = TG::Geometry.line_string([[0, 0], [10, 0]]).line

    expect { line.nearest_segment(Float::NAN, 0) }.to raise_error(TG::Geometry::ArgumentError)
    expect { line.nearest_segment(0, Float::INFINITY) }.to raise_error(TG::Geometry::ArgumentError)
  end
end
