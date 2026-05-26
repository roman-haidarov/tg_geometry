# frozen_string_literal: true

require "mkmf"

MINIMUM_RUBY_VERSION = Gem::Version.new("3.0.0")
current_ruby_version = Gem::Version.new(RUBY_VERSION)

if current_ruby_version < MINIMUM_RUBY_VERSION
  abort "tg_geometry requires Ruby >= #{MINIMUM_RUBY_VERSION}; current Ruby is #{RUBY_VERSION}"
end

VENDOR_DIR = File.expand_path("vendor", __dir__)
VENDORED_TG_DIR = File.join(VENDOR_DIR, "tg")
VENDORED_RTREE_DIR = File.join(VENDOR_DIR, "rtree")
VENDORED_JSON_DIR = File.join(VENDOR_DIR, "json")

required_vendor_files = [
  File.join(VENDORED_TG_DIR, "tg.c"),
  File.join(VENDORED_TG_DIR, "tg.h"),
  File.join(VENDORED_TG_DIR, "VERSION"),
  File.join(VENDORED_RTREE_DIR, "rtree.c"),
  File.join(VENDORED_RTREE_DIR, "rtree.h"),
  File.join(VENDORED_RTREE_DIR, "VERSION"),
  File.join(VENDORED_JSON_DIR, "json.c"),
  File.join(VENDORED_JSON_DIR, "json.h"),
  File.join(VENDORED_JSON_DIR, "LICENSE"),
  File.join(VENDORED_JSON_DIR, "VERSION")
]

missing_vendor_files = required_vendor_files.reject { |path| File.file?(path) }
unless missing_vendor_files.empty?
  abort "tg_geometry requires vendored tidwall/tg and rtree.c sources. Missing: #{missing_vendor_files.join(", ")}. Run `ruby script/vendor_libs.rb --sync`."
end

def tg_geometry_clang_compiler?
  cc = RbConfig::CONFIG["CC"].to_s
  return true if cc.include?("clang")

  # Some build environments use cc as an alias. Detect __clang__ by compiling
  # a tiny program rather than trusting the command name.
  try_compile(<<~C)
    #ifndef __clang__
    #error not clang
    #endif
    int main(void) { return 0; }
  C
end

def tg_geometry_sanitize_warnflags!
  return if tg_geometry_clang_compiler?

  # Ruby builds produced with clang may leak clang-only warning flags into
  # RbConfig::CONFIG["warnflags"]. GCC prints noisy "unrecognized command-line
  # option" notes once any real diagnostic is emitted. Keep our Linux CI logs
  # focused on tg_geometry diagnostics.
  clang_only_warning_flags = %w[
    -Wno-constant-logical-operand
    -Wno-parentheses-equality
    -Wno-self-assign
  ]

  $warnflags = $warnflags.to_s.split.reject do |flag|
    clang_only_warning_flags.include?(flag)
  end.join(" ")
end

tg_geometry_sanitize_warnflags!

forbidden_defines = %w[TG_NOATOMICS RTREE_NOATOMICS]
forbidden_defines.each do |macro|
  if ($CFLAGS.to_s.split + $CPPFLAGS.to_s.split + $defs).any? { |flag| flag.include?(macro) }
    abort "tg_geometry must not define #{macro}; atomics are required by the build requirements"
  end
end

$CFLAGS = [
  $CFLAGS,
  "-std=c11",
  "-Wall",
  "-Wextra",
  "-Wpedantic",
  "-O2"
].join(" ")

$INCFLAGS = [
  $INCFLAGS,
  # Ruby 3.x headers intentionally use newer C attributes/macros. Treat the
  # Ruby include directories as system headers so -Wpedantic reports warnings
  # from tg_geometry itself, not from the embedding Ruby version.
  "-isystem $(arch_hdrdir)",
  "-isystem $(hdrdir)/ruby/backward",
  "-isystem $(hdrdir)",
  "-I$(srcdir)/vendor/tg",
  "-I$(srcdir)/vendor/rtree",
  "-I$(srcdir)/vendor/json"
].join(" ")

unless try_compile(<<~C)
  #include <stdatomic.h>
  int main(void) { atomic_int x; atomic_init(&x, 0); return atomic_load(&x); }
C
  abort "tg_geometry requires a C11 compiler with <stdatomic.h> support"
end

required_functions = %w[
  rb_gc_mark_movable
  rb_gc_location
  rb_gc_adjust_memory_usage
]

required_functions.each do |function|
  next if have_func(function, "ruby.h")

  abort "tg_geometry requires Ruby C API function #{function}; minimum supported Ruby is #{MINIMUM_RUBY_VERSION}"
end

if have_macro("RB_NOGVL_OFFLOAD_SAFE", "ruby/thread.h")
  $defs << "-DHAVE_RB_NOGVL_OFFLOAD_SAFE"
end


if ENV["TG_DEBUG_TEST"] == "1"
  $defs << "-DTG_DEBUG_TEST"
end

$srcs = [
  "tg_geometry_ext.c",
  "tg_geometry_vendor_tg.c",
  "tg_geometry_vendor_rtree.c",
  "tg_geometry_vendor_json.c"
]

create_makefile("tg_geometry_ext_geometry_ext")
