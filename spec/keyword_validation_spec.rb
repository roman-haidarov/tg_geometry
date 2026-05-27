# frozen_string_literal: true

require "spec_helper"

RSpec.describe "strict keyword validation" do
  let(:geom_without_srid) { TG::Geometry.parse_wkt("POINT (1 2)") }
  let(:wkb) { geom_without_srid.to_wkb }

  it "rejects unsupported SRID and EWKB flags on parse_wkb" do
    expect { TG::Geometry.parse_wkb(wkb, srid: 4326) }
      .to raise_error(TG::Geometry::ArgumentError, /unknown keyword: :srid/)
    expect { TG::Geometry.parse_wkb(wkb, ewkb: true) }
      .to raise_error(TG::Geometry::ArgumentError, /unknown keyword: :ewkb/)
  end

  it "rejects public release_gvl knobs on new v0.3.0 APIs" do
    expect { TG::Geometry.line_string([[0, 0], [1, 1]], release_gvl: true) }
      .to raise_error(TG::Geometry::ArgumentError, /unknown keyword: :release_gvl/)
    expect { geom_without_srid.to_ewkb(srid: 4326, release_gvl: true) }
      .to raise_error(TG::Geometry::ArgumentError, /unknown keyword: :release_gvl/)
  end

  it "rejects unknown constructor keywords" do
    square = [[0, 0], [1, 0], [1, 1], [0, 0]]

    expect { TG::Geometry.polygon(square, autoclose: true) }
      .to raise_error(TG::Geometry::ArgumentError, /unknown keyword: :autoclose/)
    expect { TG::Geometry.multi_polygon([{ exterior: square, holes: [], extra: true }]) }
      .to raise_error(TG::Geometry::ArgumentError, /unknown keyword: :extra/)
  end
end
