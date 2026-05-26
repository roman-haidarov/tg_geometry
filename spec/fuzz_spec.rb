# frozen_string_literal: true

require "spec_helper"
require "fileutils"

RSpec.describe "Fuzz hardening" do
  ITERATIONS = Integer(ENV.fetch("TG_GEOMETRY_FUZZ_ITERATIONS", "2000")).freeze
  SEED = Integer(ENV.fetch("TG_GEOMETRY_FUZZ_SEED", "20260524")).freeze
  CORPUS_DIR = File.expand_path("fixtures/fuzz_corpus", __dir__).freeze

  ACCEPTABLE_ERRORS = [
    TG::Geometry::ParseError,
    TG::Geometry::ArgumentError,
    ::ArgumentError,
    ::EncodingError,
    ::NoMemoryError,
    ::SystemStackError
  ].freeze

  def acceptable?(error)
    ACCEPTABLE_ERRORS.any? { |k| error.is_a?(k) }
  end

  def persist_failure(format, input, error)
    FileUtils.mkdir_p(CORPUS_DIR)
    digest = input.hash.abs.to_s(16)
    path = File.join(CORPUS_DIR, "#{format}_#{digest}.bin")
    File.binwrite(path, input)
    warn "[fuzz] persisted crash input for #{format}: #{path} (#{error.class}: #{error.message[0, 80]})"
  end

  def feed(format, input)
    case format
    when :geojson then TG::Geometry.parse_geojson(input)
    when :wkt     then TG::Geometry.parse_wkt(input)
    when :wkb     then TG::Geometry.parse_wkb(input)
    when :auto    then TG::Geometry.parse(input)
    else raise ArgumentError, "unknown format #{format.inspect}"
    end
  end

  def fuzz_one(format, input)
    feed(format, input)
    :parsed
  rescue Exception => e # rubocop:disable Lint/RescueException
    if acceptable?(e)
      :rejected
    else
      persist_failure(format, input, e)
      raise
    end
  end

  shared_examples "fuzz parser" do |format|
    it "tolerates random ASCII strings for #{format}" do
      rng = Random.new(SEED + format.hash)
      results = Hash.new(0)

      ITERATIONS.times do
        len = rng.rand(0..512)
        input = Array.new(len) { rng.rand(32..126).chr }.join
        results[fuzz_one(format, input)] += 1
      end

      expect(results.values.sum).to eq(ITERATIONS)
    end

    it "tolerates random raw bytes for #{format}" do
      rng = Random.new(SEED + format.hash + 1)
      results = Hash.new(0)

      ITERATIONS.times do
        len = rng.rand(0..512)
        input = rng.bytes(len).force_encoding(Encoding::ASCII_8BIT)
        results[fuzz_one(format, input)] += 1
      end

      expect(results.values.sum).to eq(ITERATIONS)
    end

    it "tolerates the empty string for #{format}" do
      fuzz_one(format, "")
    end

    it "tolerates strings of NUL bytes for #{format}" do
      [1, 16, 256, 4096].each do |size|
        input = ("\x00" * size).force_encoding(Encoding::ASCII_8BIT)
        fuzz_one(format, input)
      end
    end
  end

  include_examples "fuzz parser", :geojson
  include_examples "fuzz parser", :wkt
  include_examples "fuzz parser", :wkb
  include_examples "fuzz parser", :auto

  describe "deeply nested GeoJSON" do
    it "rejects pathological nesting without stack-corrupting the process" do
      input = ("{\"type\":\"GeometryCollection\",\"geometries\":[" * 1000) +
              "{\"type\":\"Point\",\"coordinates\":[0,0]}" +
              ("]}" * 1000)

      fuzz_one(:geojson, input)
    end

    it "rejects deeply nested JSON arrays" do
      depth = 5_000
      input = ("[" * depth) + ("]" * depth)
      fuzz_one(:geojson, input)
    end
  end

  describe "huge inputs" do
    it "tolerates a 1 MiB random WKB blob" do
      rng = Random.new(SEED + 9999)
      input = rng.bytes(1 << 20).force_encoding(Encoding::ASCII_8BIT)
      fuzz_one(:wkb, input)
    end

    it "tolerates a 1 MiB random text blob via auto-detect" do
      rng = Random.new(SEED + 8888)
      bytes = rng.bytes(1 << 20)
      input = bytes.tr("\x00-\x1f", " ").force_encoding(Encoding::UTF_8)
      fuzz_one(:auto, input)
    end
  end

  describe "regression corpus" do
    it "re-parses every previously persisted crash input without crashing" do
      skip "no corpus directory" unless Dir.exist?(CORPUS_DIR)

      paths = Dir.glob(File.join(CORPUS_DIR, "*.bin"))
      skip "corpus is empty" if paths.empty?

      paths.each do |path|
        format = File.basename(path).split("_", 2).first.to_sym
        next unless %i[geojson wkt wkb auto].include?(format)

        input = File.binread(path)
        fuzz_one(format, input)
      end
    end
  end
end
