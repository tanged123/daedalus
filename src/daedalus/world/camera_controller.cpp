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
    distance_earth_radii = std::clamp(distance_earth_radii, min_distance_, max_distance_);
}

void CameraController::set_distance_limits(float min_distance, float max_distance) {
    min_distance_ = std::max(1e-7f, std::min(min_distance, max_distance));
    max_distance_ = std::max(min_distance_, max_distance);
    distance_earth_radii = std::clamp(distance_earth_radii, min_distance_, max_distance_);
}

glm::mat4 CameraController::view_matrix() const {
    return view_matrix(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::mat4 CameraController::view_matrix(const glm::vec3 &target, const glm::vec3 &up_hint) const {
    const float az = glm::radians(azimuth_deg);
    const float el = glm::radians(elevation_deg);

    const float cos_el = std::cos(el);
    const glm::vec3 offset(distance_earth_radii * cos_el * std::cos(az),
                           distance_earth_radii * cos_el * std::sin(az),
                           distance_earth_radii * std::sin(el));
    const glm::vec3 eye = target + offset;
    glm::vec3 up = up_hint;
    if (glm::dot(up, up) < 1e-12f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        up = glm::normalize(up);
    }

    return glm::lookAt(eye, target, up);
}

glm::mat4 CameraController::proj_matrix(float aspect) const {
    return proj_matrix(aspect, 0.01f, 100.0f);
}

glm::mat4 CameraController::proj_matrix(float aspect, float near_plane, float far_plane) const {
    const float safe_aspect = std::max(0.1f, aspect);
    const float safe_near = std::max(1e-7f, near_plane);
    const float safe_far = std::max(safe_near * 1.1f, far_plane);
    return glm::perspective(glm::radians(fov_deg), safe_aspect, safe_near, safe_far);
}

glm::dvec3 CameraController::ecef_position() const {
    const double az = glm::radians(static_cast<double>(azimuth_deg));
    const double el = glm::radians(static_cast<double>(elevation_deg));
    const double cos_el = std::cos(el);
    const double d = static_cast<double>(distance_earth_radii);

    return glm::dvec3(d * cos_el * std::cos(az), d * cos_el * std::sin(az), d * std::sin(el));
}

} // namespace daedalus::world
