# frozen_string_literal: true

require "rbconfig"

ROOT = File.expand_path("..", __dir__)
LIB_DIR = File.join(ROOT, "lib")
EXT_SO = File.join(LIB_DIR, "tg_geometry_ext_geometry_ext.#{RbConfig::CONFIG.fetch("DLEXT")}")

unless File.exist?(EXT_SO)
  system(RbConfig.ruby, "-S", "rake", "compile:test", chdir: ROOT) or abort "failed to compile native extension"
end

$LOAD_PATH.unshift(LIB_DIR) unless $LOAD_PATH.include?(LIB_DIR)

require "tg/geometry"
