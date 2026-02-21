#include "daedalus/world/vehicle_pose.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace daedalus::world {

glm::dmat3 euler_zyx_to_dcm(double yaw_rad, double pitch_rad, double roll_rad) {
    const glm::dmat3 rz =
        glm::dmat3(glm::rotate(glm::dmat4(1.0), yaw_rad, glm::dvec3(0.0, 0.0, 1.0)));
    const glm::dmat3 ry =
        glm::dmat3(glm::rotate(glm::dmat4(1.0), pitch_rad, glm::dvec3(0.0, 1.0, 0.0)));
    const glm::dmat3 rx =
        glm::dmat3(glm::rotate(glm::dmat4(1.0), roll_rad, glm::dvec3(1.0, 0.0, 0.0)));
    return rz * ry * rx;
}

std::optional<VehiclePose> solve_vehicle_pose(const VehiclePoseInput &input) {
    glm::dvec3 position_ecef(0.0);
    if (input.position_ecef_m.has_value()) {
        position_ecef = input.position_ecef_m.value();
    } else if (input.position_lla.has_value()) {
        const coord::Lla &lla = input.position_lla.value();
        position_ecef = coord::lla_to_ecef(lla.lat_rad, lla.lon_rad, lla.alt_hae_m);
    } else {
        return std::nullopt;
    }

    coord::Lla lla = input.position_lla.has_value() ? input.position_lla.value()
                                                    : coord::ecef_to_lla(position_ecef);

    glm::dmat3 ecef_from_body(1.0);
    if (input.q_body_to_ecef.has_value()) {
        ecef_from_body = glm::mat3_cast(glm::normalize(input.q_body_to_ecef.value()));
    } else {
        glm::dmat3 ned_from_body(1.0);
        if (input.q_body_to_ned.has_value()) {
            ned_from_body = glm::mat3_cast(glm::normalize(input.q_body_to_ned.value()));
        } else if (input.euler_zyx_body_to_ned_rad.has_value()) {
            const glm::dvec3 ypr = input.euler_zyx_body_to_ned_rad.value();
            ned_from_body = euler_zyx_to_dcm(ypr.x, ypr.y, ypr.z);
        }

        ecef_from_body = coord::ned_to_ecef_rotation(lla.lat_rad, lla.lon_rad) * ned_from_body;
    }

    return VehiclePose{position_ecef, ecef_from_body};
}

glm::dmat4 compose_model_matrix_ecef(const VehiclePose &pose) {
    glm::dmat4 model(1.0);
    model[0] = glm::dvec4(pose.ecef_from_body[0], 0.0);
    model[1] = glm::dvec4(pose.ecef_from_body[1], 0.0);
    model[2] = glm::dvec4(pose.ecef_from_body[2], 0.0);
    model[3] = glm::dvec4(pose.position_ecef_m, 1.0);
    return model;
}

} // namespace daedalus::world
