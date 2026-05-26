# Casual Example: Rails + ActiveRecord + GeoJSON FeatureSource

This example shows a small Rails integration for point-in-polygon lookups with real GeoJSON data.

The recommended import path uses `TG::Geometry::FeatureSource` so the application does not parse the entire FeatureCollection into a Ruby Hash tree just to extract geometries.

## 1. Store zones

```ruby
class CreateZones < ActiveRecord::Migration[8.1]
  def change
    create_table :zones do |t|
      t.string  :code, null: false
      t.string  :name
      t.string  :source, null: false, default: "osm"
      t.string  :source_uid
      t.string  :kind, null: false, default: "unknown"
      t.text    :geojson, null: false
      t.jsonb   :properties, null: false, default: {}
      t.boolean :active, null: false, default: true
      t.integer :priority, null: false, default: 100
      t.datetime :imported_at
      t.timestamps
    end

    add_index :zones, :code, unique: true
    add_index :zones, [:source, :source_uid], unique: true, where: "source_uid IS NOT NULL"
    add_index :zones, [:active, :priority, :id]
  end
end
```

`geojson` stores only the raw Geometry object (`Polygon` / `MultiPolygon`), not the whole Feature and not the FeatureCollection.

## 2. Import a FeatureCollection

```ruby
class Zone < ApplicationRecord
  OSM_KIND_KEYS = %w[amenity leisure landuse tourism boundary natural building shop].freeze

  KIND_PRIORITY = {
    "hospital" => 10,
    "school" => 20,
    "university" => 20,
    "college" => 20,
    "kindergarten" => 20,
    "park" => 30,
    "marketplace" => 35,
    "commercial" => 40,
    "retail" => 40,
    "industrial" => 45,
    "residential" => 50,
    "forest" => 60,
    "grass" => 70,
    "unknown" => 100
  }.freeze

  validates :code, :source, :kind, :geojson, presence: true
  validates :source_uid, uniqueness: { scope: :source }, allow_nil: true
  validate :geojson_must_be_parseable_polygon

  class << self
    def import_geojson_file!(path, source: "osm", replace: false)
      result = TG::Geometry::FeatureSource.read_features_file(
        path,
        id: ["properties", "@id"],
        only: [:polygon, :multipolygon],
        report: true,
        on_invalid: :skip,
        on_missing_id: :ordinal
      )

      imported = 0

      transaction do
        where(source: source).delete_all if replace

        result[:features].each do |source_uid, geometry_json, properties_json|
          # This is application-level parsing of one feature's properties only.
          # The full FeatureCollection was not parsed through JSON.parse.
          properties = JSON.parse(properties_json || "null") || {}
          kind = kind_from_properties(properties)

          zone = find_or_initialize_by(source: source, source_uid: source_uid.to_s)
          zone.assign_attributes(
            code: code_for(source, source_uid),
            name: name_from_properties(properties, kind, source_uid),
            kind: kind,
            geojson: geometry_json,
            properties: properties,
            active: true,
            priority: priority_for(kind),
            imported_at: Time.current
          )
          zone.save!
          imported += 1
        end
      end

      {
        imported: imported,
        skipped: result[:skipped],
        filtered: result[:filtered],
        errors: result[:errors]
      }
    end

    def kind_from_properties(properties)
      OSM_KIND_KEYS.each do |key|
        value = properties[key]
        return value.to_s if value.present?
      end
      "unknown"
    end

    def priority_for(kind)
      KIND_PRIORITY.fetch(kind.to_s, 100)
    end

    private

    def code_for(source, source_uid)
      "#{source}_#{source_uid}".parameterize(separator: "_")
    end

    def name_from_properties(properties, kind, source_uid)
      properties["name:ru"].presence ||
        properties["name"].presence ||
        properties["name:en"].presence ||
        "#{kind} #{source_uid}"
    end
  end

  private

  def geojson_must_be_parseable_polygon
    geom = TG::Geometry.parse_geojson(geojson)
    errors.add(:geojson, "must be Polygon or MultiPolygon") unless %i[polygon multipolygon].include?(geom.type)
  rescue TG::Geometry::ParseError => e
    errors.add(:geojson, "is invalid: #{e.message}")
  end
end
```

## 3. Import task

```ruby
namespace :zones do
  desc "Import zones from GeoJSON FeatureCollection"
  task import_geojson: :environment do
    path = ENV.fetch("PATH", Rails.root.join("db/geo/almaty_zones.geojson").to_s)
    replace = ActiveModel::Type::Boolean.new.cast(ENV.fetch("REPLACE", "false"))

    result = Zone.import_geojson_file!(
      path,
      source: ENV.fetch("SOURCE", "osm"),
      replace: replace
    )

    puts "Imported: #{result[:imported]}, filtered: #{result[:filtered]}, skipped: #{result[:skipped]}"
    result[:errors].first(10).each { |error| warn error.inspect }
  end
end
```

Run:

```bash
bundle exec rake zones:import_geojson PATH=db/geo/almaty_zones.geojson REPLACE=true
```

## 4. Build an index from database rows

```ruby
entries = Zone
  .where(active: true)
  .order(:priority, :id)
  .pluck(:id, :geojson)

index = TG::Geometry::Index.build(
  entries,
  via: :geojson,
  strategy: :rtree,
  predicate: :covers,
  geometry_index: :ystripes
)
```

`find_covering` returns the first matching id in insertion order, so sort rows in the order you want to use for priority.

## 5. Direct file-to-index path

When you do not need to store features first, build an index directly from a FeatureCollection file:

```ruby
index = TG::Geometry::FeatureSource.build_index_file(
  "db/geo/almaty_zones.geojson",
  id: ["properties", "@id"],
  only: [:polygon, :multipolygon],
  on_missing_id: :ordinal,
  strategy: :rtree,
  predicate: :covers,
  geometry_index: :ystripes
)
```

The direct build path is fail-fast. It does not support report mode or skip mode because it returns a ready `TG::Geometry::Index`, not an import report.

## 6. Query points and rectangles

Coordinates are always `(lon, lat)`.

```ruby
index.find_covering(76.945, 43.238)
index.covering_ids(76.945, 43.238)
index.intersecting_rect(76.90, 43.20, 77.00, 43.30)
```

For batch point checks, pass native-endian packed doubles:

```ruby
points = [[76.945, 43.238], [76.900, 43.250], [80.000, 50.000]]
packed = points.flatten.pack("d*")
index.covering_ids_batch_packed(packed)
```

## Notes

- `FeatureSource.read_features_file` reads the full source into memory, but it does not build a Ruby Hash tree for the whole FeatureCollection.
- `properties_json` is returned as a raw JSON string. Parse it at application level only if you need those attributes.
- `TG::Geometry` uses planar XY coordinates. It does not perform geocoding, routing, projections, or geodesic distance calculations.
