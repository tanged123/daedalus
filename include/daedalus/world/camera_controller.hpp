#pragma once

#include <glm/glm.hpp>

namespace daedalus::world {

class CameraController {
  public:
    void handle_drag(float dx, float dy);
    void handle_scroll(float delta);

    [[nodiscard]] glm::mat4 view_matrix() const;
    [[nodiscard]] glm::mat4 proj_matrix(float aspect) const;
    [[nodiscard]] glm::dvec3 ecef_position() const;

    float azimuth_deg = 0.0f;
    float elevation_deg = 30.0f;
    float distance_earth_radii = 2.5f;
    float fov_deg = 45.0f;

  private:
    static constexpr float kMinElevationDeg = -89.0f;
    static constexpr float kMaxElevationDeg = 89.0f;
    static constexpr float kMinDistance = 1.05f;
    static constexpr float kMaxDistance = 30.0f;
    static constexpr float kDragSensitivity = 0.35f;
    static constexpr float kScrollZoomFactor = 0.12f;
};

} // namespace daedalus::world
