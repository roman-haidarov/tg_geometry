# frozen_string_literal: true

require "spec_helper"

RSpec.describe "SRID metadata" do
  let(:plain_wkb) { TG::Geometry.parse_wkt("POINT (1 2)").to_wkb }
  let(:ewkb_4326) { TG::Geometry.parse_wkt("POINT (1 2)").to_ewkb(srid: 4326) }
  let(:ewkb_3857) { TG::Geometry.parse_wkt("POINT (1 2)").to_ewkb(srid: 3857) }
  let(:ewkb_0) { TG::Geometry.parse_wkt("POINT (1 2)").to_ewkb(srid: 0) }

  it "returns nil for plain WKB and non-SRID text formats" do
    expect(TG::Geometry.parse_wkb(plain_wkb).srid).to be_nil
    expect(TG::Geometry.parse_geojson('{"type":"Point","coordinates":[1,2]}').srid).to be_nil
    expect(TG::Geometry.parse_wkt("POINT (1 2)").srid).to be_nil
  end

  it "extracts SRID from little-endian EWKB and hex EWKB" do
    geom = TG::Geometry.parse_wkb(ewkb_4326)

    expect(geom.srid).to eq(4326)
    expect([geom.bbox.min_x, geom.bbox.min_y]).to eq([1.0, 2.0])
    expect(TG::Geometry.parse_wkb(ewkb_3857).srid).to eq(3857)
    expect(TG::Geometry.parse_wkb(ewkb_0).srid).to eq(0)
    expect(TG::Geometry.parse_hex(ewkb_4326.unpack1("H*")).srid).to eq(4326)
  end

  it "extracts SRID from big-endian EWKB" do
    # Big-endian EWKB: byte order 0, type POINT | SRID flag, SRID 4326, x=1.0, y=2.0.
    ewkb = [0, 0x20000001, 4326].pack("C N N") + [1.0, 2.0].pack("G G")

    expect(TG::Geometry.parse_wkb(ewkb).srid).to eq(4326)
    expect(TG::Geometry.parse_wkb(ewkb).point).to eq([1.0, 2.0])
  end

  it "does not crash on malformed EWKB" do
    expect { TG::Geometry.parse_wkb("\x01\x01\x00".b) }.to raise_error(TG::Geometry::ParseError)
  end

  it "uses constructor srid metadata" do
    expect(TG::Geometry.polygon([[0, 0], [1, 0], [1, 1], [0, 0]], srid: 4326).srid).to eq(4326)
    expect(TG::Geometry.polygon([[0, 0], [1, 0], [1, 1], [0, 0]]).srid).to be_nil
  end
end
