# frozen_string_literal: true

require "benchmark"
require "rbconfig"

ROOT = File.expand_path("..", __dir__)
EXT_DIR = File.join(ROOT, "ext", "tg_geometry")
LIB_DIR = File.join(ROOT, "lib")

$LOAD_PATH.unshift(LIB_DIR) unless $LOAD_PATH.include?(LIB_DIR)
$LOAD_PATH.unshift(EXT_DIR) unless $LOAD_PATH.include?(EXT_DIR)

begin
  require "tg/geometry"
rescue LoadError
  warn "Native extension is not built. Run: bundle exec rake compile"
  raise
end

module TGGeometryBench
  module_function

  SIZES = [100, 500, 1_000, 5_000, 50_000].freeze
  FAST_SIZES = [100, 500, 1_000].freeze

  def sizes
    ENV["TGEOMETRY_BENCH_FULL"] == "1" ? SIZES : FAST_SIZES
  end

  def iterations(default)
    Integer(ENV.fetch("TGEOMETRY_BENCH_ITERATIONS", default.to_s))
  end

  def box_wkt(min_x, min_y, max_x, max_y)
    "POLYGON ((#{min_x} #{min_y}, #{max_x} #{min_y}, #{max_x} #{max_y}, #{min_x} #{max_y}, #{min_x} #{min_y}))"
  end

  def box_geojson(min_x, min_y, max_x, max_y)
    %({"type":"Polygon","coordinates":[[[#{min_x},#{min_y}],[#{max_x},#{min_y}],[#{max_x},#{max_y}],[#{min_x},#{max_y}],[#{min_x},#{min_y}]]]})
  end

  def compact_entries(count)
    Array.new(count) do |i|
      x = i % 250
      y = i / 250
      [i, box_geojson(x, y, x + 0.8, y + 0.8)]
    end
  end

  def long_thin_entries(count)
    Array.new(count) do |i|
      [i, box_geojson(i * 0.01, i, i * 0.01 + 1_000.0, i + 0.05)]
    end
  end

  def overlapping_entries(count)
    Array.new(count) do |i|
      offset = i * 0.001
      [i, box_geojson(offset, offset, 100.0 + offset, 100.0 + offset)]
    end
  end

  def entries_for(kind, count)
    case kind
    when :compact then compact_entries(count)
    when :long_thin then long_thin_entries(count)
    when :overlapping then overlapping_entries(count)
    else raise ArgumentError, "unknown benchmark kind: #{kind.inspect}"
    end
  end

  def build_index(entries, strategy:)
    TG::Geometry::Index.build(entries, via: :geojson, strategy: strategy)
  end

  def points_for(kind)
    case kind
    when :compact then [[0.4, 0.4], [10.4, 2.4], [10_000.0, 10_000.0]]
    when :long_thin then [[1.0, 10.02], [500.0, 500.02], [-1.0, -1.0]]
    when :overlapping then [[50.0, 50.0], [0.0, 0.0], [200.0, 200.0]]
    else raise ArgumentError, "unknown benchmark kind: #{kind.inspect}"
    end
  end

  def rects_for(kind)
    case kind
    when :compact then [[0, 0, 10, 10], [100, 100, 105, 105]]
    when :long_thin then [[0, 10, 1_000, 10.05], [2_000, 2_000, 2_001, 2_001]]
    when :overlapping then [[25, 25, 75, 75], [200, 200, 201, 201]]
    else raise ArgumentError, "unknown benchmark kind: #{kind.inspect}"
    end
  end

  def packed_points(points)
    points.flatten.pack("d*")
  end

  def rss_kb
    case RbConfig::CONFIG["host_os"]
    when /linux/i
      File.read("/proc/self/status")[/^VmRSS:\s+(\d+)\s+kB/, 1].to_i
    when /darwin/i
      Integer(`ps -o rss= -p #{Process.pid}`)
    else
      0
    end
  rescue StandardError
    0
  end

  def say_header(title)
    puts "\n== #{title} =="
    puts "ruby=#{RUBY_VERSION} platform=#{RUBY_PLATFORM} full=#{ENV['TGEOMETRY_BENCH_FULL'] == '1'}"
  end
end
