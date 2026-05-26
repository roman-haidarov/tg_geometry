# frozen_string_literal: true

require "spec_helper"
require "tempfile"

RSpec.describe "FeatureSource no-GVL behavior" do
  def polygon_feature_json(id, x, y)
    <<~JSON.chomp
      {"type":"Feature","properties":{"@id":"zone/#{id}"},"geometry":{"type":"Polygon","coordinates":[[[#{x},#{y}],[#{x + 0.8},#{y}],[#{x + 0.8},#{y + 0.8}],[#{x},#{y + 0.8}],[#{x},#{y}]]]}}
    JSON
  end

  def write_large_feature_collection(count)
    file = Tempfile.new(["tg_geometry_feature_source_nogvl", ".geojson"])
    file.binmode
    file.write('{"type":"FeatureCollection","features":[')
    count.times do |i|
      file.write(",") unless i.zero?
      file.write(polygon_feature_json(i, i % 1000, i / 1000))
    end
    file.write("]}")
    file.flush
    file.close
    file
  end

  it "lets other Ruby threads run during large FeatureSource file processing" do
    count = 40_000
    file = write_large_feature_collection(count)
    ticks = 0
    done = false

    ticker = Thread.new do
      until done
        ticks += 1
        sleep 0.005
      end
    end

    index = TG::Geometry::FeatureSource.build_index_file(file.path, strategy: :flat)
    done = true
    ticker.join

    expect(index.size).to eq(count)
    expect(ticks).to be >= 1
  ensure
    done = true
    ticker&.join
    file&.unlink
  end
end
