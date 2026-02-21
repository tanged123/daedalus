#pragma once

#include "daedalus/world/coordinate_adapter.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>

namespace daedalus::world {

struct VehiclePoseInput {
    std::optional<glm::dvec3> position_ecef_m;
    std::optional<coord::Lla> position_lla;
    std::optional<glm::dquat> q_body_to_ecef;
    std::optional<glm::dquat> q_body_to_ned;

    // Stored as yaw (x), pitch (y), roll (z) in radians.
    std::optional<glm::dvec3> euler_zyx_body_to_ned_rad;
};

struct VehiclePose {
    glm::dvec3 position_ecef_m{0.0};
    glm::dmat3 ecef_from_body{1.0};
};

[[nodiscard]] glm::dmat3 euler_zyx_to_dcm(double yaw_rad, double pitch_rad, double roll_rad);
[[nodiscard]] std::optional<VehiclePose> solve_vehicle_pose(const VehiclePoseInput &input);
[[nodiscard]] glm::dmat4 compose_model_matrix_ecef(const VehiclePose &pose);

} // namespace daedalus::world
