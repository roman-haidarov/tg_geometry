# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Release Core Block 11 rtree ordered results" do
  let(:entries) do
    [
      [:first, TG::Geometry.parse_wkt("POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))")],
      [:second, TG::Geometry.parse_wkt("POLYGON ((2 2, 12 2, 12 12, 2 12, 2 2))")],
      [:third, TG::Geometry.parse_wkt("POLYGON ((4 4, 14 4, 14 14, 4 14, 4 4))")]
    ]
  end

  it "matches flat result order for point queries" do
    flat = TG::Geometry::Index.build(entries, via: :geom, strategy: :flat)
    rtree = TG::Geometry::Index.build(entries, via: :geom, strategy: :rtree)

    expect(rtree.find_covering(5, 5)).to eq(flat.find_covering(5, 5))
    expect(rtree.covering_ids(5, 5)).to eq(flat.covering_ids(5, 5))
    expect(rtree.covering_ids(100, 100)).to eq([])
  end

  it "matches flat result order for rect queries" do
    flat = TG::Geometry::Index.build(entries, via: :geom, strategy: :flat)
    rtree = TG::Geometry::Index.build(entries, via: :geom, strategy: :rtree)

    expect(rtree.intersecting_rect(3, 3, 6, 6)).to eq(flat.intersecting_rect(3, 3, 6, 6))
    expect(rtree.intersecting_rect(100, 100, 101, 101)).to eq([])
  end

  it "handles many overlapping matches deterministically" do
    many = 50.times.map do |i|
      [i, TG::Geometry.parse_wkt("POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))")]
    end

    index = TG::Geometry::Index.build(many, via: :geom, strategy: :rtree)

    expect(index.find_covering(5, 5)).to eq(0)
    expect(index.covering_ids(5, 5)).to eq((0...50).to_a)
  end

  it "handles match buffer allocation failure in debug builds" do
    skip "TG_DEBUG_TEST hooks are not enabled" unless TG::Geometry.respond_to?(:_debug_fail_next_match_buffer_alloc!)

    index = TG::Geometry::Index.build(entries, via: :geom, strategy: :rtree)
    TG::Geometry._debug_reset_test_hooks!
    TG::Geometry._debug_fail_next_match_buffer_alloc!

    expect { index.covering_ids(5, 5) }.to raise_error(NoMemoryError)
  ensure
    TG::Geometry._debug_reset_test_hooks! if TG::Geometry.respond_to?(:_debug_reset_test_hooks!)
  end
end
