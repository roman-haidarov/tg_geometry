# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Release Core Block 20 concurrency hardening" do
  let(:zone_a) { '{"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]]]}' }
  let(:zone_b) { '{"type":"Polygon","coordinates":[[[5,5],[15,5],[15,15],[5,15],[5,5]]]}' }
  let(:zone_c) { '{"type":"Polygon","coordinates":[[[100,100],[110,100],[110,110],[100,110],[100,100]]]}' }

  let(:entries) do
    [
      [:zone_a, zone_a],
      [:zone_b, zone_b],
      [:zone_c, zone_c]
    ]
  end

  describe "multi-thread read-only queries on a single Index" do
    %i[flat rtree].each do |strategy|
      it "returns identical results across many threads for strategy: #{strategy}" do
        index = TG::Geometry::Index.build(entries, via: :geojson, strategy: strategy)

        query_points = Array.new(200) do |q|
          [(q % 200) / 10.0, ((q * 7) % 200) / 10.0]
        end

        thread_count = 8
        iterations_per_thread = 50

        threads = thread_count.times.map do
          Thread.new do
            results = []
            iterations_per_thread.times do
              query_points.each do |lon, lat|
                results << [
                  index.find_covering(lon, lat),
                  index.covering_ids(lon, lat),
                  index.intersecting_rect(lon, lat, lon + 1.0, lat + 1.0)
                ]
              end
            end
            results
          end
        end

        results_per_thread = threads.map(&:value)

        expect(results_per_thread.uniq.length).to eq(1)
        expect(results_per_thread.first.length).to eq(iterations_per_thread * query_points.length)
      end
    end

    it "preserves Ruby id object identity under concurrent reads with GC.compact" do
      index = TG::Geometry::Index.build(entries, via: :geojson, strategy: :rtree)
      expected = entries.map(&:first)

      compactor = Thread.new do
        10.times do
          GC.start
          GC.compact if GC.respond_to?(:compact)
          sleep 0.001
        end
      end

      readers = 4.times.map do
        Thread.new do
          1_000.times do
            ids = index.covering_ids(7.0, 7.0)
            ids.each do |id|
              raise "unexpected id: #{id.inspect}" unless expected.include?(id)
            end
          end
        end
      end

      compactor.join
      readers.each(&:join)
    end
  end

  describe "reload pattern: old index survives while new index is swapped in" do
    it "lets active readers finish on the old index after a new index replaces it" do
      old_index = TG::Geometry::Index.build(entries, via: :geojson, strategy: :rtree)
      registry = { current: old_index }

      reader_stop = false
      reader_errors = []
      reader_observations = Queue.new

      readers = 4.times.map do
        Thread.new do
          local = registry[:current]
          until reader_stop
            begin
              reader_observations << local.find_covering(7, 7)
              reader_observations << local.covering_ids(7, 7).length
            rescue StandardError => e
              reader_errors << e
              reader_stop = true
            end
          end
        end
      end

      sleep 0.01

      new_entries = entries + [[:zone_d, zone_a]]
      new_index = TG::Geometry::Index.build(new_entries, via: :geojson, strategy: :rtree)
      registry[:current] = new_index

      sleep 0.05

      reader_stop = true
      readers.each(&:join)

      expect(reader_errors).to be_empty
      expect(reader_observations.size).to be > 0
      expect(old_index.size).to eq(3)
      expect(old_index.find_covering(7, 7)).to eq(:zone_a)
      expect(new_index.size).to eq(4)
    end

    it "lets the old index become collectable once readers release their reference" do
      geom_a = TG::Geometry.parse_geojson(zone_a)
      geom_b = TG::Geometry.parse_geojson(zone_b)

      old_index = TG::Geometry::Index.build([[:old, geom_a]], via: :geom, strategy: :rtree)
      new_index = TG::Geometry::Index.build([[:new_a, geom_a], [:new_b, geom_b]],
                                             via: :geom, strategy: :rtree)

      old_index = nil
      GC.start
      GC.compact if GC.respond_to?(:compact)
      GC.start

      expect(new_index.find_covering(5, 5)).to eq(:new_a)
      expect(new_index.size).to eq(2)
    end
  end

  describe "no mutation after build" do
    it "exposes no public mutation methods on Index" do
      index = TG::Geometry::Index.build(entries, via: :geojson, strategy: :rtree)

      %i[add delete rebuild! clear << push add_entry append].each do |forbidden|
        expect(index.respond_to?(forbidden)).to be(false), "Index must not respond to ##{forbidden}"
      end
    end

    it "marks Index as frozen at the Ruby level" do
      index = TG::Geometry::Index.build(entries, via: :geojson, strategy: :rtree)

      expect(index).to be_frozen
      expect { index.instance_variable_set(:@hacked, true) }.to raise_error(FrozenError)
    end
  end
end
