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
  FAST_SIZES = [100, 1_000, 5_000].freeze

  DEFAULT_REPEATS = 5
  DEFAULT_MIN_SECONDS = 0.25
  DEFAULT_WARMUP_SECONDS = 0.05
  DEFAULT_MAX_ITERATIONS = 100_000_000

  def full?
    ENV["TGEOMETRY_BENCH_FULL"] == "1"
  end

  def sizes
    full? ? SIZES : FAST_SIZES
  end

  def env_integer(name, default, min: nil)
    value = Integer(ENV.fetch(name, default.to_s))
    if min && value < min
      raise ArgumentError, "#{name} must be >= #{min}, got #{value}"
    end
    value
  end

  def env_float(name, default, min: nil)
    value = Float(ENV.fetch(name, default.to_s))
    if min && value < min
      raise ArgumentError, "#{name} must be >= #{min}, got #{value}"
    end
    value
  end

  def initial_iterations(default)
    env_integer("TGEOMETRY_BENCH_ITERATIONS", default, min: 1)
  end

  def repeats(default = DEFAULT_REPEATS)
    env_integer("TGEOMETRY_BENCH_REPEATS", default, min: 1)
  end

  def min_seconds(default = DEFAULT_MIN_SECONDS)
    env_float("TGEOMETRY_BENCH_MIN_SECONDS", default, min: 0.0)
  end

  def warmup_seconds(default = DEFAULT_WARMUP_SECONDS)
    env_float("TGEOMETRY_BENCH_WARMUP_SECONDS", default, min: 0.0)
  end

  def max_iterations(default = DEFAULT_MAX_ITERATIONS)
    env_integer("TGEOMETRY_BENCH_MAX_ITERATIONS", default, min: 1)
  end

  def adaptive?
    ENV.fetch("TGEOMETRY_BENCH_ADAPTIVE", "1") != "0"
  end

  def disable_gc_during_timed?
    ENV["TGEOMETRY_BENCH_DISABLE_GC"] == "1"
  end

  def monotonic
    Process.clock_gettime(Process::CLOCK_MONOTONIC)
  end

  def median(values)
    sorted = values.sort
    n = sorted.length
    return 0.0 if n.zero?

    mid = n / 2
    n.odd? ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2.0
  end

  def percentile(values, pct)
    sorted = values.sort
    return 0.0 if sorted.empty?

    rank = (pct * (sorted.length - 1)).round
    sorted.fetch(rank)
  end

  def gc_start
    GC.start(full_mark: true, immediate_sweep: true)
  rescue ArgumentError
    GC.start
  end

  def timed_gc_delta
    before = GC.stat
    seconds = nil

    if disable_gc_during_timed?
      was_enabled = GC.enable
      GC.disable
      seconds = Benchmark.realtime { yield }
      GC.enable if was_enabled
    else
      seconds = Benchmark.realtime { yield }
    end

    after = GC.stat
    [
      seconds,
      after[:total_allocated_objects] - before[:total_allocated_objects],
      after[:minor_gc_count] - before[:minor_gc_count],
      after[:major_gc_count] - before[:major_gc_count]
    ]
  ensure
    GC.enable if disable_gc_during_timed?
  end

  def calibrate_iterations(initial_iterations:, min_seconds:, max_iterations: self.max_iterations, &block)
    iterations = initial_iterations
    return iterations unless adaptive?
    return iterations if min_seconds <= 0.0

    loop do
      gc_start
      seconds = Benchmark.realtime { block.call(iterations) }
      break iterations if seconds >= min_seconds || iterations >= max_iterations

      if seconds <= 0.0
        iterations = [iterations * 10, max_iterations].min
        next
      end

      target = (iterations * (min_seconds / seconds) * 1.50).ceil
      iterations = [[target, iterations * 2].max, max_iterations].min
    end
  end

  def warmup_for(seconds, iterations, &block)
    return if seconds <= 0.0

    deadline = monotonic + seconds
    begin
      block.call(iterations)
    end while monotonic < deadline
  end

  def measure_counted(initial_iterations:, operations_per_iteration: 1, min_seconds: self.min_seconds,
                      repeats: self.repeats, warmup_seconds: self.warmup_seconds,
                      max_iterations: self.max_iterations, &block)
    iterations = calibrate_iterations(
      initial_iterations: initial_iterations,
      min_seconds: min_seconds,
      max_iterations: max_iterations,
      &block
    )

    warmup_for(warmup_seconds, iterations, &block)

    seconds_samples = []
    allocation_samples = []
    minor_gc_samples = []
    major_gc_samples = []

    repeats.times do
      gc_start
      seconds, allocations, minor_gc, major_gc = timed_gc_delta { block.call(iterations) }
      seconds_samples << seconds
      allocation_samples << allocations
      minor_gc_samples << minor_gc
      major_gc_samples << major_gc
    end

    total_operations = iterations * operations_per_iteration
    median_sec = median(seconds_samples)

    {
      iterations: iterations,
      operations_per_iteration: operations_per_iteration,
      operations_per_repeat: total_operations,
      repeats: repeats,
      min_seconds: min_seconds,
      warmup_seconds: warmup_seconds,
      best_sec: seconds_samples.min,
      median_sec: median_sec,
      worst_sec: seconds_samples.max,
      mean_sec: seconds_samples.sum / seconds_samples.length,
      p90_sec: percentile(seconds_samples, 0.90),
      ops_per_sec: median_sec.positive? ? total_operations / median_sec : 0.0,
      ns_per_op: median_sec.positive? ? (median_sec / total_operations) * 1e9 : 0.0,
      spread_pct: seconds_samples.min.positive? ? ((seconds_samples.max - seconds_samples.min) / seconds_samples.min) * 100.0 : 0.0,
      median_allocations: median(allocation_samples),
      allocations_per_op: total_operations.positive? ? median(allocation_samples) / total_operations.to_f : 0.0,
      median_minor_gc: median(minor_gc_samples),
      median_major_gc: median(major_gc_samples),
      samples_sec: seconds_samples
    }
  end

  def format_value(value)
    case value
    when Float
      if value.finite?
        "%.6f" % value
      else
        value.to_s
      end
    when Array
      value.join(":")
    else
      value.to_s.gsub(/\s+/, "_")
    end
  end

  def output_format
    ENV.fetch("TGEOMETRY_BENCH_FORMAT", "table")
  end

  def kv_output?
    %w[kv both].include?(output_format)
  end

  def table_output?
    %w[table both].include?(output_format)
  end

  def report_rows
    @report_rows ||= []
  end

  def kv_line(payload)
    payload.map { |key, value| "#{key}=#{format_value(value)}" }.join(" ")
  end

  def report(benchmark, fields = nil, stats: nil, **kwargs)
    fields = (fields || {}).merge(kwargs)

    payload = {
      benchmark: benchmark,
      ruby: RUBY_VERSION,
      platform: RUBY_PLATFORM,
      full: full?,
      adaptive: adaptive?,
      gc_disabled: disable_gc_during_timed?
    }.merge(fields)

    if stats
      payload = payload.merge(
        iterations: stats[:iterations],
        ops_per_iteration: stats[:operations_per_iteration],
        operations: stats[:operations_per_repeat],
        repeats: stats[:repeats],
        min_seconds: stats[:min_seconds],
        median_sec: stats[:median_sec],
        best_sec: stats[:best_sec],
        worst_sec: stats[:worst_sec],
        ops_per_sec: stats[:ops_per_sec],
        ns_per_op: stats[:ns_per_op],
        spread_pct: stats[:spread_pct],
        median_allocations: stats[:median_allocations],
        allocations_per_op: stats[:allocations_per_op],
        median_minor_gc: stats[:median_minor_gc],
        median_major_gc: stats[:median_major_gc]
      )
    end

    puts kv_line(payload) if kv_output?
    report_rows << payload if table_output?
    payload
  end

  TABLE_CONTEXT_COLUMNS = [
    :kind, :n, :strategy, :mode,
    :query, :method, :case, :format,
    :library, :operation,
    :point_index, :lon, :lat,
    :rect_index, :rect,
    :segments, :target_bytes, :payload_bytes,
    :points_per_batch, :threads,
    :entries, :rebuilds, :cycle
  ].freeze

  TABLE_METRIC_COLUMNS = [
    :batches_per_sec, :ops_per_sec, :qps, :ns_per_op,
    :median_sec, :spread_pct, :allocations_per_op,
    :median_minor_gc, :median_major_gc,
    :iterations, :operations,
    :geom_memsize, :flat_memsize, :rtree_memsize, :rtree_over_flat,
    :start_rss_kb, :peak_rss_kb, :finish_rss_kb, :drift_kb, :max_drift_kb,
    :elapsed_sec, :queries, :sample_count, :rss_kb
  ].freeze

  TABLE_INTERNAL_COLUMNS = [
    :benchmark, :ruby, :platform, :full, :adaptive, :gc_disabled,
    :repeats, :min_seconds, :warmup_seconds, :max_iterations,
    :ops_per_iteration, :median_allocations,
    :best_sec, :worst_sec
  ].freeze

  TABLE_HEADERS = {
    kind: "kind",
    n: "n",
    strategy: "strategy",
    mode: "mode",
    query: "query",
    method: "method",
    case: "case",
    format: "format",
    library: "lib",
    operation: "op",
    point_index: "pt#",
    rect_index: "rect#",
    lon: "lon",
    lat: "lat",
    rect: "rect",
    segments: "segments",
    target_bytes: "target B",
    payload_bytes: "payload B",
    points_per_batch: "points/batch",
    batches_per_sec: "batches/s",
    ops_per_sec: "ops/s",
    qps: "qps",
    ns_per_op: "ns/op",
    median_sec: "median s",
    spread_pct: "spread %",
    allocations_per_op: "alloc/op",
    median_minor_gc: "minor GC",
    median_major_gc: "major GC",
    iterations: "iters",
    operations: "ops",
    threads: "threads",
    entries: "entries",
    rebuilds: "rebuilds",
    queries: "queries",
    cycle: "cycle",
    rss_kb: "rss KB",
    geom_memsize: "geom B",
    flat_memsize: "flat B",
    rtree_memsize: "rtree B",
    rtree_over_flat: "rtree-flat B",
    start_rss_kb: "start KB",
    peak_rss_kb: "peak KB",
    finish_rss_kb: "finish KB",
    drift_kb: "drift KB",
    max_drift_kb: "max drift KB",
    elapsed_sec: "elapsed s",
    sample_count: "samples"
  }.freeze

  def human_int(value)
    Integer(value).to_s.reverse.gsub(/(\d{3})(?=\d)/, '\\1_').reverse
  rescue StandardError
    value.to_s
  end

  def human_rate(value)
    v = Float(value)
    return "0" if v.zero?

    abs = v.abs
    if abs >= 1_000_000
      "%.2fM" % (v / 1_000_000.0)
    elsif abs >= 1_000
      "%.1fk" % (v / 1_000.0)
    elsif abs >= 100
      "%.1f" % v
    else
      "%.3f" % v
    end
  end

  def format_table_cell(key, value)
    return "" if value.nil?

    case key
    when :ops_per_sec, :batches_per_sec, :qps
      human_rate(value)
    when :ns_per_op
      "%.1f" % Float(value)
    when :median_sec, :elapsed_sec
      "%.4f" % Float(value)
    when :spread_pct
      "%.2f" % Float(value)
    when :allocations_per_op
      v = Float(value)
      v < 0.01 ? ("%.5f" % v) : ("%.3f" % v)
    when :lon, :lat
      "%.3f" % Float(value)
    when :rect
      Array(value).map { |v| (Float(v) % 1).zero? ? Integer(v).to_s : ("%.3f" % Float(v)) }.join(":")
    when :n, :iterations, :operations, :entries, :rebuilds, :queries, :cycle,
         :rss_kb, :geom_memsize, :flat_memsize, :rtree_memsize, :rtree_over_flat,
         :start_rss_kb, :peak_rss_kb, :finish_rss_kb, :drift_kb, :max_drift_kb,
         :target_bytes, :payload_bytes, :segments, :points_per_batch, :threads,
         :sample_count, :median_minor_gc, :median_major_gc
      human_int(value)
    when :full, :adaptive, :gc_disabled
      value ? "yes" : "no"
    else
      value.to_s
    end
  end

  def table_columns(rows)
    keys = rows.flat_map(&:keys).uniq
    context = TABLE_CONTEXT_COLUMNS.select { |key| keys.include?(key) }
    metrics = TABLE_METRIC_COLUMNS.select { |key| keys.include?(key) }
    extras = keys - context - metrics - TABLE_INTERNAL_COLUMNS
    context + extras + metrics
  end

  def print_table(rows, columns)
    headers = columns.map { |key| TABLE_HEADERS.fetch(key, key.to_s) }
    body = rows.map do |row|
      columns.map { |key| format_table_cell(key, row[key]) }
    end
    widths = headers.each_with_index.map do |header, index|
      ([header.length] + body.map { |cells| cells[index].length }).max
    end

    separator = "+-#{widths.map { |w| "-" * w }.join("-+-")}-+"
    puts separator
    puts "| #{headers.each_with_index.map { |h, i| h.ljust(widths[i]) }.join(" | ")} |"
    puts separator
    body.each do |cells|
      puts "| #{cells.each_with_index.map { |cell, i| cell.rjust(widths[i]) }.join(" | ")} |"
    end
    puts separator
  end

  def flush_reports
    rows = report_rows
    return if rows.empty?

    rows.group_by { |row| row[:benchmark] }.each do |benchmark, group_rows|
      notes, measured = group_rows.partition { |row| row.key?(:note) && row.keys.none? { |key| TABLE_METRIC_COLUMNS.include?(key) } }

      notes.each do |row|
        puts "\n-- #{benchmark} --"
        puts row[:note]
      end

      next if measured.empty?

      puts "\n-- #{benchmark} --"
      print_table(measured, table_columns(measured))
    end
  ensure
    rows&.clear
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

  def repeated_points(kind, count)
    seed = points_for(kind)
    Array.new(count) { |i| seed[i % seed.length] }
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
    puts "ruby=#{RUBY_VERSION} platform=#{RUBY_PLATFORM} sizes=#{sizes.join(":")} repeats=#{repeats} " \
         "min_seconds=#{format_table_cell(:median_sec, min_seconds)} warmup=#{format_table_cell(:median_sec, warmup_seconds)} " \
         "adaptive=#{adaptive? ? "yes" : "no"} gc_disabled=#{disable_gc_during_timed? ? "yes" : "no"}"
    puts "output=table; set TGEOMETRY_BENCH_FORMAT=kv for machine-readable key=value lines" if table_output? && !kv_output?
  end
end

at_exit do
  TGGeometryBench.flush_reports if TGGeometryBench.table_output?
end
