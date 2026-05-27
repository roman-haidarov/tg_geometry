# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Constructors" do
  let(:square) { [[0.0, 0.0], [10.0, 0.0], [10.0, 10.0], [0.0, 10.0], [0.0, 0.0]] }
  let(:hole_a) { [[2.0, 2.0], [4.0, 2.0], [4.0, 4.0], [2.0, 4.0], [2.0, 2.0]] }
  let(:hole_b) { [[6.0, 6.0], [8.0, 6.0], [8.0, 8.0], [6.0, 8.0], [6.0, 6.0]] }

  describe ".line_string" do
    it "builds a frozen two-point linestring" do
      geom = TG::Geometry.line_string([[0.0, 0.0], [1.0, 1.0]])

      expect(geom).to be_frozen
      expect(geom.type).to eq(:linestring)
      expect(geom.line.num_points).to eq(2)
      expect(geom.srid).to be_nil
    end

    it "builds a 100-point linestring" do
      points = 100.times.map { |i| [i.to_f, (i * 2).to_f] }
      geom = TG::Geometry.line_string(points)

      expect(geom.line.num_points).to eq(100)
    end

    it "supports explicit index values and srid metadata" do
      %i[default natural ystripes none].each do |index|
        geom = TG::Geometry.line_string([[0, 0], [1, 1]], index: index, srid: 4326)
        expect(geom.srid).to eq(4326)
      end
    end

    it "rejects too few points" do
      expect { TG::Geometry.line_string([[0, 0]]) }
        .to raise_error(TG::Geometry::ArgumentError, "line_string requires at least 2 points, got 1")
      expect { TG::Geometry.line_string([]) }
        .to raise_error(TG::Geometry::ArgumentError, "line_string requires at least 2 points, got 0")
    end

    it "rejects non-finite coordinates with the bad point index in the message" do
      expect { TG::Geometry.line_string([[0, 0], [Float::NAN, 1]]) }
        .to raise_error(TG::Geometry::ArgumentError, /point 1/)
      expect { TG::Geometry.line_string([[0, 0], [1, Float::INFINITY]]) }
        .to raise_error(TG::Geometry::ArgumentError, /point 1/)
    end

    it "rejects out-of-range srid" do
      expect { TG::Geometry.line_string([[0, 0], [1, 1]], srid: -1) }
        .to raise_error(TG::Geometry::ArgumentError)
    end
  end

  describe ".polygon" do
    it "builds a square polygon" do
      geom = TG::Geometry.polygon(square)

      expect(geom.type).to eq(:polygon)
      expect(geom.polygon.num_holes).to eq(0)
      expect(geom.covers_xy?(5, 5)).to be(true)
    end

    it "builds polygons with one or two holes" do
      one_hole = TG::Geometry.polygon(square, holes: [hole_a], srid: 4326)
      two_holes = TG::Geometry.polygon(square, holes: [hole_a, hole_b])

      expect(one_hole.srid).to eq(4326)
      expect(one_hole.polygon.num_holes).to eq(1)
      expect(one_hole.covers_xy?(3, 3)).to be(false)
      expect(two_holes.polygon.num_holes).to eq(2)
    end

    it "treats holes: [] like omitted holes" do
      expect(TG::Geometry.polygon(square, holes: []).polygon.num_holes).to eq(0)
      expect(TG::Geometry.polygon(square).polygon.num_holes).to eq(0)
    end

    it "rejects invalid rings without autoclosing" do
      expect { TG::Geometry.polygon(square[0...-1]) }
        .to raise_error(TG::Geometry::ArgumentError, "polygon exterior ring is not closed")
      expect { TG::Geometry.polygon(square[0, 3]) }
        .to raise_error(TG::Geometry::ArgumentError, "polygon exterior ring requires at least 4 points")
      expect { TG::Geometry.polygon(square, holes: [hole_a[0...-1]]) }
        .to raise_error(TG::Geometry::ArgumentError, "polygon hole 0 is not closed")
      expect { TG::Geometry.polygon(square, holes: [hole_a[0, 3]]) }
        .to raise_error(TG::Geometry::ArgumentError, "polygon hole 0 requires at least 4 points")
    end
  end

  describe ".multi_polygon" do
    it "builds empty and non-empty multipolygons" do
      expect(TG::Geometry.multi_polygon([]).type).to eq(:multipolygon)

      geom = TG::Geometry.multi_polygon([
        { exterior: square, holes: [hole_a] },
        [[20.0, 20.0], [21.0, 20.0], [21.0, 21.0], [20.0, 21.0], [20.0, 20.0]]
      ], srid: 3857)

      expect(geom.type).to eq(:multipolygon)
      expect(geom.srid).to eq(3857)
      expect(geom.num_polygons).to eq(2)
    end
  end
end
