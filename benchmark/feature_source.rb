# frozen_string_literal: true

require_relative "_support"
require "json"
require "tempfile"

module FeatureSourceBenchData
  module_function

  def feature_collection(count)
    features = Array.new(count) do |i|
      x = i % 250
      y = i / 250
      <<~JSON.chomp
        {"type":"Feature","properties":{"@id":"zone/#{i}","name":"Zone #{i}"},"geometry":#{TGGeometryBench.box_geojson(x, y, x + 0.8, y + 0.8)}}
      JSON
    end

    %({"type":"FeatureCollection","features":[#{features.join(",")}]} )
  end

  def write_temp_geojson(json)
    file = Tempfile.new(["tg_geometry_feature_source", ".geojson"])
    file.binmode
    file.write(json)
    file.flush
    file
  end

  def ruby_json_parse_entries(path)
    parsed = JSON.parse(File.binread(path))
    parsed.fetch("features").filter_map do |feature|
      geometry = feature["geometry"]
      next unless %w[Polygon MultiPolygon].include?(geometry["type"])

      [feature.fetch("properties").fetch("@id"), JSON.generate(geometry)]
    end
  end
end

TGGeometryBench.say_header("feature_source")

sizes = TGGeometryBench.full? ? [100, 1_000, 10_000, 50_000] : [100, 1_000, 10_000]

sizes.each do |size|
  json = FeatureSourceBenchData.feature_collection(size)
  file = FeatureSourceBenchData.write_temp_geojson(json)
  path = file.path

  begin
    ruby_entries = nil
    feature_entries = nil
    feature_rows = nil
    direct_index = nil
    roundtrip_index = nil

    ruby = TGGeometryBench.measure_counted(initial_iterations: 1, min_seconds: 0.30) do |iterations|
      iterations.times { ruby_entries = FeatureSourceBenchData.ruby_json_parse_entries(path) }
    end

    read_entries = TGGeometryBench.measure_counted(initial_iterations: 1, min_seconds: 0.30) do |iterations|
      iterations.times do
        feature_entries = TG::Geometry::FeatureSource.read_entries_file(path, id: ["properties", "@id"])
      end
    end

    read_features = TGGeometryBench.measure_counted(initial_iterations: 1, min_seconds: 0.30) do |iterations|
      iterations.times do
        feature_rows = TG::Geometry::FeatureSource.read_features_file(path, id: ["properties", "@id"])
      end
    end

    direct_index_stat = TGGeometryBench.measure_counted(initial_iterations: 1, min_seconds: 0.30) do |iterations|
      iterations.times do
        direct_index = TG::Geometry::FeatureSource.build_index_file(path, id: ["properties", "@id"], strategy: :rtree)
      end
    end

    # Ensure the roundtrip benchmark does not accidentally include read_entries.
    feature_entries ||= TG::Geometry::FeatureSource.read_entries_file(path, id: ["properties", "@id"])

    roundtrip_index_stat = TGGeometryBench.measure_counted(initial_iterations: 1, min_seconds: 0.30) do |iterations|
      iterations.times do
        roundtrip_index = TG::Geometry::Index.build(feature_entries, via: :geojson, strategy: :rtree)
      end
    end

    read_plus_roundtrip_sec = read_entries[:median_sec] + roundtrip_index_stat[:median_sec]
    read_plus_roundtrip_ops_per_sec = read_plus_roundtrip_sec.positive? ? read_entries[:iterations] / read_plus_roundtrip_sec : 0.0

    common = {
      n: size,
      json_bytes: json.bytesize,
      ruby_entries: ruby_entries.length,
      feature_entries: feature_entries.length,
      feature_rows: feature_rows.length,
      direct_size: direct_index.size,
      roundtrip_size: roundtrip_index.size,
      rss_kb: TGGeometryBench.rss_kb
    }

    TGGeometryBench.report("feature_source", common.merge(mode: :ruby_json_parse_entries), stats: ruby)
    TGGeometryBench.report("feature_source", common.merge(mode: :read_entries_file), stats: read_entries)
    TGGeometryBench.report("feature_source", common.merge(mode: :read_features_file), stats: read_features)
    TGGeometryBench.report("feature_source", common.merge(mode: :build_index_direct), stats: direct_index_stat)
    TGGeometryBench.report("feature_source", common.merge(mode: :build_index_from_prepared_entries), stats: roundtrip_index_stat)
    TGGeometryBench.report(
      "feature_source_end_to_end",
      common.merge(
        mode: :read_entries_plus_build_index,
        median_sec: read_plus_roundtrip_sec,
        ops_per_sec: read_plus_roundtrip_ops_per_sec
      )
    )
  ensure
    file.close!
  end
end
