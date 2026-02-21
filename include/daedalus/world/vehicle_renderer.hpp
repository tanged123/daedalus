#pragma once

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>

#include <glm/glm.hpp>

#include <filesystem>

namespace daedalus::world {

class VehicleRenderer {
  public:
    VehicleRenderer() = default;
    ~VehicleRenderer();

    VehicleRenderer(const VehicleRenderer &) = delete;
    VehicleRenderer &operator=(const VehicleRenderer &) = delete;

    void init(const std::filesystem::path &shader_dir);
    void shutdown();
    void draw(const glm::mat4 &vp, const glm::mat4 &model, bool visible) const;

    [[nodiscard]] bool is_initialized() const { return initialized_; }

  private:
    struct Vertex {
        glm::vec3 pos;
        float dist;
    };

    void upload_geometry();

    bool initialized_ = false;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLint vp_uniform_ = -1;
    GLint model_uniform_ = -1;
    GLint color_uniform_ = -1;
    GLint soft_edge_uniform_ = -1;

    GLsizei x_axis_first_ = 0;
    GLsizei y_axis_first_ = 0;
    GLsizei z_axis_first_ = 0;
    GLsizei marker_first_ = 0;
    GLsizei marker_count_ = 0;
};

} // namespace daedalus::world
