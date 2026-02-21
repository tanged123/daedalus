#include "daedalus/world/globe_renderer.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace daedalus::world {

namespace {

constexpr int kSegmentsPerCircle = 360;
constexpr int kSegmentsPerHalfCircle = 180;
constexpr int kMajorStepDeg = 30;
constexpr float kTropicLatDeg = 23.436f;
constexpr float kPolarCircleLatDeg = 66.564f;

[[nodiscard]] bool is_major_line(int deg) { return (std::abs(deg) % kMajorStepDeg) == 0; }

} // namespace

GlobeRenderer::~GlobeRenderer() { shutdown(); }

GraticuleGeometry GlobeRenderer::build_graticule(int step_deg) {
    if (step_deg <= 0) {
        throw std::invalid_argument("Graticule step must be positive");
    }

    GraticuleGeometry geo;

    // Parallels (constant latitude, excluding poles).
    for (int lat_deg = -90 + step_deg; lat_deg < 90; lat_deg += step_deg) {
        const float lat_rad = glm::radians(static_cast<float>(lat_deg));
        auto &target = is_major_line(lat_deg) ? geo.major_vertices : geo.dim_vertices;
        append_latitude_circle(target, lat_rad, kSegmentsPerCircle);
    }

    // Meridians (constant longitude).
    for (int lon_deg = -180; lon_deg < 180; lon_deg += step_deg) {
        const float lon_rad = glm::radians(static_cast<float>(lon_deg));
        auto &target = is_major_line(lon_deg) ? geo.major_vertices : geo.dim_vertices;
        append_longitude_circle(target, lon_rad, kSegmentsPerHalfCircle);
    }

    // Special highlight lines: equator, prime meridian, tropics, polar circles.
    append_latitude_circle(geo.special_vertices, glm::radians(0.0f), kSegmentsPerCircle);
    append_longitude_circle(geo.special_vertices, glm::radians(0.0f), kSegmentsPerHalfCircle);
    append_latitude_circle(geo.special_vertices, glm::radians(+kTropicLatDeg), kSegmentsPerCircle);
    append_latitude_circle(geo.special_vertices, glm::radians(-kTropicLatDeg), kSegmentsPerCircle);
    append_latitude_circle(geo.special_vertices, glm::radians(+kPolarCircleLatDeg),
                           kSegmentsPerCircle);
    append_latitude_circle(geo.special_vertices, glm::radians(-kPolarCircleLatDeg),
                           kSegmentsPerCircle);

    geo.dim_segment_count = geo.dim_vertices.size() / 2;
    geo.major_segment_count = geo.major_vertices.size() / 2;
    geo.special_segment_count = geo.special_vertices.size() / 2;
    geo.special_line_count = 6;

    return geo;
}

void GlobeRenderer::init(const std::filesystem::path &shader_dir) {
    shutdown();

    const std::string vert_src = gl::read_text_file(shader_dir / "line.vert");
    const std::string frag_src = gl::read_text_file(shader_dir / "line.frag");

    const GLuint vert = gl::compile_shader(GL_VERTEX_SHADER, vert_src);
    const GLuint frag = gl::compile_shader(GL_FRAGMENT_SHADER, frag_src);
    line_program_ = gl::link_program(vert, frag);

    glDeleteShader(vert);
    glDeleteShader(frag);

    vp_uniform_ = glGetUniformLocation(line_program_, "u_vp");
    color_uniform_ = glGetUniformLocation(line_program_, "u_color");
    soft_edge_uniform_ = glGetUniformLocation(line_program_, "u_soft_edge");

    geometry_ = build_graticule(15);
    upload_layer(dim_layer_, geometry_.dim_vertices);
    upload_layer(major_layer_, geometry_.major_vertices);
    upload_layer(special_layer_, geometry_.special_vertices);

    initialized_ = true;
}

void GlobeRenderer::shutdown() {
    destroy_layer(dim_layer_);
    destroy_layer(major_layer_);
    destroy_layer(special_layer_);

    if (line_program_ != 0) {
        glDeleteProgram(line_program_);
        line_program_ = 0;
    }

    vp_uniform_ = -1;
    color_uniform_ = -1;
    soft_edge_uniform_ = -1;
    initialized_ = false;
}

void GlobeRenderer::draw(const glm::mat4 &vp) const {
    if (!initialized_) {
        return;
    }

    glUseProgram(line_program_);
    glUniformMatrix4fv(vp_uniform_, 1, GL_FALSE, glm::value_ptr(vp));

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    draw_layer(dim_layer_, glm::vec4(0.0f, 0.25f, 0.12f, 0.65f), 1.0f, 2.0f);
    draw_layer(major_layer_, glm::vec4(0.0f, 0.45f, 0.20f, 0.85f), 1.2f, 4.0f);
    draw_layer(special_layer_, glm::vec4(0.0f, 1.1f, 0.45f, 0.95f), 1.6f, 6.0f);

    glBindVertexArray(0);
    glUseProgram(0);
}

glm::vec3 GlobeRenderer::latlon_to_unit_sphere(float lat_rad, float lon_rad) {
    const float cos_lat = std::cos(lat_rad);
    return glm::vec3(cos_lat * std::cos(lon_rad), cos_lat * std::sin(lon_rad), std::sin(lat_rad));
}

void GlobeRenderer::append_latitude_circle(std::vector<LineVertex> &vertices, float lat_rad,
                                           int segments_per_circle) {
    for (int i = 0; i < segments_per_circle; ++i) {
        const float lon0 =
            glm::radians(static_cast<float>(i) * 360.0f / static_cast<float>(segments_per_circle));
        const float lon1 = glm::radians(static_cast<float>(i + 1) * 360.0f /
                                        static_cast<float>(segments_per_circle));
        vertices.push_back(LineVertex{latlon_to_unit_sphere(lat_rad, lon0), 0.0f});
        vertices.push_back(LineVertex{latlon_to_unit_sphere(lat_rad, lon1), 0.0f});
    }
}

void GlobeRenderer::append_longitude_circle(std::vector<LineVertex> &vertices, float lon_rad,
                                            int segments_per_half_circle) {
    for (int i = 0; i < segments_per_half_circle; ++i) {
        const float lat0 = glm::radians(-90.0f + static_cast<float>(i) * 180.0f /
                                                     static_cast<float>(segments_per_half_circle));
        const float lat1 = glm::radians(-90.0f + static_cast<float>(i + 1) * 180.0f /
                                                     static_cast<float>(segments_per_half_circle));
        vertices.push_back(LineVertex{latlon_to_unit_sphere(lat0, lon_rad), 0.0f});
        vertices.push_back(LineVertex{latlon_to_unit_sphere(lat1, lon_rad), 0.0f});
    }
}

void GlobeRenderer::upload_layer(Layer &layer, const std::vector<LineVertex> &vertices) {
    destroy_layer(layer);
    if (vertices.empty()) {
        return;
    }

    glGenVertexArrays(1, &layer.vao);
    glGenBuffers(1, &layer.vbo);

    glBindVertexArray(layer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, layer.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(LineVertex)),
                 vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                          reinterpret_cast<const void *>(offsetof(LineVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                          reinterpret_cast<const void *>(offsetof(LineVertex, dist)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    layer.vertex_count = static_cast<GLsizei>(vertices.size());
}

void GlobeRenderer::destroy_layer(Layer &layer) {
    if (layer.vbo != 0) {
        glDeleteBuffers(1, &layer.vbo);
        layer.vbo = 0;
    }
    if (layer.vao != 0) {
        glDeleteVertexArrays(1, &layer.vao);
        layer.vao = 0;
    }
    layer.vertex_count = 0;
}

void GlobeRenderer::draw_layer(const Layer &layer, const glm::vec4 &color, float line_width,
                               float soft_edge) const {
    if (layer.vao == 0 || layer.vertex_count == 0) {
        return;
    }

    glUniform4f(color_uniform_, color.r, color.g, color.b, color.a);
    glUniform1f(soft_edge_uniform_, soft_edge);

    glLineWidth(line_width);
    glBindVertexArray(layer.vao);
    glDrawArrays(GL_LINES, 0, layer.vertex_count);
}

} // namespace daedalus::world
