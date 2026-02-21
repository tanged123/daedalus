#include "daedalus/world/vehicle_pose.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

namespace {

void expect_matrix_near(const glm::dmat3 &actual, const glm::dmat3 &expected, double eps) {
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            EXPECT_NEAR(actual[c][r], expected[c][r], eps);
        }
    }
}

TEST(VehiclePose, ResolvesFromEcefAndBodyToEcefQuaternion) {
    daedalus::world::VehiclePoseInput input;
    input.position_ecef_m = glm::dvec3(1.2e6, -2.3e6, 5.1e6);
    input.q_body_to_ecef = glm::normalize(glm::dquat(0.8, 0.2, -0.3, 0.4));

    const auto pose = daedalus::world::solve_vehicle_pose(input);
    ASSERT_TRUE(pose.has_value());

    EXPECT_NEAR(pose->position_ecef_m.x, input.position_ecef_m->x, 1e-9);
    EXPECT_NEAR(pose->position_ecef_m.y, input.position_ecef_m->y, 1e-9);
    EXPECT_NEAR(pose->position_ecef_m.z, input.position_ecef_m->z, 1e-9);

    const glm::dmat3 expected = glm::mat3_cast(input.q_body_to_ecef.value());
    expect_matrix_near(pose->ecef_from_body, expected, 1e-12);
}

TEST(VehiclePose, ResolvesFromLlaAndBodyToNedQuaternion) {
    daedalus::world::VehiclePoseInput input;
    input.position_lla = daedalus::world::coord::Lla{0.51, -1.40, 12345.0};
    input.q_body_to_ned = glm::normalize(glm::dquat(0.92, -0.04, 0.23, 0.30));

    const auto pose = daedalus::world::solve_vehicle_pose(input);
    ASSERT_TRUE(pose.has_value());

    const auto &lla = input.position_lla.value();
    const glm::dvec3 expected_pos =
        daedalus::world::coord::lla_to_ecef(lla.lat_rad, lla.lon_rad, lla.alt_hae_m);
    EXPECT_NEAR(pose->position_ecef_m.x, expected_pos.x, 1e-6);
    EXPECT_NEAR(pose->position_ecef_m.y, expected_pos.y, 1e-6);
    EXPECT_NEAR(pose->position_ecef_m.z, expected_pos.z, 1e-6);

    const glm::dmat3 expected_rot =
        daedalus::world::coord::ned_to_ecef_rotation(lla.lat_rad, lla.lon_rad) *
        glm::mat3_cast(input.q_body_to_ned.value());
    expect_matrix_near(pose->ecef_from_body, expected_rot, 1e-12);
}

TEST(VehiclePose, ResolvesFromLlaAndEulerFallback) {
    daedalus::world::VehiclePoseInput input;
    input.position_lla = daedalus::world::coord::Lla{0.49, -1.33, 8000.0};
    input.euler_zyx_body_to_ned_rad = glm::dvec3(0.2, -0.1, 0.05);

    const auto pose = daedalus::world::solve_vehicle_pose(input);
    ASSERT_TRUE(pose.has_value());

    const auto &lla = input.position_lla.value();
    const glm::dmat3 expected_rot =
        daedalus::world::coord::ned_to_ecef_rotation(lla.lat_rad, lla.lon_rad) *
        daedalus::world::euler_zyx_to_dcm(0.2, -0.1, 0.05);
    expect_matrix_near(pose->ecef_from_body, expected_rot, 1e-12);
}

TEST(VehiclePose, ReturnsNulloptWhenNoPositionProvided) {
    daedalus::world::VehiclePoseInput input;
    input.euler_zyx_body_to_ned_rad = glm::dvec3(0.0, 0.0, 0.0);

    const auto pose = daedalus::world::solve_vehicle_pose(input);
    EXPECT_FALSE(pose.has_value());
}

TEST(VehiclePose, ComposeModelMatrixPlacesTranslationInFourthColumn) {
    daedalus::world::VehiclePose pose{};
    pose.position_ecef_m = glm::dvec3(10.0, 20.0, 30.0);
    pose.ecef_from_body = glm::dmat3(1.0);

    const glm::dmat4 model = daedalus::world::compose_model_matrix_ecef(pose);

    EXPECT_DOUBLE_EQ(model[3][0], 10.0);
    EXPECT_DOUBLE_EQ(model[3][1], 20.0);
    EXPECT_DOUBLE_EQ(model[3][2], 30.0);
    EXPECT_DOUBLE_EQ(model[3][3], 1.0);
}

} // namespace
