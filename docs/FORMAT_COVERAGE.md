# Format coverage

Expansion Block D exposes additional TG format helpers without changing ownership rules.

## Parse helpers

- `TG::Geometry.parse_hex(str, index: :ystripes)`
- `TG::Geometry.parse_geobin(bytes, index: :ystripes)`

These are shortcuts over `TG::Geometry.parse(..., format: :hex)` and `TG::Geometry.parse(..., format: :geobin)`.

## Writer helpers

- `TG::Geometry::Geom#to_hex` -> UTF-8 String
- `TG::Geometry::Geom#to_geobin` -> ASCII-8BIT String

Writers use the same direct Ruby string buffer pattern as existing writers. Hex is text and GeoBIN is binary.

## Raw extra_json

- `TG::Geometry::Geom#extra_json` -> UTF-8 String or nil

This returns a copied Ruby String from TG's raw extra JSON pointer. It does not parse JSON into Hashes and does not expose borrowed child wrappers. This follows the constraint that raw `extra_json` is safer than implicit `JSON.parse`.
