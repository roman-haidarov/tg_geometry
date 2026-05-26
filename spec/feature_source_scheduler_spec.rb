# frozen_string_literal: true

require "spec_helper"
require "tempfile"

begin
  require "async"
rescue LoadError
  Async = nil
end

RSpec.describe "FeatureSource scheduler behavior" do
  def polygon_feature_json(id, x, y)
    <<~JSON.chomp
      {"type":"Feature","properties":{"@id":"zone/#{id}"},"geometry":{"type":"Polygon","coordinates":[[[#{x},#{y}],[#{x + 0.8},#{y}],[#{x + 0.8},#{y + 0.8}],[#{x},#{y + 0.8}],[#{x},#{y}]]]}}
    JSON
  end

  def write_large_feature_collection(count)
    file = Tempfile.new(["tg_geometry_feature_source_scheduler", ".geojson"])
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

  it "lets other fibers run while a large FeatureSource file is processed" do
    skip "async gem is not available" if Async.nil?

    count = 40_000
    file = write_large_feature_collection(count)
    ticks = 0
    done = false
    index = nil

    Async do |task|
      build_task = task.async do
        TG::Geometry::FeatureSource.build_index_file(file.path, strategy: :flat)
      ensure
        done = true
      end

      ticker = task.async do
        until done
          # puts "Tick #{ticks}"
          ticks += 1
          sleep 0.02
        end
      end

      index = build_task.wait
      ticker.stop
    end

    expect(index.size).to eq(count)
    expect(ticks).to be >= 3
  ensure
    file&.unlink
  end
end
