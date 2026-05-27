# frozen_string_literal: true

require "forwardable"

module TG
  module Geometry
    class Registry
      extend Forwardable

      DEFAULT_INDEX_OPTIONS = {
        via: :geojson,
        strategy: :rtree,
        predicate: :covers,
        geometry_index: :ystripes
      }.freeze

      class << self
        def source(&block)
          @source = block if block
          @source || (superclass.source if superclass.respond_to?(:source))
        end

        def index_options(**options)
          inherited = superclass.respond_to?(:index_options) ? superclass.index_options : DEFAULT_INDEX_OPTIONS
          return inherited.merge(@index_options || {}).freeze if options.empty?

          @index_options = inherited.merge(@index_options || {}, options).freeze
        end
      end

      attr_reader :index_options

      def_delegators :index, :size, :bbox, :find_covering, :covering_ids,
                     :intersecting_rect, :covering_ids_batch_packed

      def initialize(entries: nil, source: nil, **index_options)
        @entries = entries
        @source = source
        @index_options = self.class.index_options.merge(index_options).freeze
        @reload_mutex = Mutex.new
        @index = nil
      end

      def reload!
        new_index = TG::Geometry::Index.build(resolve_entries, **@index_options)
        @reload_mutex.synchronize { @index = new_index }
        new_index
      end

      def index
        @index || raise(TG::Geometry::Error, "registry index is not loaded; call reload! first")
      end

      def loaded? = !@index.nil?

      private

      def resolve_entries
        return @entries if @entries

        callable = @source || self.class.source
        raise TG::Geometry::Error, "registry source is not configured" unless callable

        instance_exec(&callable)
      end
    end
  end
end
