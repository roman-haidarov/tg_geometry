# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Release Core Block 12 packed batch API" do
  let(:zone_a) { TG::Geometry.parse_wkt("POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))") }
  let(:zone_b) { TG::Geometry.parse_wkt("POLYGON ((20 20, 30 20, 30 30, 20 30, 20 20))") }

  def pack_points(points)
    points.flatten.pack("d*")
  end

  it "returns one result per input point" do
    index = TG::Geometry::Index.build([[:a, zone_a], [:b, zone_b]], via: :geom, strategy: :flat)

    packed = pack_points([[1.0, 1.0], [25.0, 25.0], [100.0, 100.0]])
    expect(index.covering_ids_batch_packed(packed)).to eq([:a, :b, nil])
  end

  it "supports empty input" do
    index = TG::Geometry::Index.build([[:a, zone_a]], via: :geom, strategy: :flat)

    expect(index.covering_ids_batch_packed("".b)).to eq([])
  end

  it "rejects invalid input type, length, and non-finite values" do
    index = TG::Geometry::Index.build([[:a, zone_a]], via: :geom, strategy: :flat)

    expect { index.covering_ids_batch_packed(Object.new) }.to raise_error(TypeError)
    expect { index.covering_ids_batch_packed("123".b) }.to raise_error(TG::Geometry::ArgumentError)
    expect { index.covering_ids_batch_packed([Float::NAN, 1.0].pack("d*")) }
      .to raise_error(TG::Geometry::ArgumentError)
  end

  it "matches scalar find_covering for flat and rtree strategies" do
    points = [[1.0, 1.0], [25.0, 25.0], [100.0, 100.0], [0.0, 5.0]]

    %i[flat rtree].each do |strategy|
      index = TG::Geometry::Index.build([[:a, zone_a], [:b, zone_b]],
                                        via: :geom,
                                        strategy: strategy)
      packed = pack_points(points)
      expected = points.map { |lon, lat| index.find_covering(lon, lat) }

      expect(index.covering_ids_batch_packed(packed)).to eq(expected)
    end
  end

  it "returns the same VALUE ids, including mutable ids" do
    id = +"zone-a"
    index = TG::Geometry::Index.build([[id, zone_a]], via: :geom, strategy: :flat)

    expect(index.covering_ids_batch_packed(pack_points([[1.0, 1.0]])).first).to equal(id)
  end
end
