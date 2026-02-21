#include "daedalus/world/world_view.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <exception>

namespace daedalus::world {

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
        globe_.init(gl::resolve_assets_dir() / "shaders");
        initialized_ = true;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[Daedalus] WorldView init failed: %s\n", e.what());
        shutdown();
    }
}

void WorldView::shutdown() {
    globe_.shutdown();
    scene_fbo_.destroy();
    initialized_ = false;
}

void WorldView::update(const std::map<size_t, data::SignalBuffer> &signal_buffers) {
    (void)signal_buffers;
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

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ImGui::Image(static_cast<ImTextureID>(scene_fbo_.color_tex), panel_size, ImVec2(0.0f, 1.0f),
                 ImVec2(1.0f, 0.0f));
}

} // namespace daedalus::world
