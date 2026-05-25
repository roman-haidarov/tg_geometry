#!/usr/bin/env ruby
# frozen_string_literal: true

require "digest"
require "fileutils"
require "open3"
require "optparse"
require "tmpdir"

VENDOR_DIR = File.expand_path("../ext/tg_geometry/vendor", __dir__)
MANIFEST_PATH = File.join(VENDOR_DIR, ".vendored")

PINS = {
  tg: {
    repo: "https://github.com/tidwall/tg.git",
    ref: "main",
    commit: "caf840504eaab4563280cf4ab16d618f69a23720",
    sha256: "f8e0d904055c209b2a23a2200456f9374ab95cadb69e61b5721d7f8e2500e705",
    target: "tg",
    files: %w[tg.c tg.h LICENSE README.md]
  },
  rtree: {
    repo: "https://github.com/tidwall/rtree.c.git",
    ref: "v0.5.3",
    commit: "5717a8a1eb373428ebaae8c1c623f186ec46461f",
    sha256: "4afc86cbd3abe03730206031a5aff5b8b29d37b055fc356052f6f06e1d1f9a61",
    target: "rtree",
    files: %w[rtree.c rtree.h LICENSE README.md]
  }
}.freeze

NORMALIZED_MTIME = Time.utc(2000, 1, 1).freeze
MANIFEST_HEADER = "# tg_geometry vendor manifest. Do not edit by hand. Regenerate with: ruby script/vendor_libs.rb --sync"

options = { mode: :sync }
OptionParser.new do |opts|
  opts.banner = "Usage: vendor_libs.rb [--verify | --sync] [--force]"
  opts.on("--verify", "Verify existing vendor tree against pinned files, VERSION metadata, and tree SHA256") { options[:mode] = :verify }
  opts.on("--sync", "Rebuild the vendor tree only when pinned content is missing or changed") { options[:mode] = :sync }
  opts.on("--force", "Force a fresh clone and rebuild even when the vendor tree is current") { options[:force] = true }
end.parse!

def sh!(cmd)
  system(*cmd) || abort("command failed: #{cmd.join(" ")}")
end

def capture!(*cmd)
  out, status = Open3.capture2(*cmd)
  abort("command failed: #{cmd.join(" ")}") unless status.success?

  out.strip
end

def vendor_candidate?(path)
  return false if File.symlink?(path)
  return false unless File.file?(path)
  return false if path.split(File::SEPARATOR).any? { |seg| seg.start_with?(".") && seg != "." && seg != ".." }

  true
end

def normalize_tree!(directory)
  Dir.glob(File.join(directory, "**", "*"), File::FNM_DOTMATCH).each do |path|
    base = File.basename(path)
    next if base == "." || base == ".."
    next if File.symlink?(path)

    if File.file?(path)
      File.chmod(0o644, path)
      File.utime(NORMALIZED_MTIME, NORMALIZED_MTIME, path)
    elsif File.directory?(path)
      File.chmod(0o755, path)
    end
  end
  File.utime(NORMALIZED_MTIME, NORMALIZED_MTIME, directory) if File.directory?(directory)
end

def copy_entry!(source_root, target_root, relative)
  src = File.join(source_root, relative)
  abort "missing required upstream file: #{src}" unless File.file?(src)
  abort "refusing to vendor symlink: #{src}" if File.symlink?(src)

  dest = File.join(target_root, relative)
  FileUtils.mkdir_p(File.dirname(dest))
  FileUtils.cp(src, dest, preserve: false)
end

def write_version!(target, pin)
  version = [
    "repo=#{pin.fetch(:repo)}",
    "ref=#{pin.fetch(:ref)}",
    "commit=#{pin.fetch(:commit)}"
  ].join("\n") + "\n"
  File.write(File.join(target, "VERSION"), version)
end

def tree_sha256_for(directory)
  entries = Dir.glob(File.join(directory, "**", "*"), File::FNM_DOTMATCH)
               .reject { |path| File.directory?(path) || File.symlink?(path) || %w[. ..].include?(File.basename(path)) }
               .sort

  digest = Digest::SHA256.new
  entries.each do |path|
    relative = path.sub(/\A#{Regexp.escape(directory)}\/?/, "")
    digest << relative << "\0"
    digest << File.binread(path)
    digest << "\0"
  end
  digest.hexdigest
end

def vendor_one!(name, pin)
  target = File.join(VENDOR_DIR, pin.fetch(:target))
  FileUtils.rm_rf(target)
  FileUtils.mkdir_p(target)

  Dir.mktmpdir("tg-geometry-#{name}-") do |tmpdir|
    clone_dir = File.join(tmpdir, pin.fetch(:target))
    sh!(["git", "clone", "--filter=blob:none", pin.fetch(:repo), clone_dir])
    sh!(["git", "-C", clone_dir, "fetch", "--depth", "1", "origin", pin.fetch(:commit)])
    sh!(["git", "-C", clone_dir, "checkout", "--detach", pin.fetch(:commit)])

    actual_commit = capture!("git", "-C", clone_dir, "rev-parse", "HEAD")
    abort "commit mismatch for #{name}: expected #{pin.fetch(:commit)}, got #{actual_commit}" unless actual_commit == pin.fetch(:commit)

    pin.fetch(:files).each { |relative| copy_entry!(clone_dir, target, relative) }
  end

  write_version!(target, pin)
  normalize_tree!(target)

  pin.merge(tree_sha256: tree_sha256_for(target))
end

def manifest_body_lines(results)
  lines = ["gem=tg_geometry", "libraries=#{PINS.keys.join(",")}"]
  results.each do |name, data|
    prefix = name.to_s
    lines << "#{prefix}_repo=#{data.fetch(:repo)}"
    lines << "#{prefix}_ref=#{data.fetch(:ref)}"
    lines << "#{prefix}_commit=#{data.fetch(:commit)}"
    lines << "#{prefix}_target=#{data.fetch(:target)}"
    lines << "#{prefix}_files=#{(data.fetch(:files) + ["VERSION"]).join(",")}"
    lines << "#{prefix}_tree_sha256=#{data.fetch(:tree_sha256)}"
  end
  lines
end

def manifest_signature(body_lines)
  Digest::SHA256.hexdigest(body_lines.join("\n") + "\n")
end

def write_manifest(results)
  body = manifest_body_lines(results)
  sig = manifest_signature(body)
  content = ([MANIFEST_HEADER] + body + ["manifest_sha256=#{sig}"]).join("\n") + "\n"
  FileUtils.mkdir_p(File.dirname(MANIFEST_PATH))
  File.write(MANIFEST_PATH, content)
  File.chmod(0o644, MANIFEST_PATH)
  File.utime(NORMALIZED_MTIME, NORMALIZED_MTIME, MANIFEST_PATH)
end

def expected_vendor_results
  PINS.each_with_object({}) do |(name, pin), results|
    results[name] = pin.merge(tree_sha256: pin.fetch(:sha256))
  end
end

def print_results(prefix, results)
  puts prefix
  results.each do |name, data|
    puts "  #{name}: commit=#{data.fetch(:commit)} tree_sha256=#{data.fetch(:tree_sha256)}"
  end
end

def parse_kv_file(path)
  return {} unless File.file?(path)

  File.readlines(path, chomp: true).each_with_object({}) do |line, hash|
    next if line.empty? || line.start_with?("#")

    key, value = line.split("=", 2)
    hash[key] = value if key && value
  end
end

def verify_vendor_tree
  failures = []

  PINS.each do |name, pin|
    target = File.join(VENDOR_DIR, pin.fetch(:target))
    unless Dir.exist?(target)
      failures << "#{name}: vendor directory missing (#{target})"
      next
    end

    (pin.fetch(:files) + ["VERSION"]).each do |relative|
      path = File.join(target, relative)
      failures << "#{name}: missing vendored file #{relative}" unless File.file?(path)
    end

    version = parse_kv_file(File.join(target, "VERSION"))
    failures << "#{name}: VERSION repo mismatch" unless version["repo"] == pin.fetch(:repo)
    failures << "#{name}: VERSION ref mismatch" unless version["ref"] == pin.fetch(:ref)
    failures << "#{name}: VERSION commit mismatch" unless version["commit"] == pin.fetch(:commit)

    next unless failures.none? { |failure| failure.start_with?("#{name}:") }

    actual_sha256 = tree_sha256_for(target)
    expected_sha256 = pin.fetch(:sha256)
    failures << "#{name}: tree_sha256 mismatch: expected #{expected_sha256}, got #{actual_sha256}" unless actual_sha256 == expected_sha256
  end

  expected_results = expected_vendor_results
  manifest = parse_kv_file(MANIFEST_PATH)
  if manifest.empty?
    failures << "manifest: missing #{MANIFEST_PATH}"
  else
    expected_body = manifest_body_lines(expected_results)
    expected_signature = manifest_signature(expected_body)

    expected_body.each do |line|
      key, expected_value = line.split("=", 2)
      failures << "manifest: #{key} mismatch" unless manifest[key] == expected_value
    end

    failures << "manifest: manifest_sha256 mismatch" unless manifest["manifest_sha256"] == expected_signature
  end

  failures
end

case options.fetch(:mode)
when :verify
  failures = verify_vendor_tree
  if failures.empty?
    print_results("vendor verify: ok", expected_vendor_results)
    exit 0
  end

  failures.each { |failure| warn failure }
  exit 1
when :sync
  unless options[:force]
    failures = verify_vendor_tree
    if failures.empty?
      print_results("vendor sync: up to date", expected_vendor_results)
      exit 0
    end
  end

  FileUtils.rm_rf(VENDOR_DIR)
  FileUtils.mkdir_p(VENDOR_DIR)

  results = {}
  PINS.each do |name, pin|
    results[name] = vendor_one!(name, pin)
  end

  write_manifest(results)
  print_results("vendor sync: ok", results)
else
  abort "unknown mode: #{options.fetch(:mode)}"
end
