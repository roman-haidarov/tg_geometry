# frozen_string_literal: true

require "spec_helper"
require "stringio"

RSpec.describe "FeatureSource" do
  let(:fixture_dir) { File.join(__dir__, "fixtures", "feature_source") }
  let(:simple_path) { File.join(fixture_dir, "simple_feature_collection.geojson") }
  let(:simple_json) { File.binread(simple_path) }

  it "defines TG::Geometry::FeatureSource" do
    expect(TG::Geometry.const_defined?(:FeatureSource)).to be(true)
  end

  it "reads entries from JSON, file, and IO without parsing properties to Hash" do
    expected = [["zone/a", /"Polygon"/], [2, /"MultiPolygon"/]]

    [
      TG::Geometry::FeatureSource.read_entries_json(simple_json),
      TG::Geometry::FeatureSource.read_entries_file(simple_path),
      TG::Geometry::FeatureSource.read_entries_io(StringIO.new(simple_json))
    ].each do |entries|
      expect(entries.size).to eq(2)
      expected.each_with_index do |(id, geometry_matcher), i|
        expect(entries[i][0]).to eq(id)
        expect(entries[i][1]).to match(geometry_matcher)
        expect(entries[i][1]).to be_a(String)
      end
    end
  end

  it "reads features with raw properties JSON" do
    features = TG::Geometry::FeatureSource.read_features_json(simple_json)

    expect(features.size).to eq(2)
    expect(features[0][0]).to eq("zone/a")
    expect(features[0][1]).to include('"Polygon"')
    expect(features[0][2]).to include('"@id": "zone/a"')
    expect(features[1][2]).to include('"@id": 2')
  end

  it "uses the OSM-style default id path [properties, @id]" do
    json = File.binread(File.join(fixture_dir, "osm_like_feature_collection.geojson"))

    entries = TG::Geometry::FeatureSource.read_entries_json(json)

    expect(entries.first.first).to eq("relation/100")
  end

  it "filters non polygon types by default and reports filtered separately" do
    json = File.binread(File.join(fixture_dir, "mixed_geometry_types.geojson"))

    report = TG::Geometry::FeatureSource.read_entries_json(json, report: true)

    expect(report[:entries].map(&:first)).to eq(["poly"])
    expect(report[:filtered]).to eq(2)
    expect(report[:skipped]).to eq(0)
    expect(report[:errors]).to eq([])
  end

  it "supports only: nil for no geometry type filtering" do
    json = File.binread(File.join(fixture_dir, "mixed_geometry_types.geojson"))

    entries = TG::Geometry::FeatureSource.read_entries_json(json, only: nil)

    expect(entries.map(&:first)).to eq(%w[poly point line])
  end

  it "requires report mode for skip options" do
    expect do
      TG::Geometry::FeatureSource.read_entries_json(simple_json, on_invalid: :skip)
    end.to raise_error(TG::Geometry::ArgumentError)

    expect do
      TG::Geometry::FeatureSource.read_entries_json(simple_json, on_missing_id: :skip)
    end.to raise_error(TG::Geometry::ArgumentError)
  end

  it "supports on_missing_id: :ordinal" do
    json = File.binread(File.join(fixture_dir, "properties_null_missing.geojson"))

    entries = TG::Geometry::FeatureSource.read_entries_json(json, on_missing_id: :ordinal)

    expect(entries.map(&:first)).to eq(["feature/0", "feature/1"])
  end

  it "supports on_missing_id: :skip in report mode without requiring on_invalid: :skip" do
    json = File.binread(File.join(fixture_dir, "properties_null_missing.geojson"))

    report = TG::Geometry::FeatureSource.read_entries_json(json,
                                                           report: true,
                                                           on_missing_id: :skip)

    expect(report[:entries]).to eq([])
    expect(report[:skipped]).to eq(2)
    expect(report[:filtered]).to eq(0)
    expect(report[:errors].map { |e| e[:feature_index] }).to eq([0, 1])
  end

  it "reports invalid geometry with capped errors and exact skipped count" do
    json = File.binread(File.join(fixture_dir, "invalid_geometry_middle.geojson"))

    report = TG::Geometry::FeatureSource.read_entries_json(json,
                                                           report: true,
                                                           on_invalid: :skip,
                                                           max_errors: 1)

    expect(report[:entries].map(&:first)).to eq(["ok-a", "ok-b"])
    expect(report[:skipped]).to eq(1)
    expect(report[:errors].size).to eq(1)
    expect(report[:errors].first[:feature_index]).to eq(1)
    expect(report[:errors].first[:reason]).to include("invalid geometry")
  end

  it "raises ParseError for malformed JSON and non FeatureCollection roots" do
    malformed = File.binread(File.join(fixture_dir, "malformed_json.geojson"))

    expect { TG::Geometry::FeatureSource.read_entries_json(malformed) }
      .to raise_error(TG::Geometry::ParseError)
    expect { TG::Geometry::FeatureSource.read_entries_json('{"type":"Feature"}') }
      .to raise_error(TG::Geometry::ParseError)
  end

  it "builds an Index directly from JSON, file, and IO" do
    indexes = [
      TG::Geometry::FeatureSource.build_index_json(simple_json, strategy: :flat),
      TG::Geometry::FeatureSource.build_index_file(simple_path, strategy: :flat),
      TG::Geometry::FeatureSource.build_index_io(StringIO.new(simple_json), strategy: :flat)
    ]

    indexes.each do |index|
      expect(index).to be_a(TG::Geometry::Index)
      expect(index).to be_frozen
      expect(index.size).to eq(2)
      expect(index.find_covering(5, 5)).to eq("zone/a")
      expect(index.find_covering(25, 25)).to eq(2)
    end
  end

  it "matches Index.build(read_entries, via: :geojson) behavior" do
    entries = TG::Geometry::FeatureSource.read_entries_json(simple_json)
    from_entries = TG::Geometry::Index.build(entries, via: :geojson, strategy: :flat)
    direct = TG::Geometry::FeatureSource.build_index_json(simple_json, strategy: :flat)

    [[5, 5], [25, 25], [100, 100]].each do |lon, lat|
      expect(direct.find_covering(lon, lat)).to eq(from_entries.find_covering(lon, lat))
    end
  end


  it "raises ParseError with a string reason for missing geometry" do
    json = '{"type":"FeatureCollection","features":[{"type":"Feature","properties":{"@id":"x"}}]}'

    expect { TG::Geometry::FeatureSource.read_entries_json(json) }
      .to raise_error(TG::Geometry::ParseError, /feature 0.*missing geometry/)
  end

  it "reports missing geometry with a string reason in skip mode" do
    json = '{"type":"FeatureCollection","features":[{"type":"Feature","properties":{"@id":"x"}}]}'

    report = TG::Geometry::FeatureSource.read_entries_json(
      json,
      report: true,
      on_invalid: :skip
    )

    expect(report[:entries]).to eq([])
    expect(report[:skipped]).to eq(1)
    expect(report[:filtered]).to eq(0)
    expect(report[:errors].first[:reason]).to be_a(String)
    expect(report[:errors].first[:reason]).to include("missing geometry")
  end

  it "skips invalid id type when on_invalid is skip and report is true" do
    json = <<~JSON
      {"type":"FeatureCollection","features":[
        {"type":"Feature","properties":{"@id":false},"geometry":{"type":"Polygon","coordinates":[[[0,0],[0,1],[1,1],[1,0],[0,0]]]}}
      ]}
    JSON

    report = TG::Geometry::FeatureSource.read_entries_json(
      json,
      report: true,
      on_invalid: :skip
    )

    expect(report[:entries]).to eq([])
    expect(report[:skipped]).to eq(1)
    expect(report[:filtered]).to eq(0)
    expect(report[:errors].first[:reason]).to include("invalid id")
  end

  it "skips fractional numeric id when on_invalid is skip and report is true" do
    json = <<~JSON
      {"type":"FeatureCollection","features":[
        {"type":"Feature","properties":{"@id":1.2},"geometry":{"type":"Polygon","coordinates":[[[0,0],[0,1],[1,1],[1,0],[0,0]]]}}
      ]}
    JSON

    report = TG::Geometry::FeatureSource.read_entries_json(
      json,
      report: true,
      on_invalid: :skip
    )

    expect(report[:entries]).to eq([])
    expect(report[:skipped]).to eq(1)
    expect(report[:errors].first[:reason]).to include("numeric id must be an integer")
  end

  it "raises ParseError for malformed feature shapes" do
    cases = [
      '[1]',
      '{"type":"Feature","properties":{"@id":"x"},"geometry":null}',
      '{"type":"Feature","properties":{"@id":"x"},"geometry":"bad"}',
      '{"type":"Feature","properties":{"@id":"x"},"geometry":{"coordinates":[]}}',
      '{"type":"Feature","properties":{"@id":"x"},"geometry":{"type":1,"coordinates":[]}}'
    ]

    cases.each do |feature_json|
      json = "{\"type\":\"FeatureCollection\",\"features\":[#{feature_json}]}"
      expect { TG::Geometry::FeatureSource.read_entries_json(json) }
        .to raise_error(TG::Geometry::ParseError)
    end
  end

  it "raises ParseError for missing or non-array features" do
    expect { TG::Geometry::FeatureSource.read_entries_json('{"type":"FeatureCollection"}') }
      .to raise_error(TG::Geometry::ParseError, /features/)

    expect { TG::Geometry::FeatureSource.read_entries_json('{"type":"FeatureCollection","features":{}}') }
      .to raise_error(TG::Geometry::ParseError, /features/)
  end

  it "treats invalid properties type as invalid feature for read_features" do
    json = <<~JSON
      {"type":"FeatureCollection","features":[
        {"type":"Feature","properties":[],"geometry":{"type":"Polygon","coordinates":[[[0,0],[0,1],[1,1],[1,0],[0,0]]]}}
      ]}
    JSON

    expect { TG::Geometry::FeatureSource.read_features_json(json, on_missing_id: :ordinal) }
      .to raise_error(TG::Geometry::ParseError, /properties must be an object or null/)
  end

  it "rejects unknown keywords" do
    expect { TG::Geometry::FeatureSource.read_entries_json(simple_json, typo: 1) }
      .to raise_error(TG::Geometry::ArgumentError, /unknown keyword/)
  end

  it "surfaces missing file errors as file errors" do
    missing = File.join(fixture_dir, "does_not_exist.geojson")

    expect { TG::Geometry::FeatureSource.read_entries_file(missing) }
      .to raise_error(Errno::ENOENT)
  end

  it "rejects invalid id types" do
    json = <<~JSON
      {"type":"FeatureCollection","features":[
        {"type":"Feature","properties":{"@id":false},"geometry":{"type":"Polygon","coordinates":[[[0,0],[0,1],[1,1],[1,0],[0,0]]]}}
      ]}
    JSON

    expect { TG::Geometry::FeatureSource.read_entries_json(json) }
      .to raise_error(TG::Geometry::ArgumentError)
  end
end
