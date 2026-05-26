# Limitations

`tg_geometry` is not a full GIS system.

Not included:

- geocoding;
- routing;
- projections;
- geodesic distance/area;
- buffer / union / difference / overlay result geometry operations;
- nearest POI indexing;
- public callback/search APIs;
- Ractor support claim;
- no-GVL execution claim;
- automatic strategy selection.

TG works in planar XY coordinates. If lon/lat coordinates are passed in, length, area, and perimeter-style values are in input coordinate units, not meters.

FeatureSource reads the full source into memory. It avoids a Ruby `JSON.parse` object tree, but it is not a streaming backend. There is no gzip, NDGeoJSON, GeoJSONSeq, or FlatGeobuf support.

FeatureSource returns raw properties JSON strings. It does not parse properties into Ruby Hash objects.
