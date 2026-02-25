#pragma once

#include <glm/glm.hpp>

namespace daedalus::world {

class CameraController {
  public:
    void handle_drag(float dx, float dy);
    void handle_scroll(float delta);
    void set_distance_limits(float min_distance, float max_distance);

    [[nodiscard]] glm::mat4 view_matrix() const;
    [[nodiscard]] glm::mat4 view_matrix(const glm::vec3 &target, const glm::vec3 &up_hint) const;
    [[nodiscard]] glm::mat4 proj_matrix(float aspect) const;
    [[nodiscard]] glm::mat4 proj_matrix(float aspect, float near_plane, float far_plane) const;
    [[nodiscard]] glm::dvec3 ecef_position() const;

    float azimuth_deg = 0.0f;
    float elevation_deg = 30.0f;
    float distance_earth_radii = 2.5f;
    float fov_deg = 45.0f;

  private:
    static constexpr float kMinElevationDeg = -89.0f;
    static constexpr float kMaxElevationDeg = 89.0f;
    static constexpr float kDefaultMinDistance = 1.05f;
    static constexpr float kDefaultMaxDistance = 30.0f;
    static constexpr float kDragSensitivity = 0.35f;
    static constexpr float kScrollZoomFactor = 0.12f;

    float min_distance_ = kDefaultMinDistance;
    float max_distance_ = kDefaultMaxDistance;
};

} // namespace daedalus::world
