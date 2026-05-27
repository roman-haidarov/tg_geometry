# frozen_string_literal: true

require "spec_helper"
require "tg/geometry/active_record_type"

RSpec.describe TG::Geometry::ActiveRecordType do
  let(:type) { described_class.new }
  let(:ewkb) { TG::Geometry.parse_wkt("POINT (1 2)").to_ewkb(srid: 4326) }
  let(:geom) { TG::Geometry.parse_wkb(ewkb) }

  it "deserializes supported read shapes" do
    expect(type.type).to eq(:tg_geometry)
    expect(type.deserialize(nil)).to be_nil
    expect(type.deserialize(geom)).to equal(geom)
    expect(type.deserialize(ewkb).srid).to eq(4326)
    expect(type.deserialize(ewkb.unpack1("H*")).srid).to eq(4326)
    expect(type.deserialize("\\x#{ewkb.unpack1('H*')}").srid).to eq(4326)
    expect(type.deserialize('{"type":"Point","coordinates":[1,2]}').srid).to be_nil
    expect(type.deserialize("POINT (1 2)").srid).to be_nil
  end

  it "detects ASCII-8BIT hex before WKB fallback" do
    hex = ewkb.unpack1("H*").b

    expect(type.deserialize(hex).srid).to eq(4326)
  end

  it "is strict by default and can warn+nil in non-strict mode" do
    expect { type.deserialize("not geometry") }.to raise_error(TG::Geometry::ParseError)

    non_strict = described_class.new(strict: false)
    expect { expect(non_strict.deserialize("not geometry")).to be_nil }
      .to output(/parse failed/).to_stderr
  end

  it "is read-only on serialize" do
    expect(type.serialize(nil)).to be_nil
    expect(type.serialize("raw")).to eq("raw")
    expect { type.serialize(geom) }.to raise_error(TG::Geometry::ArgumentError)
  end

  it "uses cast as deserialize" do
    expect(type.cast(ewkb).srid).to eq(4326)
  end
end
