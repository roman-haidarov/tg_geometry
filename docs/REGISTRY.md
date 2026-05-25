# Registry

`TG::Geometry::Registry` is Ruby-side application sugar over immutable `TG::Geometry::Index`.

It does not add a mutable native index. `reload!` always builds a new Index and then swaps a Ruby reference:

```ruby
class DeliveryZones < TG::Geometry::Registry
  source do
    [
      [:zone_a, '{"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]]]}']
    ]
  end
end

registry = DeliveryZones.new(via: :geojson, strategy: :rtree)
registry.reload!
registry.find_covering(5, 5)
```

Readers capture the current immutable Index. An active reader can continue using the old Index while `reload!` builds and swaps a new one.

## API

- `source { ... }` on a subclass defines the entry source.
- `index_options(...)` on a subclass sets default Index options.
- `reload!` builds and swaps a new Index.
- `find_covering`, `covering_ids`, `intersecting_rect`, and `covering_ids_batch_packed` delegate to the current Index.
- `index`, `size`, `bbox`, and `loaded?` expose current state.

The source must return the strict first-release entry format:

```ruby
[[id, object], [id, object], ...]
```

No Redis dependency, no Rails dependency, no global native singleton, and no in-place Index mutation are introduced.
