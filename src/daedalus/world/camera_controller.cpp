#include "daedalus/world/camera_controller.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace daedalus::world {

void CameraController::handle_drag(float dx, float dy) {
    azimuth_deg += dx * kDragSensitivity;
    elevation_deg -= dy * kDragSensitivity;
    elevation_deg = std::clamp(elevation_deg, kMinElevationDeg, kMaxElevationDeg);
}

void CameraController::handle_scroll(float delta) {
    const float scale = 1.0f - (delta * kScrollZoomFactor);
    distance_earth_radii *= std::max(0.1f, scale);
    distance_earth_radii = std::clamp(distance_earth_radii, kMinDistance, kMaxDistance);
}

glm::mat4 CameraController::view_matrix() const {
    const float az = glm::radians(azimuth_deg);
    const float el = glm::radians(elevation_deg);

    const float cos_el = std::cos(el);
    const glm::vec3 eye(distance_earth_radii * cos_el * std::cos(az),
                        distance_earth_radii * cos_el * std::sin(az),
                        distance_earth_radii * std::sin(el));

    return glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::mat4 CameraController::proj_matrix(float aspect) const {
    const float safe_aspect = std::max(0.1f, aspect);
    return glm::perspective(glm::radians(fov_deg), safe_aspect, 0.01f, 100.0f);
}

glm::dvec3 CameraController::ecef_position() const {
    const double az = glm::radians(static_cast<double>(azimuth_deg));
    const double el = glm::radians(static_cast<double>(elevation_deg));
    const double cos_el = std::cos(el);
    const double d = static_cast<double>(distance_earth_radii);

    return glm::dvec3(d * cos_el * std::cos(az), d * cos_el * std::sin(az), d * std::sin(el));
}

} // namespace daedalus::world
