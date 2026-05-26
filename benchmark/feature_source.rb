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

sizes = ENV["TGEOMETRY_BENCH_FULL"] == "1" ? [100, 1_000, 10_000, 50_000] : [100, 1_000]

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

    ruby_time = Benchmark.realtime do
      ruby_entries = FeatureSourceBenchData.ruby_json_parse_entries(path)
    end

    read_entries_time = Benchmark.realtime do
      feature_entries = TG::Geometry::FeatureSource.read_entries_file(path, id: ["properties", "@id"])
    end

    read_features_time = Benchmark.realtime do
      feature_rows = TG::Geometry::FeatureSource.read_features_file(path, id: ["properties", "@id"])
    end

    direct_index_time = Benchmark.realtime do
      direct_index = TG::Geometry::FeatureSource.build_index_file(path, id: ["properties", "@id"], strategy: :rtree)
    end

    roundtrip_index_time = Benchmark.realtime do
      roundtrip_index = TG::Geometry::Index.build(feature_entries, via: :geojson, strategy: :rtree)
    end

    puts "n=#{size} ruby_json_parse_sec=%.6f read_entries_sec=%.6f read_features_sec=%.6f build_index_direct_sec=%.6f build_index_from_entries_sec=%.6f entries=%d features=%d direct_size=%d roundtrip_size=%d rss_kb=%d" % [
      ruby_time,
      read_entries_time,
      read_features_time,
      direct_index_time,
      roundtrip_index_time,
      ruby_entries.length,
      feature_rows.length,
      direct_index.size,
      roundtrip_index.size,
      TGGeometryBench.rss_kb
    ]
  ensure
    file.close!
  end
end
