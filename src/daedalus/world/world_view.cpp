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
#include <sstream>
#include <string>
#include <string_view>

namespace daedalus::world {

namespace {

constexpr double kEarthRadiusM = 6378137.0;
constexpr float kOrbitMinDistanceER = 1.05f;
constexpr float kOrbitMaxDistanceER = 30.0f;
constexpr float kFollowMinDistanceER = 0.00002f; // ~128 m
constexpr float kFollowMaxDistanceER = 1.2f;     // ~7,650 km

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

[[nodiscard]] std::string format_xyz(const glm::vec3 &v) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[%.3f, %.3f, %.3f]", v.x, v.y, v.z);
    return std::string(buf);
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
    vehicle_position_unit_ = glm::vec3(0.0f);
    vehicle_status_.assign("No pose data");
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
        vehicle_status_ =
            "Vehicle hidden: no position signals (need position_lla.* or position_ecef.*)";
        return;
    }

    vehicle_model_ = make_vehicle_model(pose.value());
    vehicle_position_unit_ = glm::vec3(pose->position_ecef_m / kEarthRadiusM);
    vehicle_visible_ = true;
    vehicle_status_ =
        std::string("Vehicle visible at ") + format_xyz(vehicle_position_unit_) + " Earth radii";
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

    ImGui::Checkbox("Follow vehicle camera", &follow_vehicle_camera_);
    ImGui::SameLine();
    ImGui::Checkbox("Debug pose", &show_vehicle_debug_);

    const ImVec2 image_size = ImGui::GetContentRegionAvail();
    if (image_size.x < 2.0f || image_size.y < 2.0f) {
        return;
    }

    const bool follow_vehicle = follow_vehicle_camera_ && vehicle_visible_;
    if (follow_vehicle) {
        camera_.set_distance_limits(kFollowMinDistanceER, kFollowMaxDistanceER);
    } else {
        camera_.set_distance_limits(kOrbitMinDistanceER, kOrbitMaxDistanceER);
    }

    handle_input();

    const int width = static_cast<int>(image_size.x);
    const int height = static_cast<int>(image_size.y);

    gl::GlStateGuard guard;
    scene_fbo_.resize(width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_.fbo);
    glViewport(0, 0, width, height);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = image_size.x / image_size.y;
    const glm::vec3 target = follow_vehicle ? vehicle_position_unit_ : glm::vec3(0.0f);
    const glm::vec3 up_hint =
        follow_vehicle ? glm::normalize(vehicle_position_unit_) : glm::vec3(0.0f, 0.0f, 1.0f);
    const float near_plane = follow_vehicle ? 0.000002f : 0.01f;
    const float far_plane = follow_vehicle ? 12.0f : 100.0f;
    const glm::mat4 vp =
        camera_.proj_matrix(aspect, near_plane, far_plane) * camera_.view_matrix(target, up_hint);
    globe_.draw(vp);
    vehicle_.draw(vp, vehicle_model_, vehicle_visible_);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ImGui::Image(static_cast<ImTextureID>(scene_fbo_.color_tex), image_size, ImVec2(0.0f, 1.0f),
                 ImVec2(1.0f, 0.0f));

    if (show_vehicle_debug_) {
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetItemRectMin();
        const std::string lla_signals = "LLA: " + describe_signal(signal_slots_.lat) + ", " +
                                        describe_signal(signal_slots_.lon) + ", " +
                                        describe_signal(signal_slots_.alt);
        const std::string ecef_signals = "ECEF: " + describe_signal(signal_slots_.ecef_x) + ", " +
                                         describe_signal(signal_slots_.ecef_y) + ", " +
                                         describe_signal(signal_slots_.ecef_z);
        const std::string att_signals = "Att q_be: " + describe_signal(signal_slots_.q_be_w) +
                                        ", " + describe_signal(signal_slots_.q_be_x) + ", " +
                                        describe_signal(signal_slots_.q_be_y) + ", " +
                                        describe_signal(signal_slots_.q_be_z);

        draw_list->AddText(ImVec2(min.x + 10.0f, min.y + 10.0f), IM_COL32(180, 230, 255, 255),
                           vehicle_status_.c_str());
        draw_list->AddText(ImVec2(min.x + 10.0f, min.y + 28.0f), IM_COL32(150, 210, 230, 255),
                           lla_signals.c_str());
        draw_list->AddText(ImVec2(min.x + 10.0f, min.y + 46.0f), IM_COL32(150, 210, 230, 255),
                           ecef_signals.c_str());
        draw_list->AddText(ImVec2(min.x + 10.0f, min.y + 64.0f), IM_COL32(150, 210, 230, 255),
                           att_signals.c_str());
    }
}

void WorldView::resolve_signal_slots(const std::vector<std::string> &subscribed_signals) {
    signal_slots_ = SignalSlots{};

    std::vector<std::string> lowered_signals;
    lowered_signals.reserve(subscribed_signals.size());
    for (const std::string &name : subscribed_signals) {
        lowered_signals.push_back(to_lower_ascii(name));
    }

    signal_slots_.ecef_x =
        find_signal_by_suffix(lowered_signals, {"position_ecef.x", "ecef_position.x", "ecef.x",
                                                "position_ecef_m.x", "position.x"});
    signal_slots_.ecef_y =
        find_signal_by_suffix(lowered_signals, {"position_ecef.y", "ecef_position.y", "ecef.y",
                                                "position_ecef_m.y", "position.y"});
    signal_slots_.ecef_z =
        find_signal_by_suffix(lowered_signals, {"position_ecef.z", "ecef_position.z", "ecef.z",
                                                "position_ecef_m.z", "position.z"});

    signal_slots_.lat = find_signal_by_suffix(
        lowered_signals, {"position_lla.lat", "lla.lat", "position_lla.latitude"});
    signal_slots_.lon = find_signal_by_suffix(
        lowered_signals, {"position_lla.lon", "lla.lon", "position_lla.longitude"});
    signal_slots_.alt = find_signal_by_suffix(
        lowered_signals, {"position_lla.alt", "lla.alt", "position_lla.altitude", "altitude"});

    signal_slots_.q_be_w =
        find_signal_by_suffix(lowered_signals, {"attitude.w", "quaternion.w", "quat_be.w",
                                                "q_body_to_ecef.w", "attitude_be.w"});
    signal_slots_.q_be_x =
        find_signal_by_suffix(lowered_signals, {"attitude.x", "quaternion.x", "quat_be.x",
                                                "q_body_to_ecef.x", "attitude_be.x"});
    signal_slots_.q_be_y =
        find_signal_by_suffix(lowered_signals, {"attitude.y", "quaternion.y", "quat_be.y",
                                                "q_body_to_ecef.y", "attitude_be.y"});
    signal_slots_.q_be_z =
        find_signal_by_suffix(lowered_signals, {"attitude.z", "quaternion.z", "quat_be.z",
                                                "q_body_to_ecef.z", "attitude_be.z"});

    signal_slots_.q_bn_w =
        find_signal_by_suffix(lowered_signals, {"attitude_ned.w", "quaternion_ned.w", "quat_bn.w"});
    signal_slots_.q_bn_x =
        find_signal_by_suffix(lowered_signals, {"attitude_ned.x", "quaternion_ned.x", "quat_bn.x"});
    signal_slots_.q_bn_y =
        find_signal_by_suffix(lowered_signals, {"attitude_ned.y", "quaternion_ned.y", "quat_bn.y"});
    signal_slots_.q_bn_z =
        find_signal_by_suffix(lowered_signals, {"attitude_ned.z", "quaternion_ned.z", "quat_bn.z"});

    signal_slots_.yaw =
        find_signal_by_suffix(lowered_signals, {"euler_zyx.yaw", "euler.yaw", "yaw", "yaw_rad"});
    signal_slots_.pitch = find_signal_by_suffix(
        lowered_signals, {"euler_zyx.pitch", "euler.pitch", "pitch", "pitch_rad"});
    signal_slots_.roll = find_signal_by_suffix(
        lowered_signals, {"euler_zyx.roll", "euler.roll", "roll", "roll_rad"});
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

std::string WorldView::describe_signal(const std::optional<size_t> &index) const {
    if (!index.has_value()) {
        return "<none>";
    }
    if (index.value() >= subscribed_signals_cache_.size()) {
        std::ostringstream os;
        os << '#' << index.value() << "<?>";
        return os.str();
    }
    std::ostringstream os;
    os << '#' << index.value() << ' ' << subscribed_signals_cache_[index.value()];
    return os.str();
}

} // namespace daedalus::world
