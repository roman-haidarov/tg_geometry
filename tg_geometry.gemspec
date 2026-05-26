# frozen_string_literal: true

require_relative "lib/tg/geometry/version"

Gem::Specification.new do |spec|
  spec.name          = "tg_geometry"
  spec.version       = TG::Geometry::VERSION
  spec.authors       = ["Roman Haydarov"]
  spec.email         = ["romnhajdarov@gmail.com"]

  spec.summary       = "Native extension for TG::Geometry parsing, predicates, immutable indexes, FeatureSource imports, registries, low-level wrappers, and packed point batches"
  spec.description   = "Defines TG::Geometry with immutable Geom parsing and constructor wrappers, expanded geometry predicates and accessors, Rect helpers, Hex/GeoBIN writers, raw extra_json access, read-only borrowed Line/Ring/Polygon and GeometryCollection child wrappers, value Segment wrappers, Registry reload sugar, optional ActiveRecord source helpers, and an immutable geofencing-oriented Index with owned and borrowed geometry ingestion, flat/rtree strategies, deterministic ordered id results, exact rtree allocation accounting, and native-endian packed point batch queries, and FeatureSource GeoJSON FeatureCollection extraction/build paths over vendored C sources. Ractor support is not claimed."
  spec.homepage      = "https://github.com/roman-haidarov/tg_geometry"
  spec.license       = "MIT"
  spec.required_ruby_version = ">= 3.1"

  spec.metadata["homepage_uri"]    = spec.homepage
  spec.metadata["source_code_uri"] = "#{spec.homepage}/tree/main"
  spec.metadata["changelog_uri"]   = "#{spec.homepage}/blob/main/CHANGELOG.md"

  vendor_files = Dir[
    "ext/tg_geometry/vendor/.vendored",
    "ext/tg_geometry/vendor/tg/{LICENSE,README.md,VERSION,tg.c,tg.h}",
    "ext/tg_geometry/vendor/rtree/{LICENSE,README.md,VERSION,rtree.c,rtree.h}",
    "ext/tg_geometry/vendor/json/{LICENSE,VERSION,json.c,json.h}"
  ]

  spec.files = (Dir[
    "lib/**/*.rb",
    "ext/tg_geometry/*.{c,h,rb}",
    "spec/**/*.rb",
    "benchmark/**/*.rb",
    "benchmark/**/*.txt",
    "docs/**/*.md",
    "README.md",
    "CHANGELOG.md",
    "LICENSE.txt",
    "Rakefile",
    "Gemfile",
    "script/vendor_libs.rb"
  ] + vendor_files).select { |path| File.file?(path) && !File.symlink?(path) }.uniq

  spec.extensions    = ["ext/tg_geometry/extconf.rb"]
  spec.require_paths = ["lib"]

  spec.add_development_dependency "rake", "~> 13.0"
  spec.add_development_dependency "rspec", "~> 3.13"
end
