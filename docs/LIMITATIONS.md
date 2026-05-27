# Limitations

`tg_geometry` is not a full GIS system.

Not included:

- Geodesic / Haversine distance;
- Projection / reprojection;
- Buffer / union / difference / convex hull;
- Index nearest_ids (KNN);
- GeoBIN bbox helpers;
- Streaming FeatureSource;
- Index serialization / mmap;
- Ractor support;
- Windows / JRuby;
- Write-side ActiveRecordType / AR scopes / migrations;
- Z/M variants of array constructors;
- Public `release_gvl:` option.

TG works in planar XY coordinates. If lon/lat coordinates are passed in, length, area, and perimeter-style values are in input coordinate units, not meters.

FeatureSource reads the full source into memory. It avoids a Ruby `JSON.parse` object tree, but it is not a streaming backend. There is no gzip, NDGeoJSON, GeoJSONSeq, or FlatGeobuf support.

FeatureSource returns raw properties JSON strings. It does not parse properties into Ruby Hash objects.
