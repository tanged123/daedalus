#include "daedalus/world/world_view.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <string_view>

namespace daedalus::world {

namespace {

constexpr double kEarthRadiusM = 6378137.0;

[[nodiscard]] std::string to_lower_ascii(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

[[nodiscard]] bool ends_with(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

[[nodiscard]] std::optional<size_t>
find_signal_by_suffix(const std::vector<std::string> &lowered_signals,
                      std::initializer_list<std::string_view> suffixes) {
    size_t best_index = std::numeric_limits<size_t>::max();
    size_t best_length = std::numeric_limits<size_t>::max();

    for (size_t i = 0; i < lowered_signals.size(); ++i) {
        for (std::string_view suffix : suffixes) {
            if (!ends_with(lowered_signals[i], suffix)) {
                continue;
            }
            if (lowered_signals[i].size() < best_length) {
                best_index = i;
                best_length = lowered_signals[i].size();
            }
        }
    }

    if (best_index == std::numeric_limits<size_t>::max()) {
        return std::nullopt;
    }
    return best_index;
}

} // namespace

WorldView::~WorldView() { shutdown(); }

void WorldView::init() {
    if (initialized_) {
        return;
    }

    if (glfwGetCurrentContext() == nullptr) {
        std::fprintf(stderr, "[Daedalus] WorldView init skipped: no OpenGL context\n");
        return;
    }

    try {
        scene_fbo_ = gl::Fbo::create(1, 1);
        const std::filesystem::path shader_dir = gl::resolve_assets_dir() / "shaders";
        globe_.init(shader_dir);
        vehicle_.init(shader_dir);
        initialized_ = true;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[Daedalus] WorldView init failed: %s\n", e.what());
        shutdown();
    }
}

void WorldView::shutdown() {
    vehicle_.shutdown();
    globe_.shutdown();
    scene_fbo_.destroy();
    vehicle_visible_ = false;
    vehicle_model_ = glm::mat4(1.0f);
    initialized_ = false;
}

void WorldView::update(const std::map<size_t, data::SignalBuffer> &signal_buffers,
                       const std::vector<std::string> &subscribed_signals) {
    if (subscribed_signals != subscribed_signals_cache_) {
        subscribed_signals_cache_ = subscribed_signals;
        resolve_signal_slots(subscribed_signals_cache_);
    }

    VehiclePoseInput input;

    const auto x_ecef = latest_signal_value(signal_buffers, signal_slots_.ecef_x);
    const auto y_ecef = latest_signal_value(signal_buffers, signal_slots_.ecef_y);
    const auto z_ecef = latest_signal_value(signal_buffers, signal_slots_.ecef_z);
    if (x_ecef.has_value() && y_ecef.has_value() && z_ecef.has_value()) {
        input.position_ecef_m = glm::dvec3(x_ecef.value(), y_ecef.value(), z_ecef.value());
    }

    const auto lat = latest_signal_value(signal_buffers, signal_slots_.lat);
    const auto lon = latest_signal_value(signal_buffers, signal_slots_.lon);
    const auto alt = latest_signal_value(signal_buffers, signal_slots_.alt);
    if (lat.has_value() && lon.has_value() && alt.has_value()) {
        input.position_lla = coord::Lla{lat.value(), lon.value(), alt.value()};
    }

    const auto q_be_w = latest_signal_value(signal_buffers, signal_slots_.q_be_w);
    const auto q_be_x = latest_signal_value(signal_buffers, signal_slots_.q_be_x);
    const auto q_be_y = latest_signal_value(signal_buffers, signal_slots_.q_be_y);
    const auto q_be_z = latest_signal_value(signal_buffers, signal_slots_.q_be_z);
    if (q_be_w.has_value() && q_be_x.has_value() && q_be_y.has_value() && q_be_z.has_value()) {
        input.q_body_to_ecef =
            glm::dquat(q_be_w.value(), q_be_x.value(), q_be_y.value(), q_be_z.value());
    } else {
        const auto q_bn_w = latest_signal_value(signal_buffers, signal_slots_.q_bn_w);
        const auto q_bn_x = latest_signal_value(signal_buffers, signal_slots_.q_bn_x);
        const auto q_bn_y = latest_signal_value(signal_buffers, signal_slots_.q_bn_y);
        const auto q_bn_z = latest_signal_value(signal_buffers, signal_slots_.q_bn_z);
        if (q_bn_w.has_value() && q_bn_x.has_value() && q_bn_y.has_value() && q_bn_z.has_value()) {
            input.q_body_to_ned =
                glm::dquat(q_bn_w.value(), q_bn_x.value(), q_bn_y.value(), q_bn_z.value());
        } else {
            const auto yaw = latest_signal_value(signal_buffers, signal_slots_.yaw);
            const auto pitch = latest_signal_value(signal_buffers, signal_slots_.pitch);
            const auto roll = latest_signal_value(signal_buffers, signal_slots_.roll);
            if (yaw.has_value() && pitch.has_value() && roll.has_value()) {
                input.euler_zyx_body_to_ned_rad =
                    glm::dvec3(yaw.value(), pitch.value(), roll.value());
            }
        }
    }

    const std::optional<VehiclePose> pose = solve_vehicle_pose(input);
    if (!pose.has_value()) {
        vehicle_visible_ = false;
        return;
    }

    vehicle_model_ = make_vehicle_model(pose.value());
    vehicle_visible_ = true;
}

void WorldView::handle_input() {
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        camera_.handle_drag(io.MouseDelta.x, io.MouseDelta.y);
    }

    if (io.MouseWheel != 0.0f) {
        camera_.handle_scroll(io.MouseWheel);
    }
}

void WorldView::render() {
    if (!initialized_) {
        init();
    }

    if (!initialized_) {
        ImGui::TextDisabled("World view unavailable (OpenGL init failed)");
        return;
    }

    const ImVec2 panel_size = ImGui::GetContentRegionAvail();
    if (panel_size.x < 2.0f || panel_size.y < 2.0f) {
        return;
    }

    handle_input();

    const int width = static_cast<int>(panel_size.x);
    const int height = static_cast<int>(panel_size.y);

    gl::GlStateGuard guard;
    scene_fbo_.resize(width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_.fbo);
    glViewport(0, 0, width, height);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = panel_size.x / panel_size.y;
    const glm::mat4 vp = camera_.proj_matrix(aspect) * camera_.view_matrix();
    globe_.draw(vp);
    vehicle_.draw(vp, vehicle_model_, vehicle_visible_);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ImGui::Image(static_cast<ImTextureID>(scene_fbo_.color_tex), panel_size, ImVec2(0.0f, 1.0f),
                 ImVec2(1.0f, 0.0f));
}

void WorldView::resolve_signal_slots(const std::vector<std::string> &subscribed_signals) {
    signal_slots_ = SignalSlots{};

    std::vector<std::string> lowered_signals;
    lowered_signals.reserve(subscribed_signals.size());
    for (const std::string &name : subscribed_signals) {
        lowered_signals.push_back(to_lower_ascii(name));
    }

    signal_slots_.ecef_x =
        find_signal_by_suffix(lowered_signals, {"position_ecef.x", "ecef_position.x", "ecef.x"});
    signal_slots_.ecef_y =
        find_signal_by_suffix(lowered_signals, {"position_ecef.y", "ecef_position.y", "ecef.y"});
    signal_slots_.ecef_z =
        find_signal_by_suffix(lowered_signals, {"position_ecef.z", "ecef_position.z", "ecef.z"});

    signal_slots_.lat = find_signal_by_suffix(lowered_signals, {"position_lla.lat", "lla.lat"});
    signal_slots_.lon = find_signal_by_suffix(lowered_signals, {"position_lla.lon", "lla.lon"});
    signal_slots_.alt = find_signal_by_suffix(lowered_signals, {"position_lla.alt", "lla.alt"});

    signal_slots_.q_be_w =
        find_signal_by_suffix(lowered_signals, {"attitude.w", "quaternion.w", "quat_be.w"});
    signal_slots_.q_be_x =
        find_signal_by_suffix(lowered_signals, {"attitude.x", "quaternion.x", "quat_be.x"});
    signal_slots_.q_be_y =
        find_signal_by_suffix(lowered_signals, {"attitude.y", "quaternion.y", "quat_be.y"});
    signal_slots_.q_be_z =
        find_signal_by_suffix(lowered_signals, {"attitude.z", "quaternion.z", "quat_be.z"});

    signal_slots_.q_bn_w =
        find_signal_by_suffix(lowered_signals, {"attitude_ned.w", "quaternion_ned.w", "quat_bn.w"});
    signal_slots_.q_bn_x =
        find_signal_by_suffix(lowered_signals, {"attitude_ned.x", "quaternion_ned.x", "quat_bn.x"});
    signal_slots_.q_bn_y =
        find_signal_by_suffix(lowered_signals, {"attitude_ned.y", "quaternion_ned.y", "quat_bn.y"});
    signal_slots_.q_bn_z =
        find_signal_by_suffix(lowered_signals, {"attitude_ned.z", "quaternion_ned.z", "quat_bn.z"});

    signal_slots_.yaw = find_signal_by_suffix(lowered_signals, {"euler_zyx.yaw", "euler.yaw"});
    signal_slots_.pitch =
        find_signal_by_suffix(lowered_signals, {"euler_zyx.pitch", "euler.pitch"});
    signal_slots_.roll = find_signal_by_suffix(lowered_signals, {"euler_zyx.roll", "euler.roll"});
}

std::optional<double>
WorldView::latest_signal_value(const std::map<size_t, data::SignalBuffer> &signal_buffers,
                               const std::optional<size_t> &index) {
    if (!index.has_value()) {
        return std::nullopt;
    }

    const auto it = signal_buffers.find(index.value());
    if (it == signal_buffers.end() || it->second.empty()) {
        return std::nullopt;
    }

    return it->second.last_value();
}

glm::mat4 WorldView::make_vehicle_model(const VehiclePose &pose) const {
    glm::dmat4 model = compose_model_matrix_ecef(pose);
    model[3] = glm::dvec4(pose.position_ecef_m / kEarthRadiusM, 1.0);

    const double distance = std::max(1.0, static_cast<double>(camera_.distance_earth_radii));
    const double scale = std::clamp(0.012 * distance, 0.012, 0.08);
    model = model * glm::scale(glm::dmat4(1.0), glm::dvec3(scale));
    return glm::mat4(model);
}

} // namespace daedalus::world
