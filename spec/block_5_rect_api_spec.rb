# frozen_string_literal: true

require "spec_helper"

RSpec.describe "Release Core Block 5 Rect API" do
  it "constructs an immutable rect with coordinate accessors" do
    rect = TG::Geometry::Rect.new(1, 2, 5, 8)

    expect(rect).to be_a(TG::Geometry::Rect)
    expect(rect).to be_frozen
    expect([rect.min_x, rect.min_y, rect.max_x, rect.max_y]).to eq([1.0, 2.0, 5.0, 8.0])
  end

  it "requires explicit constructor coordinates" do
    expect { TG::Geometry::Rect.new }.to raise_error(ArgumentError)
    expect { TG::Geometry::Rect.new(0, 0, 1) }.to raise_error(ArgumentError)
  end

  it "rejects invalid coordinate order" do
    expect { TG::Geometry::Rect.new(5, 0, 1, 1) }.to raise_error(TG::Geometry::ArgumentError)
    expect { TG::Geometry::Rect.new(0, 5, 1, 1) }.to raise_error(TG::Geometry::ArgumentError)
  end

  it "rejects non-finite coordinates" do
    expect { TG::Geometry::Rect.new(Float::NAN, 0, 1, 1) }.to raise_error(TG::Geometry::ArgumentError)
    expect { TG::Geometry::Rect.new(0, 0, Float::INFINITY, 1) }.to raise_error(TG::Geometry::ArgumentError)
  end

  it "returns center as two floats" do
    expect(TG::Geometry::Rect.new(0, 2, 10, 8).center).to eq([5.0, 5.0])
  end

  it "checks rectangle intersection inclusively at boundaries" do
    rect = TG::Geometry::Rect.new(0, 0, 10, 10)

    expect(rect.intersects?(TG::Geometry::Rect.new(5, 5, 12, 12))).to be(true)
    expect(rect.intersects?(TG::Geometry::Rect.new(10, 10, 20, 20))).to be(true)
    expect(rect.intersects?(TG::Geometry::Rect.new(11, 11, 20, 20))).to be(false)
  end

  it "requires Rect argument for intersects? and expand_to_include" do
    rect = TG::Geometry::Rect.new(0, 0, 10, 10)

    expect { rect.intersects?(Object.new) }.to raise_error(TypeError)
    expect { rect.expand_to_include("not a rect") }.to raise_error(TypeError)
  end

  it "checks point containment inclusively at boundaries" do
    rect = TG::Geometry::Rect.new(0, 0, 10, 10)

    expect(rect.contains_point?(5, 5)).to be(true)
    expect(rect.contains_point?(0, 10)).to be(true)
    expect(rect.contains_point?(11, 5)).to be(false)
  end

  it "rejects non-finite contains_point? inputs" do
    rect = TG::Geometry::Rect.new(0, 0, 10, 10)

    expect { rect.contains_point?(Float::NAN, 5) }.to raise_error(TG::Geometry::ArgumentError)
    expect { rect.contains_point?(5, -Float::INFINITY) }.to raise_error(TG::Geometry::ArgumentError)
  end

  it "expands to include another rect and returns a new frozen rect" do
    rect = TG::Geometry::Rect.new(0, 0, 10, 10)
    expanded = rect.expand_to_include(TG::Geometry::Rect.new(-2, 3, 12, 8))

    expect(expanded).to be_a(TG::Geometry::Rect)
    expect(expanded).to be_frozen
    expect(expanded).not_to equal(rect)
    expect([expanded.min_x, expanded.min_y, expanded.max_x, expanded.max_y]).to eq([-2.0, 0.0, 12.0, 10.0])
    expect([rect.min_x, rect.min_y, rect.max_x, rect.max_y]).to eq([0.0, 0.0, 10.0, 10.0])
  end

  it "expands to include a point and returns a new frozen rect" do
    rect = TG::Geometry::Rect.new(0, 0, 10, 10)
    expanded = rect.expand_to_include_point(-3, 12)

    expect(expanded).to be_a(TG::Geometry::Rect)
    expect(expanded).to be_frozen
    expect(expanded).not_to equal(rect)
    expect([expanded.min_x, expanded.min_y, expanded.max_x, expanded.max_y]).to eq([-3.0, 0.0, 10.0, 12.0])
  end

  it "rejects non-finite expand_to_include_point inputs" do
    rect = TG::Geometry::Rect.new(0, 0, 10, 10)

    expect { rect.expand_to_include_point(Float::NAN, 5) }.to raise_error(TG::Geometry::ArgumentError)
    expect { rect.expand_to_include_point(5, Float::INFINITY) }.to raise_error(TG::Geometry::ArgumentError)
  end

  it "does not expose ambiguous contains? method" do
    rect = TG::Geometry::Rect.new(0, 0, 10, 10)

    expect(rect).not_to respond_to(:contains?)
  end
end
