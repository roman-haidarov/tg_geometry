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

## Distance limitations

`*_lnglat_meters` distance methods are approximate local-meter helpers for geofencing. They use a per-query local equirectangular frame anchored at the query latitude. Segments are GeoJSON straight coordinate segments, not great-circle arcs. Accuracy is intended for local geofencing and degrades with latitude separation. This is not geodesy.

The metric is raw planar lng/lat and does not wrap longitude at `+/-180`. Cross-antimeridian proximity is not detected: `179.9` and `-179.9` are treated as roughly `360` degrees apart. This matches the rest of the gem's planar model and `covers_xy?` behavior. Data that legitimately crosses the antimeridian should be cut at `+/-180` before import.

Near the poles, longitude scale approaches zero. Distance methods remain finite and avoid NaN/Inf, but the local frame understates longitude distance. Radius queries near poles may therefore include accepted false positives relative to real geodesy; that is an accuracy limitation of the planar metric, not a geodesic guarantee.

`*_xy` distance methods never convert units. They return input coordinate units.

Distance queries keep the GVL. There is no no-GVL, Falcon, Async, Ractor, projection/reprojection, kNN, `nearest_ids`, signed distance, polygon-to-polygon distance, or geometry-to-geometry distance support in this feature.
