# frozen_string_literal: true

require "spec_helper"

RSpec.describe "point to geometry distance" do
  R = 6_371_008.8
  DEG = Math::PI / 180.0

  def meters_per_lng_at(lat)
    R * DEG * Math.cos(lat * DEG)
  end

  def meters_per_lat
    R * DEG
  end

  def point_to_segment_distance(px, py, ax, ay, bx, by)
    dx = bx - ax
    dy = by - ay
    len_sq = dx * dx + dy * dy
    return Math.hypot(px - ax, py - ay) if len_sq.zero?

    t = (((px - ax) * dx) + ((py - ay) * dy)) / len_sq
    t = 0.0 if t < 0.0
    t = 1.0 if t > 1.0
    Math.hypot(px - (ax + t * dx), py - (ay + t * dy))
  end

  def lnglat_segment_distance_m(query_lng, query_lat, a, b)
    sx = meters_per_lng_at(query_lat)
    sy = meters_per_lat
    ax = sx * (a[0] - query_lng)
    ay = sy * (a[1] - query_lat)
    bx = sx * (b[0] - query_lng)
    by = sy * (b[1] - query_lat)
    point_to_segment_distance(0.0, 0.0, ax, ay, bx, by)
  end

  it "implements polygon inside, boundary, outside, and hole semantics" do
    polygon = TG::Geometry.polygon(
      [[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]],
      holes: [[[2, 2], [4, 2], [4, 4], [2, 4], [2, 2]]]
    )

    expect(polygon.distance_to_xy(5, 5)).to eq(0.0)
    expect(polygon.boundary_distance_to_xy(5, 5)).to be_within(1e-12).of(Math.sqrt(2.0))
    expect(polygon.distance_to_xy(0, 5)).to eq(0.0)
    expect(polygon.boundary_distance_to_xy(0, 5)).to eq(0.0)
    expect(polygon.distance_to_xy(12, 5)).to be_within(1e-12).of(2.0)
    expect(polygon.distance_to_xy(3, 3)).to be_within(1e-12).of(1.0)
  end

  it "uses approximate local meters for lng/lat outside distances" do
    polygon = TG::Geometry.polygon([[0, 0], [0.01, 0], [0.01, 0.01], [0, 0.01], [0, 0]])
    distance = polygon.distance_to_lnglat_meters(0.02, 0.005)
    expected = meters_per_lng_at(0.005) * 0.01

    expect(distance).to be_within(0.01).of(expected)
    expect(polygon.distance_to_lnglat_meters(0.005, 0.005)).to eq(0.0)
    expect(polygon.boundary_distance_to_lnglat_meters(0.005, 0.005)).to be > 0.0
  end

  it "uses minimum distance over multipolygon members" do
    multipolygon = TG::Geometry.parse_wkt(
      "MULTIPOLYGON (((0 0, 2 0, 2 2, 0 2, 0 0)), ((10 0, 12 0, 12 2, 10 2, 10 0)))"
    )

    expect(multipolygon.distance_to_xy(1, 1)).to eq(0.0)
    expect(multipolygon.distance_to_xy(6, 1)).to be_within(1e-12).of(4.0)
  end

  it "handles lines, points, multipoints, and mixed geometry collections" do
    line = TG::Geometry.line_string([[0, 0], [10, 0]])
    point = TG::Geometry.point(3, 4)
    multipoint = TG::Geometry.parse_wkt("MULTIPOINT ((0 0), (10 0))")
    collection = TG::Geometry.parse_wkt(
      "GEOMETRYCOLLECTION (POLYGON ((20 20, 30 20, 30 30, 20 30, 20 20)), LINESTRING (0 0, 10 0))"
    )

    expect(line.distance_to_xy(5, 3)).to be_within(1e-12).of(line.boundary_distance_to_xy(5, 3))
    expect(line.distance_to_xy(5, 3)).to be_within(1e-12).of(3.0)
    expect(point.distance_to_xy(0, 0)).to be_within(1e-12).of(5.0)
    expect(multipoint.distance_to_xy(7, 0)).to be_within(1e-12).of(3.0)
    expect(collection.distance_to_xy(5, 2)).to be_within(1e-12).of(2.0)
    expect(collection.distance_to_xy(25, 25)).to eq(0.0)
  end

  it "skips empty members but raises when nothing is measurable" do
    mixed = TG::Geometry.parse_wkt("GEOMETRYCOLLECTION (POINT EMPTY, POINT (1 1))")
    all_empty = TG::Geometry.parse_wkt("GEOMETRYCOLLECTION (POINT EMPTY, LINESTRING EMPTY)")

    expect(mixed.distance_to_xy(4, 5)).to be_within(1e-12).of(5.0)
    expect { all_empty.distance_to_xy(0, 0) }.to raise_error(TG::Geometry::ArgumentError)
    expect { TG::Geometry.empty_polygon.boundary_distance_to_xy(0, 0) }.to raise_error(TG::Geometry::ArgumentError)
  end

  it "returns nearest boundary points for interior polygon queries" do
    polygon = TG::Geometry.polygon([[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]])
    nearest = polygon.nearest_point_xy(5, 5)
    boundary_distance = polygon.boundary_distance_to_xy(5, 5)

    expect(polygon.distance_to_xy(5, 5)).to eq(0.0)
    expect(boundary_distance).to be_within(1e-12).of(5.0)
    expect(point_to_segment_distance(5, 5, nearest[0], nearest[1], nearest[0], nearest[1])).to be_within(1e-12).of(boundary_distance)
    expect(nearest[0] == 0.0 || nearest[0] == 10.0 || nearest[1] == 0.0 || nearest[1] == 10.0).to be(true)
  end

  it "keeps nearest lng/lat raw and does not force-wrap returned longitude" do
    line = TG::Geometry.line_string([[181, 0], [181, 1]])
    nearest = line.nearest_point_lnglat(180, 0.5)

    expect(nearest[0]).to be_within(1e-12).of(181.0)
    expect(nearest[1]).to be_within(1e-12).of(0.5)
  end

  it "does not wrap antimeridian proximity" do
    point = TG::Geometry.point(179.9, 0)

    expect(point.distance_to_lnglat_meters(-179.9, 0)).to be > 30_000_000
    expect(point.covers_xy?(-179.9, 0)).to be(false)
  end

  it "matches independent local-meter segment math and stays finite near the pole" do
    line = TG::Geometry.line_string([[0, 0], [0.01, 0]])
    expected = lnglat_segment_distance_m(0.005, 0.01, [0, 0], [0.01, 0])

    expect(line.distance_to_lnglat_meters(0.005, 0.01)).to be_within(0.001).of(expected)
    expect(TG::Geometry.point(10, 90).distance_to_lnglat_meters(0, 90)).to be_finite
    expect(TG::Geometry.point(10, 90).nearest_point_lnglat(0, 90)).to eq([10.0, 90.0])
  end

  it "keeps Geom and Index memsize unchanged after distance calls" do
    require "objspace"

    geom = TG::Geometry.polygon([[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]])
    index = TG::Geometry::Index.build([[:zone, geom]], via: :geom, strategy: :rtree)
    geom_size = ObjectSpace.memsize_of(geom)
    index_size = ObjectSpace.memsize_of(index)

    5.times do
      geom.distance_to_xy(5, 5)
      geom.nearest_point_lnglat(0.01, 0.01)
      index.within_distance_xy(5, 5, 10)
      index.within_distance_lnglat_meters(0.01, 0.01, 2_000)
    end

    expect(ObjectSpace.memsize_of(geom)).to eq(geom_size)
    expect(ObjectSpace.memsize_of(index)).to eq(index_size)
  end

  it "survives GC stress and compaction" do
    geom = TG::Geometry.line_string([[0, 0], [10, 0]])

    GC.stress = true
    expect(geom.distance_to_xy(5, 3)).to be_within(1e-12).of(3.0)
    expect(geom.nearest_point_xy(5, 3)).to eq([5.0, 0.0])
  ensure
    GC.stress = false
    GC.compact if GC.respond_to?(:compact)
  end
end

RSpec.describe "Index distance radius queries" do
  def build_distance_entries
    [
      [:zone, TG::Geometry.polygon([[0, 0], [2, 0], [2, 2], [0, 2], [0, 0]])],
      [:line, TG::Geometry.line_string([[4, 0], [4, 4]])],
      [:point, TG::Geometry.point(8, 0)]
    ]
  end

  it "matches brute force membership for xy and returns filter distances" do
    entries = build_distance_entries
    index = TG::Geometry::Index.build(entries, via: :geom, strategy: :rtree)

    srand(1234)
    30.times do
      x = rand * 10.0 - 1.0
      y = rand * 5.0 - 1.0
      radius = rand * 3.0
      expected = entries.filter_map do |id, geom|
        distance = geom.distance_to_xy(x, y)
        [id, distance] if distance <= radius
      end
      actual = index.within_distance_xy(x, y, radius)

      expect(actual.map(&:first)).to eq(expected.map(&:first))
      actual.each do |id, distance|
        geom = entries.assoc(id)[1]
        expect(distance).to be_within(1e-12).of(geom.distance_to_xy(x, y))
      end
      expect(index.within_distance_ids_xy(x, y, radius)).to eq(expected.map(&:first))
    end
  end

  it "matches brute force membership for lng/lat meters" do
    entries = [
      [:zone, TG::Geometry.polygon([[0, 0], [0.02, 0], [0.02, 0.02], [0, 0.02], [0, 0]])],
      [:line, TG::Geometry.line_string([[0.04, 0], [0.04, 0.04]])],
      [:point, TG::Geometry.point(0.08, 0)]
    ]
    index = TG::Geometry::Index.build(entries, via: :geom, strategy: :rtree)

    srand(5678)
    30.times do
      lng = rand * 0.1 - 0.01
      lat = rand * 0.05 - 0.01
      radius = rand * 3_000.0
      expected = entries.filter_map do |id, geom|
        distance = geom.distance_to_lnglat_meters(lng, lat)
        [id, distance] if distance <= radius
      end
      actual = index.within_distance_lnglat_meters(lng, lat, radius)

      expect(actual.map(&:first)).to eq(expected.map(&:first))
      actual.each do |id, distance|
        geom = entries.assoc(id)[1]
        expect(distance).to be_within(1e-9).of(geom.distance_to_lnglat_meters(lng, lat))
      end
      expect(index.within_distance_ids_lnglat_meters(lng, lat, radius)).to eq(expected.map(&:first))
    end
  end

  it "sorts filtered pairs by distance only when requested" do
    entries = build_distance_entries
    index = TG::Geometry::Index.build(entries, via: :geom, strategy: :rtree)
    sorted = index.within_distance_xy(3, 1, 10, sort: true)

    expect(sorted.map(&:last)).to eq(sorted.map(&:last).sort)
    expect(index.within_distance_xy(3, 1, 10, sort: false).map(&:first)).to eq([:zone, :line, :point])
  end

  it "handles zero radius and near-pole/full-longitude prefilter cases" do
    zone = TG::Geometry.polygon([[0, 0], [1, 0], [1, 1], [0, 1], [0, 0]])
    pole_point = TG::Geometry.point(90, 90)
    index = TG::Geometry::Index.build([[:zone, zone], [:pole, pole_point]], via: :geom, strategy: :rtree)

    expect(index.within_distance_ids_xy(0.5, 0.5, 0)).to eq([:zone])
    expect(index.within_distance_ids_lnglat_meters(0, 90, 1)).to eq([:pole])
  end

  it "rejects invalid arguments and keywords" do
    index = TG::Geometry::Index.build(build_distance_entries, via: :geom, strategy: :rtree)
    geom = TG::Geometry.point(0, 0)

    expect { geom.distance_to_lnglat_meters(Float::NAN, 0) }.to raise_error(TG::Geometry::ArgumentError)
    expect { geom.distance_to_lnglat_meters(0, Float::INFINITY) }.to raise_error(TG::Geometry::ArgumentError)
    expect { geom.distance_to_lnglat_meters(181, 0) }.to raise_error(TG::Geometry::ArgumentError)
    expect { geom.distance_to_lnglat_meters(0, -91) }.to raise_error(TG::Geometry::ArgumentError)
    expect { geom.distance_to_xy(0, Float::NAN) }.to raise_error(TG::Geometry::ArgumentError)
    expect { geom.distance_to_xy(0, 0, metric: :meters) }.to raise_error(TG::Geometry::ArgumentError)
    expect { index.within_distance_xy(0, 0, -1) }.to raise_error(TG::Geometry::ArgumentError)
    expect { index.within_distance_lnglat_meters(0, 0, Float::INFINITY) }.to raise_error(TG::Geometry::ArgumentError)
    expect { index.within_distance_xy(0, 0, 1, bogus: true) }.to raise_error(TG::Geometry::ArgumentError)
    expect { index.within_distance_ids_xy(0, 0, 1, sort: true) }.to raise_error(TG::Geometry::ArgumentError)
    expect { index.within_distance_ids_lnglat_meters(0, 0, 1, sort: true) }.to raise_error(TG::Geometry::ArgumentError)
  end
end
