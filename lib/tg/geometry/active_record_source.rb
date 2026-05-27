# frozen_string_literal: true

module TG
  module Geometry
    module ActiveRecordSource
      module_function

      def call(scope, id:, geometry:, batch_size: 1_000)
        enumerator(scope, batch_size: batch_size).map do |record|
          [read_field(record, id), read_field(record, geometry)]
        end
      end

      def registry_source(scope, id:, geometry:, batch_size: 1_000)
        -> { TG::Geometry::ActiveRecordSource.call(scope, id: id, geometry: geometry, batch_size: batch_size) }
      end

      class << self
        private

        def enumerator(scope, batch_size:)
          scope.respond_to?(:find_each) ? scope.find_each(batch_size: batch_size) : scope.each
        end

        def read_field(record, reader)
          case reader
          in Proc then reader.call(record)
          in Symbol | String if record.respond_to?(reader) then record.public_send(reader)
          in Symbol | String if record.respond_to?(:[])    then record[reader]
          in Symbol | String
            raise TG::Geometry::ArgumentError, "record does not expose #{reader.inspect}"
          else
            raise TG::Geometry::ArgumentError, "field reader must be Symbol, String, or Proc"
          end
        end
      end
    end
  end
end
