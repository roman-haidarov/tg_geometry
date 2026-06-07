# frozen_string_literal: true

require_relative "_support"
require "objspace"

TGGeometryBench.say_header("distance_memory_accounting")

geom = TG::Geometry.polygon([[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]])
index = TG::Geometry::Index.build([[:zone, geom]], via: :geom, strategy: :rtree)

CASES = [
  ["geom_distance_to_xy", "Geom", geom, -> { geom.distance_to_xy(5, 5) }],
  ["geom_boundary_distance_to_xy", "Geom", geom, -> { geom.boundary_distance_to_xy(5, 5) }],
  ["geom_nearest_point_xy", "Geom", geom, -> { geom.nearest_point_xy(5, 5) }],
  ["geom_distance_to_lnglat_meters", "Geom", geom, -> { geom.distance_to_lnglat_meters(0.01, 0.01) }],
  ["index_within_distance_xy", "Index", index, -> { index.within_distance_xy(5, 5, 10) }],
  ["index_within_distance_lnglat_meters", "Index", index, -> { index.within_distance_lnglat_meters(0.01, 0.01, 2_000) }]
].freeze

ITERATIONS = TGGeometryBench.env_integer("TGEOMETRY_DISTANCE_MEMORY_ITERATIONS", 10_000, min: 1)

CASES.each do |name, receiver_name, receiver, callable|
  TGGeometryBench.gc_start
  before_memsize = ObjectSpace.memsize_of(receiver)
  before_allocated = GC.stat[:total_allocated_objects]

  ITERATIONS.times { callable.call }

  TGGeometryBench.gc_start
  after_memsize = ObjectSpace.memsize_of(receiver)
  after_allocated = GC.stat[:total_allocated_objects]

  TGGeometryBench.report(
    "distance_memory_accounting",
    {
      case: name,
      receiver: receiver_name,
      operations: ITERATIONS,
      before_memsize: before_memsize,
      after_memsize: after_memsize,
      delta_memsize: after_memsize - before_memsize,
      allocated_objects: after_allocated - before_allocated,
      note: "delta B must stay 0; Ruby result allocations are expected for arrays/pairs"
    }
  )
end
