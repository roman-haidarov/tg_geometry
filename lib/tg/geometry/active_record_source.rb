# frozen_string_literal: true

module TG
  module Geometry
    module ActiveRecordSource
      module_function

      def call(scope, id:, geometry:, batch_size: 1_000)
        entries = []

        each_record(scope, batch_size: batch_size) do |record|
          entries << [read_field(record, id), read_field(record, geometry)]
        end

        entries
      end

      def registry_source(scope, id:, geometry:, batch_size: 1_000)
        proc do
          TG::Geometry::ActiveRecordSource.call(
            scope,
            id: id,
            geometry: geometry,
            batch_size: batch_size
          )
        end
      end

      def each_record(scope, batch_size:)
        if scope.respond_to?(:find_each)
          scope.find_each(batch_size: batch_size) { |record| yield record }
        else
          scope.each { |record| yield record }
        end
      end
      private_class_method :each_record

      def read_field(record, reader)
        case reader
        when Proc
          reader.call(record)
        when Symbol, String
          if record.respond_to?(reader)
            record.public_send(reader)
          elsif record.respond_to?(:[])
            record[reader]
          else
            raise TG::Geometry::ArgumentError, "record does not expose #{reader.inspect}"
          end
        else
          raise TG::Geometry::ArgumentError, "field reader must be Symbol, String, or Proc"
        end
      end
      private_class_method :read_field
    end
  end
end
