# frozen_string_literal: true

require "spec_helper"

RSpec.describe TG::Geometry::Registry do
  let(:zone_a) do
    '{"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]]]}'
  end

  let(:zone_b) do
    '{"type":"Polygon","coordinates":[[[20,20],[30,20],[30,30],[20,30],[20,20]]]}'
  end

  it "builds and swaps immutable indexes on reload" do
    current_entries = [[:a, zone_a]]
    registry = described_class.new(source: -> { current_entries }, via: :geojson, strategy: :flat)
    old_index = registry.reload!

    expect(registry.find_covering(5, 5)).to eq(:a)

    current_entries = [[:b, zone_b]]
    new_index = registry.reload!

    expect(old_index.find_covering(5, 5)).to eq(:a)
    expect(new_index.find_covering(25, 25)).to eq(:b)
    expect(registry.find_covering(25, 25)).to eq(:b)
  end

  it "supports subclass source definitions" do
    entries = [[:a, zone_a]]
    klass = Class.new(described_class) do
      source { entries }
    end

    registry = klass.new(via: :geojson, strategy: :flat)
    registry.reload!

    expect(registry.loaded?).to eq(true)
    expect(registry.covering_ids(5, 5)).to eq([:a])
  end

  it "raises before reload" do
    registry = described_class.new(entries: [[:a, zone_a]], via: :geojson, strategy: :flat)

    expect { registry.find_covering(5, 5) }.to raise_error(TG::Geometry::Error)
  end
end
