#include "daedalus/world/vehicle_renderer.hpp"

#include "daedalus/world/gl_util.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace daedalus::world {

VehicleRenderer::~VehicleRenderer() { shutdown(); }

void VehicleRenderer::init(const std::filesystem::path &shader_dir) {
    shutdown();

    const std::string vert_src = gl::read_text_file(shader_dir / "line.vert");
    const std::string frag_src = gl::read_text_file(shader_dir / "line.frag");

    const GLuint vert = gl::compile_shader(GL_VERTEX_SHADER, vert_src);
    const GLuint frag = gl::compile_shader(GL_FRAGMENT_SHADER, frag_src);
    program_ = gl::link_program(vert, frag);

    glDeleteShader(vert);
    glDeleteShader(frag);

    vp_uniform_ = glGetUniformLocation(program_, "u_vp");
    model_uniform_ = glGetUniformLocation(program_, "u_model");
    color_uniform_ = glGetUniformLocation(program_, "u_color");
    soft_edge_uniform_ = glGetUniformLocation(program_, "u_soft_edge");
    camera_pos_uniform_ = glGetUniformLocation(program_, "u_camera_pos");
    clip_backside_uniform_ = glGetUniformLocation(program_, "u_clip_backside");

    upload_geometry();
    initialized_ = true;
}

void VehicleRenderer::shutdown() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }

    vp_uniform_ = -1;
    model_uniform_ = -1;
    color_uniform_ = -1;
    soft_edge_uniform_ = -1;
    camera_pos_uniform_ = -1;
    clip_backside_uniform_ = -1;
    x_axis_first_ = 0;
    y_axis_first_ = 0;
    z_axis_first_ = 0;
    marker_first_ = 0;
    marker_count_ = 0;
    initialized_ = false;
}

void VehicleRenderer::draw(const glm::mat4 &vp, const glm::mat4 &model, bool visible) const {
    if (!initialized_ || !visible || vao_ == 0) {
        return;
    }

    glUseProgram(program_);
    glUniformMatrix4fv(vp_uniform_, 1, GL_FALSE, glm::value_ptr(vp));
    glUniformMatrix4fv(model_uniform_, 1, GL_FALSE, glm::value_ptr(model));
    glUniform3f(camera_pos_uniform_, 0.0f, 0.0f, 0.0f);
    glUniform1i(clip_backside_uniform_, 0);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    glBindVertexArray(vao_);

    glUniform1f(soft_edge_uniform_, 5.0f);

    glLineWidth(2.2f);
    glUniform4f(color_uniform_, 1.3f, 1.3f, 1.3f, 0.95f);
    glDrawArrays(GL_LINES, x_axis_first_, 2);

    glLineWidth(1.7f);
    glUniform4f(color_uniform_, 0.35f, 0.95f, 1.1f, 0.90f);
    glDrawArrays(GL_LINES, y_axis_first_, 2);
    glDrawArrays(GL_LINES, z_axis_first_, 2);

    glLineWidth(1.6f);
    glUniform4f(color_uniform_, 1.35f, 0.70f, 0.10f, 0.95f);
    glDrawArrays(GL_LINES, marker_first_, marker_count_);

    glBindVertexArray(0);
    glUseProgram(0);
}

void VehicleRenderer::upload_geometry() {
    std::vector<Vertex> vertices;
    vertices.reserve(6 + 16);

    const auto push_seg = [&vertices](const glm::vec3 &a, const glm::vec3 &b) {
        vertices.push_back(Vertex{a, 0.0f});
        vertices.push_back(Vertex{b, 0.0f});
    };

    const glm::vec3 origin(0.0f);
    const glm::vec3 x_tip(1.0f, 0.0f, 0.0f);
    const glm::vec3 y_tip(0.0f, 1.0f, 0.0f);
    const glm::vec3 z_tip(0.0f, 0.0f, 1.0f);

    x_axis_first_ = static_cast<GLsizei>(vertices.size());
    push_seg(origin, x_tip);
    y_axis_first_ = static_cast<GLsizei>(vertices.size());
    push_seg(origin, y_tip);
    z_axis_first_ = static_cast<GLsizei>(vertices.size());
    push_seg(origin, z_tip);

    const glm::vec3 nose(1.20f, 0.0f, 0.0f);
    const glm::vec3 right(0.85f, +0.22f, 0.0f);
    const glm::vec3 left(0.85f, -0.22f, 0.0f);
    const glm::vec3 up(0.85f, 0.0f, +0.22f);
    const glm::vec3 down(0.85f, 0.0f, -0.22f);

    marker_first_ = static_cast<GLsizei>(vertices.size());
    push_seg(nose, right);
    push_seg(nose, left);
    push_seg(nose, up);
    push_seg(nose, down);
    push_seg(right, up);
    push_seg(up, left);
    push_seg(left, down);
    push_seg(down, right);
    marker_count_ = static_cast<GLsizei>(vertices.size()) - marker_first_;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void *>(offsetof(Vertex, pos)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void *>(offsetof(Vertex, dist)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace daedalus::world
