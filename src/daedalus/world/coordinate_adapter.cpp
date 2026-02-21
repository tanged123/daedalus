#include "daedalus/world/coordinate_adapter.hpp"

#include <vulcan/coordinates/FramePrimitives.hpp>
#include <vulcan/coordinates/Geodetic.hpp>

namespace daedalus::world::coord {

glm::dvec3 lla_to_ecef(double lat_rad, double lon_rad, double alt_hae_m) {
    const vulcan::LLA<double> lla{lon_rad, lat_rad, alt_hae_m};
    const vulcan::Vec3<double> ecef = vulcan::lla_to_ecef(lla);
    return glm::dvec3(ecef(0), ecef(1), ecef(2));
}

Lla ecef_to_lla(const glm::dvec3 &ecef_m) {
    vulcan::Vec3<double> ecef;
    ecef << ecef_m.x, ecef_m.y, ecef_m.z;

    const vulcan::LLA<double> lla = vulcan::ecef_to_lla(ecef);
    return Lla{lla.lat, lla.lon, lla.alt};
}

glm::dmat3 ned_to_ecef_rotation(double lat_rad, double lon_rad) {
    const vulcan::CoordinateFrame<double> ned =
        vulcan::CoordinateFrame<double>::ned(lon_rad, lat_rad);
    const vulcan::Mat3<double> dcm = ned.dcm();

    glm::dmat3 out(1.0);
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            out[c][r] = dcm(r, c);
        }
    }
    return out;
}

glm::mat4 rte_transform(const glm::dmat4 &model_ecef, const glm::dvec3 &camera_ecef) {
    glm::dmat4 rte = model_ecef;
    rte[3] -= glm::dvec4(camera_ecef, 0.0);
    return glm::mat4(rte);
}

} // namespace daedalus::world::coord
