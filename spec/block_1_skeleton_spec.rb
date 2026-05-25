# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Release Core Block 1 skeleton" do
  it "compiles the native extension" do
    expect(File.file?(EXT_SO)).to be(true)
  end

  it "loads through the canonical require path" do
    expect(defined?(TG::Geometry)).to eq("constant")
    expect(TG::Geometry::VERSION).to be_a(String)
  end

  it "defines the required error hierarchy" do
    expect(TG::Geometry::Error).to be < StandardError
    expect(TG::Geometry::ParseError).to be < TG::Geometry::Error
    expect(TG::Geometry::ArgumentError).to be < ::ArgumentError
    expect(TG::Geometry::FrozenIndexError).to be < TG::Geometry::Error
  end

  it "defines required public classes under TG::Geometry" do
    expect(TG::Geometry::Geom).to be_a(Class)
    expect(TG::Geometry::Rect).to be_a(Class)
    expect(TG::Geometry::Index).to be_a(Class)
  end

  it "does not expose forbidden top-level TG API classes or methods" do
    expect(TG).not_to respond_to(:parse)
    expect(TG.const_defined?(:Geom, false)).to be(false)
    expect(TG.const_defined?(:Rect, false)).to be(false)
    expect(TG.const_defined?(:Index, false)).to be(false)
    expect(TG.const_defined?(:Error, false)).to be(false)
    expect(TG.const_defined?(:ParseError, false)).to be(false)
  end

  it "disables manual allocation for Geom and Index" do
    expect { TG::Geometry::Geom.allocate }.to raise_error(TypeError)
    expect { TG::Geometry::Index.allocate }.to raise_error(TypeError)
  end

  it "keeps Rect constructible with explicit coordinates" do
    expect(TG::Geometry::Rect.new(0, 0, 0, 0)).to be_a(TG::Geometry::Rect)
  end
end
