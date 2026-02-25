#pragma once

#include "daedalus/data/signal_buffer.hpp"
#include "daedalus/world/camera_controller.hpp"
#include "daedalus/world/gl_util.hpp"
#include "daedalus/world/globe_renderer.hpp"
#include "daedalus/world/vehicle_pose.hpp"
#include "daedalus/world/vehicle_renderer.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace daedalus::world {

class WorldView {
  public:
    WorldView() = default;
    ~WorldView();

    WorldView(const WorldView &) = delete;
    WorldView &operator=(const WorldView &) = delete;

    void init();
    void shutdown();

    void update(const std::map<size_t, data::SignalBuffer> &signal_buffers,
                const std::vector<std::string> &subscribed_signals);
    void render();

  private:
    struct SignalSlots {
        std::optional<size_t> ecef_x;
        std::optional<size_t> ecef_y;
        std::optional<size_t> ecef_z;
        std::optional<size_t> lat;
        std::optional<size_t> lon;
        std::optional<size_t> alt;
        std::optional<size_t> q_be_w;
        std::optional<size_t> q_be_x;
        std::optional<size_t> q_be_y;
        std::optional<size_t> q_be_z;
        std::optional<size_t> q_bn_w;
        std::optional<size_t> q_bn_x;
        std::optional<size_t> q_bn_y;
        std::optional<size_t> q_bn_z;
        std::optional<size_t> yaw;
        std::optional<size_t> pitch;
        std::optional<size_t> roll;
    };

    void handle_input();
    void resolve_signal_slots(const std::vector<std::string> &subscribed_signals);
    [[nodiscard]] static std::optional<double>
    latest_signal_value(const std::map<size_t, data::SignalBuffer> &signal_buffers,
                        const std::optional<size_t> &index);
    [[nodiscard]] std::string describe_signal(const std::optional<size_t> &index) const;
    [[nodiscard]] glm::mat4 make_vehicle_model(const VehiclePose &pose) const;

    bool initialized_ = false;
    gl::Fbo scene_fbo_;
    CameraController camera_;
    GlobeRenderer globe_;
    VehicleRenderer vehicle_;
    SignalSlots signal_slots_;
    std::vector<std::string> subscribed_signals_cache_;
    glm::mat4 vehicle_model_{1.0f};
    glm::vec3 vehicle_position_unit_{0.0f, 0.0f, 0.0f};
    bool vehicle_visible_ = false;
    bool follow_vehicle_camera_ = false;
    bool show_vehicle_debug_ = true;
    std::string vehicle_status_ = "No pose data";
};

} // namespace daedalus::world
