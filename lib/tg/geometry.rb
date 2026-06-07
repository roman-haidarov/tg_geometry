# frozen_string_literal: true

require_relative "geometry/version"
require "tg_geometry_ext_geometry_ext"
require_relative "geometry/registry"
require_relative "geometry/active_record_source"

module TG
  # Fast immutable planar geometry engine with proper indexes and clean
  # EWKB/PostGIS boundaries. Not a GIS. Not an rgeo replacement.
  #
  # All distances and coordinates are in input units. SRID is metadata only;
  # no reprojection is performed.
  module Geometry
    # @!method self.line_string(points, index: :natural, srid: nil)
    #   @param points [Array<Array<Float>>]
    #   @param index [:default, :none, :natural, :ystripes]
    #   @param srid [Integer, nil]
    #   @return [TG::Geometry::Geom]
    #
    # @!method self.polygon(exterior, holes: [], index: :ystripes, srid: nil)
    #   @param exterior [Array<Array<Float>>]
    #   @param holes [Array<Array<Array<Float>>>]
    #   @param index [:default, :none, :natural, :ystripes]
    #   @param srid [Integer, nil]
    #   @return [TG::Geometry::Geom]
    #
    # @!method self.multi_polygon(polygons, index: :ystripes, srid: nil)
    #   Each polygon is a Hash with :exterior and optional :holes, or an Array
    #   shorthand for an exterior with no holes.
    #   @param polygons [Array<Hash, Array>]
    #   @return [TG::Geometry::Geom]
    #
    # @!method self.parse_wkb(bytes, index: :ystripes)
    #   Parses WKB or EWKB. SRID is preserved when the EWKB SRID flag is set.
    #   @return [TG::Geometry::Geom]
    #
    # @!method self.parse_hex(hex, index: :ystripes)
    #   Parses HEXWKB or HEXEWKB. SRID is preserved when the EWKB SRID flag is set.
    #   @return [TG::Geometry::Geom]

    class Geom
      # @!method srid
      #   @return [Integer, nil] SRID metadata; not used for reprojection
      #
      # @!method distance_to_lnglat_meters(lng, lat)
      #   Approximate meters in a query-local equirectangular frame. Not geodesy.
      #   @return [Float]
      #
      # @!method boundary_distance_to_lnglat_meters(lng, lat)
      #   Approximate meters to nearest boundary/segment/point.
      #   @return [Float]
      #
      # @!method nearest_point_lnglat(lng, lat)
      #   Raw planar nearest boundary/geometry point. Longitude is not wrapped.
      #   @return [Array(Float, Float)]
      #
      # @!method distance_to_xy(x, y)
      #   Planar distance in input coordinate units.
      #   @return [Float]
      #
      # @!method boundary_distance_to_xy(x, y)
      #   Planar boundary distance in input coordinate units.
      #   @return [Float]
      #
      # @!method nearest_point_xy(x, y)
      #   Planar nearest boundary/geometry point in input coordinate units.
      #   @return [Array(Float, Float)]
      #
      # @!method to_ewkb(srid: nil)
      #   Writes EWKB with the SRID flag set. Uses explicit srid: when provided,
      #   otherwise Geom#srid. Raises if no SRID is available. to_wkb remains plain.
      #   @return [String] frozen ASCII-8BIT EWKB string
    end

    class Index
      # @!method self.build(entries, via:, strategy:, predicate: :covers, geometry_index: :ystripes)
      #   predicate: affects only legacy point query methods: find_covering,
      #   covering_ids(x, y), covering_ids_batch_packed. The *_geom_ids methods
      #   use their own predicates derived from the method name.
      #
      # @!method intersecting_geom_ids(geom)
      #   Stored geometries for which tg_geom_intersects(stored, query) is true.
      #   @return [Array<Object>] ids in insertion order
      #
      # @!method covering_geom_ids(geom)
      #   Stored geometries for which tg_geom_covers(stored, query) is true.
      #   Direction: stored covers query. Boundary points are covered.
      #   @return [Array<Object>] ids in insertion order
      #
      # @!method containing_geom_ids(geom)
      #   Stored geometries for which tg_geom_contains(stored, query) is true.
      #   Direction: stored contains query. Boundary points are not contained.
      #   @return [Array<Object>] ids in insertion order
      #
      # @!method within_distance_lnglat_meters(lng, lat, radius_m, sort: false)
      #   Rtree bbox prefilter plus exact approximate-meter distance filter.
      #   @return [Array<Array(Object, Float)>]
      #
      # @!method within_distance_ids_lnglat_meters(lng, lat, radius_m)
      #   Same membership as within_distance_lnglat_meters, ids only.
      #   @return [Array<Object>]
      #
      # @!method within_distance_xy(x, y, radius, sort: false)
      #   Rtree bbox prefilter plus exact planar distance filter.
      #   @return [Array<Array(Object, Float)>]
      #
      # @!method within_distance_ids_xy(x, y, radius)
      #   Same membership as within_distance_xy, ids only.
      #   @return [Array<Object>]
    end

    class Line
      # @!method nearest_segment(x, y)
      #   @return [TG::Geometry::NearestSegment, nil] planar Euclidean
    end

    class Ring
      # @!method nearest_segment(x, y)
      #   @return [TG::Geometry::NearestSegment, nil] planar Euclidean
    end

    # Result of Line#nearest_segment / Ring#nearest_segment.
    class NearestSegment
      # @!attribute [r] segment   @return [TG::Geometry::Segment]
      # @!attribute [r] index     @return [Integer]
      # @!attribute [r] distance  @return [Float] planar Euclidean in input units
      # @!attribute [r] point     @return [Array(Float, Float)] projection
    end
  end
end
