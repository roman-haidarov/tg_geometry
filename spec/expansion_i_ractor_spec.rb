# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Expansion Block I Ractor investigation" do
  it "keeps native wrappers outside Ractor shareability claims" do
    skip "Ractor is not available on this Ruby" unless defined?(Ractor)

    geom = TG::Geometry.parse_wkt("POINT (1 2)")
    rect = TG::Geometry::Rect.new(0, 0, 1, 1)
    index = TG::Geometry::Index.build(
      [[:zone, '{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}']],
      via: :geojson,
      strategy: :flat
    )

    expect(Ractor.shareable?(geom)).to be(false)
    expect(Ractor.shareable?(rect)).to be(false)
    expect(Ractor.shareable?(index)).to be(false)

    expect { Ractor.make_shareable(geom) }.to raise_error(Ractor::Error)
    expect { Ractor.make_shareable(rect) }.to raise_error(Ractor::Error)
    expect { Ractor.make_shareable(index) }.to raise_error(Ractor::Error)
  end
end
