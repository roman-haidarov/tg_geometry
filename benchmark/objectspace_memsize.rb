# frozen_string_literal: true

require_relative "_support"
require "objspace"

TGGeometryBench.say_header("objectspace_memsize")

%i[compact long_thin overlapping].each do |kind|
  TGGeometryBench.sizes.each do |size|
    entries = TGGeometryBench.entries_for(kind, size)
    flat = TGGeometryBench.build_index(entries, strategy: :flat)
    rtree = TGGeometryBench.build_index(entries, strategy: :rtree)
    geom = TG::Geometry.parse_geojson(entries.first.last)

    puts "kind=#{kind} n=#{size} geom_memsize=#{ObjectSpace.memsize_of(geom)} flat_memsize=#{ObjectSpace.memsize_of(flat)} rtree_memsize=#{ObjectSpace.memsize_of(rtree)}"
  end
end
