# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Geom#to_ewkb" do
  let(:geom_without_srid) { TG::Geometry.parse_wkt("POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))") }
  let(:geom_with_srid) { TG::Geometry.parse_wkb(geom_without_srid.to_ewkb(srid: 4326)) }

  it "roundtrips semantic geometry and SRID" do
    ewkb = geom_with_srid.to_ewkb
    reparsed = TG::Geometry.parse_wkb(ewkb)

    expect(reparsed.srid).to eq(4326)
    expect(reparsed.type).to eq(:polygon)
    expect(reparsed.bbox.max_x).to eq(10.0)
    expect(reparsed.covers_xy?(5, 5)).to be(true)
  end

  it "can add or override SRID explicitly" do
    expect(TG::Geometry.parse_wkb(geom_without_srid.to_ewkb(srid: 4326)).srid).to eq(4326)
    expect(TG::Geometry.parse_wkb(geom_with_srid.to_ewkb(srid: 3857)).srid).to eq(3857)
  end

  it "returns frozen binary strings" do
    ewkb = geom_with_srid.to_ewkb

    expect(ewkb.encoding).to eq(Encoding::BINARY)
    expect(ewkb).to be_frozen
  end

  it "rejects missing or invalid SRID" do
    expect { geom_without_srid.to_ewkb }.to raise_error(TG::Geometry::ArgumentError)
    expect { geom_with_srid.to_ewkb(srid: -1) }.to raise_error(TG::Geometry::ArgumentError)
    expect { geom_with_srid.to_ewkb(srid: 2**31) }.to raise_error(TG::Geometry::ArgumentError)
    expect { geom_with_srid.to_ewkb(srid: "4326") }.to raise_error(TG::Geometry::ArgumentError)
  end
end
