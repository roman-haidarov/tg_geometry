# SRID and EWKB

EWKB extends WKB with extra metadata bits. PostGIS sets the SRID flag (`0x20000000`) in the geometry type word and inserts a 32-bit SRID after the type header.

`tg` already understands EWKB enough to parse the geometry payload correctly, but it does not expose SRID metadata. `tg_geometry` therefore reads the EWKB header at wrapper level before passing the original bytes to `tg_parse_wkb_ix` / `tg_parse_hexn_ix`.

The original bytes are not modified. The native `tg_geom` remains a plain planar geometry. The Ruby `TG::Geometry::Geom` wrapper stores SRID metadata in parallel:

```ruby
geom = TG::Geometry.parse_wkb(ewkb)
geom.srid # => Integer or nil
```

`parse_wkb` and `parse_hex` preserve SRID metadata. GeoJSON, WKT, GeoBIN, and `parse(format: :auto)` do not guarantee SRID preservation in v0.3.0.

`to_wkb` intentionally stays plain WKB. Use `to_ewkb` for PostGIS-compatible SRID-bearing output:

```ruby
geom.to_ewkb
geom.to_ewkb(srid: 4326)
```

SRID is metadata only. No reprojection, SRID compatibility check, meter conversion, or geodesic calculation is performed.
