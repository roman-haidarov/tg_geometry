# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Exact rtree accounting" do
  let(:zone_a) { TG::Geometry.parse_wkt("POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))") }
  let(:zone_b) { TG::Geometry.parse_wkt("POLYGON ((20 20, 30 20, 30 30, 20 30, 20 20))") }

  it "builds an rtree index and reports rtree native bytes in debug builds" do
    index = TG::Geometry::Index.build([[:a, zone_a], [:b, zone_b]], via: :geom, strategy: :rtree)

    expect(index.strategy).to eq(:rtree)
    if index.respond_to?(:_rtree_bytes_for_test)
      expect(index._rtree_bytes_for_test).to be > 0
    end
  end

  it "does not build an rtree for empty indexes" do
    index = TG::Geometry::Index.build([], via: :geojson, strategy: :rtree)

    expect(index.strategy).to eq(:rtree)
    expect(index.size).to eq(0)
    expect(index._rtree_bytes_for_test).to eq(0) if index.respond_to?(:_rtree_bytes_for_test)
  end

  it "returns rtree bytes to zero after dispose in debug builds" do
    index = TG::Geometry::Index.build([[:a, zone_a]], via: :geom, strategy: :rtree)

    skip "TG_DEBUG_TEST hooks are not enabled" unless index.respond_to?(:_force_dispose_for_test!)

    expect(index._rtree_bytes_for_test).to be > 0
    index._force_dispose_for_test!
    index._force_dispose_for_test!
    expect(index._rtree_bytes_for_test).to eq(0)
    expect(index._entries_bytes_for_test).to eq(0)
    expect(index._initialized_entries_for_test).to eq(0)
  end

  it "cleans up after rtree allocation failure in debug builds" do
    skip "TG_DEBUG_TEST hooks are not enabled" unless TG::Geometry.respond_to?(:_debug_fail_next_rtree_alloc!)

    TG::Geometry._debug_reset_test_hooks!
    TG::Geometry._debug_fail_next_rtree_alloc!

    expect do
      TG::Geometry::Index.build([[:a, zone_a], [:b, zone_b]], via: :geom, strategy: :rtree)
    end.to raise_error(NoMemoryError)
  ensure
    TG::Geometry._debug_reset_test_hooks! if TG::Geometry.respond_to?(:_debug_reset_test_hooks!)
  end

  it "cleans up after rtree insert allocation failure in debug builds" do
    skip "TG_DEBUG_TEST hooks are not enabled" unless TG::Geometry.respond_to?(:_debug_fail_rtree_alloc_after!)

    TG::Geometry._debug_reset_test_hooks!
    TG::Geometry._debug_fail_rtree_alloc_after!(1)

    expect do
      TG::Geometry::Index.build([[:a, zone_a], [:b, zone_b]], via: :geom, strategy: :rtree)
    end.to raise_error(NoMemoryError)
  ensure
    TG::Geometry._debug_reset_test_hooks! if TG::Geometry.respond_to?(:_debug_reset_test_hooks!)
  end

  it "allows two threads to build separate rtree indexes without cross-accounting" do
    indexes = Queue.new

    threads = 2.times.map do |i|
      Thread.new do
        geom = TG::Geometry.parse_wkt("POLYGON ((#{i * 20} 0, #{i * 20 + 10} 0, #{i * 20 + 10} 10, #{i * 20} 10, #{i * 20} 0))")
        indexes << TG::Geometry::Index.build([[i, geom]], via: :geom, strategy: :rtree)
      end
    end
    threads.each(&:join)

    built = 2.times.map { indexes.pop }
    expect(built.map(&:size)).to eq([1, 1])
    if built.all? { |idx| idx.respond_to?(:_rtree_bytes_for_test) }
      expect(built.map(&:_rtree_bytes_for_test)).to all(be > 0)
    end
  end
end
