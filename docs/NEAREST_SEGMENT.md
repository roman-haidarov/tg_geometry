# Nearest segment

`Line#nearest_segment(x, y)` and `Ring#nearest_segment(x, y)` compute the nearest segment to a point in planar XY space.

```ruby
nearest = ring.nearest_segment(x, y)
nearest.segment   # TG::Geometry::Segment
nearest.index     # Integer segment index in the parent line/ring
nearest.distance  # Float
nearest.point     # [x, y] projection onto the segment
```

Distance is Euclidean distance in input coordinate units. It is not meters unless the input coordinate system is already meters.

Degenerate segments (`a == b`) are handled as point distance. The projection point for a degenerate segment is the segment endpoint.

When several segments have the same distance, tie-break behaviour follows tg iteration order. Code should not rely on a specific equal-distance segment.
