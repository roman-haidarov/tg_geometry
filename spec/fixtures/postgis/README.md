# PostGIS / EWKB fixtures

These files are committed binary EWKB fixtures used by the v0.3.0 SRID/EWKB tests.
They are intentionally small and are not generated at spec runtime.

Canonical PostGIS generation examples:

```sql
SELECT ST_AsEWKB(ST_GeomFromText('POINT(37.6 55.7)', 4326));
SELECT ST_AsEWKB(ST_GeomFromText('POLYGON((0 0,10 0,10 10,0 10,0 0))', 4326));
SELECT ST_AsEWKB(ST_GeomFromText('POLYGON((0 0,10 0,10 10,0 10,0 0),(2 2,4 2,4 4,2 4,2 2))', 4326));
SELECT ST_AsEWKB(ST_GeomFromText('POLYGON((0 0,10 0,10 10,0 10,0 0))', 3857));
```

`multipolygon_large.ewkb` is a synthetic large multipolygon fixture with about 7000 points.
It stands in for a real imported polygon while keeping the repository small.
