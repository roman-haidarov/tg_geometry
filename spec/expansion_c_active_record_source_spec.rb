# frozen_string_literal: true

require "spec_helper"

RSpec.describe TG::Geometry::ActiveRecordSource do
  Record = Struct.new(:id, :geojson, keyword_init: true)

  let(:zone) do
    '{"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]]]}'
  end

  it "builds entries from enumerable records without requiring Rails" do
    records = [Record.new(id: :a, geojson: zone)]

    expect(described_class.call(records, id: :id, geometry: :geojson)).to eq([[:a, zone]])
  end

  it "supports proc readers" do
    records = [Record.new(id: :a, geojson: zone)]

    entries = described_class.call(
      records,
      id: ->(record) { record.id.to_s },
      geometry: ->(record) { record.geojson }
    )

    expect(entries).to eq([["a", zone]])
  end

  it "can feed a Registry source" do
    records = [Record.new(id: :a, geojson: zone)]
    registry = TG::Geometry::Registry.new(
      source: described_class.registry_source(records, id: :id, geometry: :geojson),
      via: :geojson,
      strategy: :flat
    )

    registry.reload!

    expect(registry.find_covering(5, 5)).to eq(:a)
  end
end
