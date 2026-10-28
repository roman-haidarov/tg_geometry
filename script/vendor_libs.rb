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
    commit: "532fc38e1a7bd440339cf83259a9d7983f04ad69",
    sha256: "dbf9a49d5ff326974312e94fb39c5687260a2c2b27b10971b24635ed6f79184c",
    target: "tg",
    files: %w[tg.c tg.h LICENSE README.md]
  },
  rtree: {
    repo: "https://github.com/tidwall/rtree.c.git",
    ref: "v0.6.1",
    commit: "eebc774929f89fd5f4095f7c8835a87f2d2fb73f",
    sha256: "6779f4b2c9df8f199907d26c3fba11bc07c6f923d80881577c19f50119e15177",
    target: "rtree",
    files: %w[rtree.c rtree.h LICENSE README.md]
  },
  json: {
    repo: "https://github.com/tidwall/json.c.git",
    ref: "main",
    commit: "104d8cbf5d09ceba37d469574cbf5696349bcaf1",
    sha256: "764155b839b910f45aaaf0d0881a47570d4ef0919465c5117c7658ab3dac0594",
    target: "json",
    files: %w[json.c json.h LICENSE]
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
      if %w[.c .h].include?(File.extname(path))
        source = File.binread(path)
        File.binwrite(path, source.gsub(/[ \t]+(?=\r?$)/, ""))
      end
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

def expected_vendor_results(manifest = nil)
  manifest ||= parse_kv_file(MANIFEST_PATH)

  PINS.each_with_object({}) do |(name, pin), results|
    manifest_sha = manifest["#{name}_tree_sha256"]
    tree_sha = pin.fetch(:sha256) || manifest_sha
    results[name] = pin.merge(tree_sha256: tree_sha)
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
  manifest = parse_kv_file(MANIFEST_PATH)

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
    expected_sha256 = pin.fetch(:sha256) || manifest["#{name}_tree_sha256"]
    if expected_sha256.nil? || expected_sha256.empty?
      failures << "#{name}: tree_sha256 missing from manifest"
    elsif actual_sha256 != expected_sha256
      failures << "#{name}: tree_sha256 mismatch: expected #{expected_sha256}, got #{actual_sha256}"
    end
  end

  expected_results = expected_vendor_results(manifest)
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
