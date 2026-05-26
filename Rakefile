# frozen_string_literal: true

require "bundler/gem_tasks"
require "fileutils"
require "rbconfig"
require "rake/clean"

EXT_NAME = "tg_geometry_ext_geometry_ext"
EXT_DIR = File.expand_path("ext/tg_geometry", __dir__)
LIB_DIR = File.expand_path("lib", __dir__)
DLEXT = RbConfig::CONFIG.fetch("DLEXT")
PLATFORM = RbConfig::CONFIG.fetch("arch")
BUILD_RUBY_VERSION = RUBY_VERSION
BUILD_DIR = File.expand_path(File.join("tmp", PLATFORM, EXT_NAME, BUILD_RUBY_VERSION), __dir__)
EXT_SO = File.join(EXT_DIR, "#{EXT_NAME}.#{DLEXT}")
LIB_SO = File.join(LIB_DIR, "#{EXT_NAME}.#{DLEXT}")

EXT_BUILD_GLOBS = [
  File.join(EXT_DIR, "Makefile"),
  File.join(EXT_DIR, "mkmf.log"),
  File.join(EXT_DIR, "*.o"),
  File.join(EXT_DIR, "*.so"),
  File.join(EXT_DIR, "*.bundle"),
  File.join(EXT_DIR, "*.dll"),
  File.join(EXT_DIR, "*.dylib")
].freeze

CLEAN.include(
  "tmp",
  "lib/*.so",
  "lib/*.bundle",
  "lib/*.dll",
  "lib/*.dylib",
  *EXT_BUILD_GLOBS
)

# Keep the source tree clean like pq_crypto: ext/tg_geometry contains the checked-in
# extension sources and ext/tg_geometry/vendor contains the checked-in vendored C
# sources, while Makefile/mkmf.log/object/shared-library build outputs live in tmp/.
def remove_in_place_extension_artifacts!
  EXT_BUILD_GLOBS.each { |pattern| FileUtils.rm_f(Dir[pattern]) }
end

def build_extension!(debug: false)
  remove_in_place_extension_artifacts!
  FileUtils.rm_rf(BUILD_DIR)
  FileUtils.mkdir_p(BUILD_DIR)

  env = debug ? { "TG_DEBUG_TEST" => "1" } : {}
  Dir.chdir(BUILD_DIR) do
    sh(env, RbConfig.ruby, File.join(EXT_DIR, "extconf.rb"))
    sh "make"
  end

  FileUtils.mkdir_p(LIB_DIR)
  FileUtils.cp(File.join(BUILD_DIR, "#{EXT_NAME}.#{DLEXT}"), LIB_SO)
end

desc "Compile the native extension into tmp/ and copy it to lib/"
task "compile" do
  build_extension!(debug: false)
end

desc "Compile the native extension with test-only debug hooks"
task "compile:test" do
  build_extension!(debug: true)
end

begin
  require "rspec/core/rake_task"

  RSpec::Core::RakeTask.new(:spec => "compile:test")

  task :gc_stress_env do
    ENV["RUBY_GC_STRESS"] = "1"
  end

  namespace :spec do
    RSpec::Core::RakeTask.new(:gc_stress => ["compile:test", "gc_stress_env"]) do |task|
      task.rspec_opts = ["--tag", "~skip"]
      task.verbose = true
    end

    RSpec::Core::RakeTask.new(:gc_compact => "compile:test") do |task|
      task.pattern = "spec/geom_parse_spec.rb spec/index_build_spec.rb spec/index_borrowed_geometry_spec.rb spec/memory_gc_spec.rb"
    end
  end
rescue LoadError
  task :spec do
    abort "RSpec is required to run the test suite. Install development dependencies with bundle install."
  end

  namespace :spec do
    task :gc_stress => :spec
    task :gc_compact => :spec
  end
end

namespace :benchmark do
  Dir[File.join(__dir__, "benchmark", "*.rb")].sort.each do |path|
    next if File.basename(path).start_with?("_")

    name = File.basename(path, ".rb")
    desc "Run benchmark/#{name}.rb"
    task name => :compile do
      ruby path
    end
  end
end

namespace :vendor do
  desc "Sync vendored C sources to pinned commits"
  task :sync do
    ruby "script/vendor_libs.rb", "--sync"
  end

  desc "Verify vendored C sources against pinned tree SHA256"
  task :verify do
    ruby "script/vendor_libs.rb", "--verify"
  end
end

desc "Alias for vendor:sync"
task vendor: "vendor:sync"

desc "Vendor sources, compile, run specs"
task full_build: ["vendor:verify", "compile", "spec"]

task default: :spec
