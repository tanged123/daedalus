#include "daedalus/world/coordinate_adapter.hpp"

#include <gtest/gtest.h>

#include <vulcan/coordinates/FramePrimitives.hpp>
#include <vulcan/coordinates/Geodetic.hpp>

namespace {

TEST(CoordinateAdapter, LlaToEcefMatchesVulcan) {
    constexpr double lat = 0.4995;
    constexpr double lon = -1.4067;
    constexpr double alt = 12450.0;

    const glm::dvec3 ecef = daedalus::world::coord::lla_to_ecef(lat, lon, alt);

    const vulcan::LLA<double> lla{lon, lat, alt};
    const vulcan::Vec3<double> expected = vulcan::lla_to_ecef(lla);

    EXPECT_NEAR(ecef.x, expected(0), 1e-6);
    EXPECT_NEAR(ecef.y, expected(1), 1e-6);
    EXPECT_NEAR(ecef.z, expected(2), 1e-6);
}

TEST(CoordinateAdapter, EcefToLlaMatchesVulcan) {
    vulcan::Vec3<double> ecef;
    ecef << 906330.485338, -5537298.413228, 3043686.230361;

    const auto expected = vulcan::ecef_to_lla(ecef);
    const auto actual = daedalus::world::coord::ecef_to_lla(glm::dvec3(ecef(0), ecef(1), ecef(2)));

    EXPECT_NEAR(actual.lat_rad, expected.lat, 1e-12);
    EXPECT_NEAR(actual.lon_rad, expected.lon, 1e-12);
    EXPECT_NEAR(actual.alt_hae_m, expected.alt, 1e-6);
}

TEST(CoordinateAdapter, NedRotationMatchesVulcanFrame) {
    constexpr double lat = 0.3;
    constexpr double lon = -1.2;

    const glm::dmat3 actual = daedalus::world::coord::ned_to_ecef_rotation(lat, lon);

    const auto ned = vulcan::CoordinateFrame<double>::ned(lon, lat);
    const auto expected = ned.dcm();

    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            EXPECT_NEAR(actual[c][r], expected(r, c), 1e-12);
        }
    }
}

TEST(CoordinateAdapter, RteTransformSubtractsCameraTranslation) {
    glm::dmat4 model(1.0);
    model[3] = glm::dvec4(1000.0, 2000.0, 3000.0, 1.0);

    const glm::dvec3 camera(10.0, 20.0, 30.0);
    const glm::mat4 rte = daedalus::world::coord::rte_transform(model, camera);

    EXPECT_FLOAT_EQ(rte[3][0], 990.0f);
    EXPECT_FLOAT_EQ(rte[3][1], 1980.0f);
    EXPECT_FLOAT_EQ(rte[3][2], 2970.0f);
    EXPECT_FLOAT_EQ(rte[3][3], 1.0f);
}

} // namespace
