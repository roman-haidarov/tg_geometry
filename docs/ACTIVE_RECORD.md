# ActiveRecord source helper

`TG::Geometry::ActiveRecordSource` is an optional Ruby helper. It does not require Rails or ActiveRecord and is not loaded from a separate integration gem.

It accepts any object that responds to `find_each`, or any Enumerable:

```ruby
entries = TG::Geometry::ActiveRecordSource.call(
  Zone.where(active: true),
  id: :id,
  geometry: :geojson,
  batch_size: 1_000
)

index = TG::Geometry::Index.build(entries, via: :geojson, strategy: :rtree)
```

It can also feed a Registry:

```ruby
class DeliveryZones < TG::Geometry::Registry
  active_record_source Zone.where(active: true), id: :id, geometry: :geojson
end
```

Field readers may be Symbols, Strings, or Procs. The helper only converts application records into `[[id, object], ...]`; it does not mutate records, keep database connections, create background jobs, install reload hooks, or add a Rails dependency to the native extension.
