# frozen_string_literal: true

module TG
  module Geometry
    class Registry
      DEFAULT_INDEX_OPTIONS = {
        via: :geojson,
        strategy: :rtree,
        predicate: :covers,
        geometry_index: :ystripes
      }.freeze

      class << self
        def source(&definition)
          if definition
            @source = definition
          elsif instance_variable_defined?(:@source)
            @source
          elsif superclass.respond_to?(:source)
            superclass.source
          end
        end

        def index_options(**options)
          if options.empty?
            inherited = superclass.respond_to?(:index_options) ? superclass.index_options : DEFAULT_INDEX_OPTIONS
            inherited.merge(@index_options || {}).freeze
          else
            @index_options = index_options.merge(options).freeze
          end
        end

        def active_record_source(scope, id:, geometry:, batch_size: 1_000)
          require_relative "active_record_source"

          source do
            TG::Geometry::ActiveRecordSource.call(
              scope,
              id: id,
              geometry: geometry,
              batch_size: batch_size
            )
          end
        end
      end

      attr_reader :index_options

      def initialize(entries: nil, source: nil, **index_options)
        @entries = entries
        @source = source
        @index_options = self.class.index_options.merge(index_options).freeze
        @reload_mutex = Mutex.new
        @index = nil
      end

      def reload!
        new_index = TG::Geometry::Index.build(resolve_entries, **@index_options)

        @reload_mutex.synchronize do
          @index = new_index
        end

        new_index
      end

      def index
        @index || raise(TG::Geometry::Error, "registry index is not loaded; call reload! first")
      end

      def loaded?
        !@index.nil?
      end

      def size
        current_index.size
      end

      def bbox
        current_index.bbox
      end

      def find_covering(lon, lat)
        current_index.find_covering(lon, lat)
      end

      def covering_ids(lon, lat)
        current_index.covering_ids(lon, lat)
      end

      def intersecting_rect(min_x, min_y, max_x, max_y)
        current_index.intersecting_rect(min_x, min_y, max_x, max_y)
      end

      def covering_ids_batch_packed(binary_string)
        current_index.covering_ids_batch_packed(binary_string)
      end

      private

      def current_index
        @index || raise(TG::Geometry::Error, "registry index is not loaded; call reload! first")
      end

      def resolve_entries
        return @entries unless @entries.nil?

        callable = @source || self.class.source
        raise TG::Geometry::Error, "registry source is not configured" unless callable

        if callable.respond_to?(:call)
          instance_exec(&callable)
        else
          raise TG::Geometry::Error, "registry source must be callable"
        end
      end
    end
  end
end
