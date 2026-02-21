#pragma once

#include <glm/glm.hpp>

namespace daedalus::world::coord {

struct Lla {
    double lat_rad = 0.0;
    double lon_rad = 0.0;
    double alt_hae_m = 0.0;
};

[[nodiscard]] glm::dvec3 lla_to_ecef(double lat_rad, double lon_rad, double alt_hae_m);
[[nodiscard]] Lla ecef_to_lla(const glm::dvec3 &ecef_m);
[[nodiscard]] glm::dmat3 ned_to_ecef_rotation(double lat_rad, double lon_rad);
[[nodiscard]] glm::mat4 rte_transform(const glm::dmat4 &model_ecef, const glm::dvec3 &camera_ecef);

} // namespace daedalus::world::coord
