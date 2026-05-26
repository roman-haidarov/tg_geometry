# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Auto strategy status" do
  it "does not expose :auto in the first public release" do
    entries = [[1, '{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}']]

    expect(TG::Geometry::Index).not_to respond_to(:auto_strategy_threshold)
    expect do
      TG::Geometry::Index.build(entries, via: :geojson, strategy: :auto)
    end.to raise_error(TG::Geometry::ArgumentError)
  end
end
