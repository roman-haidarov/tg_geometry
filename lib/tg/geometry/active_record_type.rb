# frozen_string_literal: true

require "active_model"
require "tg/geometry"

module TG
  module Geometry
    class ActiveRecordType < ActiveModel::Type::Value
      HEX_PATTERN = /\A[0-9A-Fa-f]+\z/

      def initialize(strict: true)
        super()
        @strict = strict
      end

      def type = :tg_geometry

      def deserialize(value)
        case value
        when nil                then nil
        when TG::Geometry::Geom then value
        when String             then parse_string(value)
        else
          @strict ? raise(TG::Geometry::ArgumentError, "cannot deserialize #{value.class} as TG::Geometry::Geom") : nil
        end
      end

      alias cast deserialize

      def serialize(value)
        case value
        when nil, String then value
        else raise TG::Geometry::ArgumentError, "TG::Geometry::ActiveRecordType is read-only in v0.3.0"
        end
      end

      private

      def parse_string(str)
        stripped = str.strip

        if stripped.start_with?("\\x") && stripped[2..].match?(HEX_PATTERN)
          TG::Geometry.parse_hex(stripped[2..])
        elsif stripped.match?(HEX_PATTERN)
          TG::Geometry.parse_hex(stripped)
        elsif str.encoding == Encoding::ASCII_8BIT
          TG::Geometry.parse_wkb(str)
        elsif stripped.start_with?("{")
          TG::Geometry.parse_geojson(str)
        else
          TG::Geometry.parse_wkt(str)
        end
      rescue TG::Geometry::ParseError => e
        raise if @strict

        warn "TG::Geometry::ActiveRecordType: parse failed: #{e.message}"
        nil
      end
    end
  end
end
