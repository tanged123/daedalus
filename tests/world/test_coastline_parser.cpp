#include "daedalus/world/globe_renderer.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace {

TEST(CoastlineParser, ParsesLineAndMultiLineFeatures) {
    const std::string geojson = R"JSON(
{
  "type": "FeatureCollection",
  "features": [
    {
      "type": "Feature",
      "geometry": {
        "type": "LineString",
        "coordinates": [[0, 0], [10, 0], [20, 0]]
      }
    },
    {
      "type": "Feature",
      "geometry": {
        "type": "MultiLineString",
        "coordinates": [
          [[30, 10], [35, 12]],
          [[40, 20], [45, 22], [50, 23]]
        ]
      }
    }
  ]
}
)JSON";

    const auto parsed = daedalus::world::GlobeRenderer::parse_coastline_geojson(geojson);

    EXPECT_EQ(parsed.segment_count, 5u);
    EXPECT_EQ(parsed.vertices.size(), 10u);

    for (const auto &vertex : parsed.vertices) {
        const float radius = std::sqrt(vertex.pos.x * vertex.pos.x + vertex.pos.y * vertex.pos.y +
                                       vertex.pos.z * vertex.pos.z);
        EXPECT_NEAR(radius, 1.001f, 1e-4f);
    }
}

TEST(CoastlineParser, RequiresFeatureArray) {
    const std::string bad_geojson = R"JSON({"type":"FeatureCollection"})JSON";
    EXPECT_THROW((void)daedalus::world::GlobeRenderer::parse_coastline_geojson(bad_geojson),
                 std::runtime_error);
}

} // namespace
