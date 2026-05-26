# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Index build" do
  let(:geom) { TG::Geometry.parse_wkt("POINT (1 2)") }

  it "builds an empty immutable index" do
    index = TG::Geometry::Index.build([], via: :geojson, strategy: :flat)

    expect(index).to be_a(TG::Geometry::Index)
    expect(index).to be_frozen
    expect(index.size).to eq(0)
    expect(index.strategy).to eq(:flat)
    expect(index.predicate).to eq(:covers)
    expect(index.bbox).to be_nil
  end

  it "requires entries to be an Array" do
    expect { TG::Geometry::Index.build("bad", via: :geojson, strategy: :flat) }
      .to raise_error(TypeError)
  end

  it "requires each entry to be a two-element Array" do
    expect { TG::Geometry::Index.build([:bad], via: :geojson, strategy: :flat) }
      .to raise_error(TypeError)

    expect { TG::Geometry::Index.build([[1, "{}", :extra]], via: :geojson, strategy: :flat) }
      .to raise_error(TG::Geometry::ArgumentError)
  end

  it "rejects nil ids" do
    expect { TG::Geometry::Index.build([[nil, "{}"]], via: :geojson, strategy: :flat) }
      .to raise_error(TG::Geometry::ArgumentError, /id cannot be nil/)
  end

  it "allows false ids and duplicate ids" do
    index = TG::Geometry::Index.build([[false, geom], [:same, geom], [:same, geom]],
                                      via: :geom,
                                      strategy: :flat)

    expect(index.size).to eq(3)
    expect(index.bbox).to be_a(TG::Geometry::Rect)
  end

  it "validates via, strategy, predicate, and geometry_index symbols" do
    expect { TG::Geometry::Index.build([], via: :shape, strategy: :flat) }
      .to raise_error(TG::Geometry::ArgumentError)
    expect { TG::Geometry::Index.build([], via: :geojson, strategy: :bogus) }
      .to raise_error(TG::Geometry::ArgumentError)
    expect { TG::Geometry::Index.build([], via: :geojson, strategy: :flat, predicate: :touches) }
      .to raise_error(TG::Geometry::ArgumentError)
    expect { TG::Geometry::Index.build([], via: :geojson, strategy: :flat, geometry_index: :packed) }
      .to raise_error(TG::Geometry::ArgumentError)
  end

  it "requires String values for owned geometry modes in the owned geometry build path" do
    expect { TG::Geometry::Index.build([[1, Object.new]], via: :geojson, strategy: :flat) }
      .to raise_error(TypeError)
    expect { TG::Geometry::Index.build([[1, Object.new]], via: :wkb, strategy: :flat) }
      .to raise_error(TypeError)
  end

  it "requires Geom values for via: :geom" do
    expect { TG::Geometry::Index.build([[1, "not geom"]], via: :geom, strategy: :flat) }
      .to raise_error(TypeError)
  end

  it "cleans up immediately after failed build validation" do
    entries = [[1, geom], [nil, geom], [2, geom]]

    expect { TG::Geometry::Index.build(entries, via: :geom, strategy: :flat) }
      .to raise_error(TG::Geometry::ArgumentError)

    GC.start
    GC.compact if GC.respond_to?(:compact)
  end

  it "survives GC.stress build/free lifecycle" do
    old_stress = GC.stress
    GC.stress = true

    10.times do
      index = TG::Geometry::Index.build([[false, geom], [:dup, geom], [:dup, geom]],
                                        via: :geom,
                                        strategy: :rtree,
                                        predicate: :contains,
                                        geometry_index: :ystripes)
      expect(index).to be_frozen
      expect(index.size).to eq(3)
      expect(index.strategy).to eq(:rtree)
      expect(index.predicate).to eq(:contains)
    end
  ensure
    GC.stress = old_stress
  end

  it "survives GC.compact build/free lifecycle" do
    index = TG::Geometry::Index.build([["id", geom]], via: :geom, strategy: :flat)

    GC.start
    GC.compact if GC.respond_to?(:compact)

    expect(index.size).to eq(1)
    expect(index.bbox).to be_a(TG::Geometry::Rect)
  end

  it "keeps public allocate disabled" do
    expect { TG::Geometry::Index.allocate }.to raise_error(TypeError)
  end
end
