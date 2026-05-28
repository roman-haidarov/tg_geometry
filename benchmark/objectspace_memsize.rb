# frozen_string_literal: true

require_relative "_support"
require "objspace"

TGGeometryBench.say_header("objectspace_memsize")

%i[compact long_thin overlapping].each do |kind|
  TGGeometryBench.sizes.each do |size|
    entries = TGGeometryBench.entries_for(kind, size)
    TGGeometryBench.gc_start
    flat = TGGeometryBench.build_index(entries, strategy: :flat)
    TGGeometryBench.gc_start
    rtree = TGGeometryBench.build_index(entries, strategy: :rtree)
    geom = TG::Geometry.parse_geojson(entries.first.last)

    TGGeometryBench.report(
      "objectspace_memsize",
      {
        kind: kind,
        n: size,
        geom_memsize: ObjectSpace.memsize_of(geom),
        flat_memsize: ObjectSpace.memsize_of(flat),
        rtree_memsize: ObjectSpace.memsize_of(rtree),
        rtree_over_flat: ObjectSpace.memsize_of(rtree) - ObjectSpace.memsize_of(flat),
        rss_kb: TGGeometryBench.rss_kb
      }
    )
  end
end
