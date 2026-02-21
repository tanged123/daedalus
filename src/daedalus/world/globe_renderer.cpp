#include "daedalus/world/globe_renderer.hpp"
#include "daedalus/world/gl_util.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace daedalus::world {

namespace {

constexpr int kSegmentsPerCircle = 360;
constexpr int kSegmentsPerHalfCircle = 180;
constexpr int kMajorStepDeg = 30;
constexpr float kTropicLatDeg = 23.436f;
constexpr float kPolarCircleLatDeg = 66.564f;
constexpr float kCoastlineRadius = 1.001f;

[[nodiscard]] bool is_major_line(int deg) { return (std::abs(deg) % kMajorStepDeg) == 0; }

[[nodiscard]] bool is_coordinate_pair(const nlohmann::json &coord) {
    return coord.is_array() && coord.size() >= 2 && coord[0].is_number() && coord[1].is_number();
}

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

CoastlineGeometry GlobeRenderer::parse_coastline_geojson(std::string_view geojson_text) {
    const nlohmann::json root = nlohmann::json::parse(geojson_text);
    if (!root.contains("features") || !root["features"].is_array()) {
        throw std::runtime_error("Coastline GeoJSON missing features array");
    }

    CoastlineGeometry out;
    auto process_strip = [&out](const nlohmann::json &coords) {
        if (!coords.is_array() || coords.size() < 2) {
            return;
        }

        for (size_t i = 0; i + 1 < coords.size(); ++i) {
            const nlohmann::json &c0 = coords[i];
            const nlohmann::json &c1 = coords[i + 1];
            if (!is_coordinate_pair(c0) || !is_coordinate_pair(c1)) {
                continue;
            }

            const float lon0 = glm::radians(c0[0].get<float>());
            const float lat0 = glm::radians(c0[1].get<float>());
            const float lon1 = glm::radians(c1[0].get<float>());
            const float lat1 = glm::radians(c1[1].get<float>());

            out.vertices.push_back(
                LineVertex{latlon_to_unit_sphere(lat0, lon0) * kCoastlineRadius, 0.0f});
            out.vertices.push_back(
                LineVertex{latlon_to_unit_sphere(lat1, lon1) * kCoastlineRadius, 0.0f});
            ++out.segment_count;
        }
    };

    for (const auto &feature : root["features"]) {
        if (!feature.contains("geometry") || !feature["geometry"].is_object()) {
            continue;
        }
        const auto &geom = feature["geometry"];
        if (!geom.contains("type") || !geom["type"].is_string() || !geom.contains("coordinates")) {
            continue;
        }

        const std::string type = geom["type"].get<std::string>();
        if (type == "LineString") {
            process_strip(geom["coordinates"]);
        } else if (type == "MultiLineString" && geom["coordinates"].is_array()) {
            for (const auto &strip : geom["coordinates"]) {
                process_strip(strip);
            }
        }
    }

    return out;
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
    model_uniform_ = glGetUniformLocation(line_program_, "u_model");
    color_uniform_ = glGetUniformLocation(line_program_, "u_color");
    soft_edge_uniform_ = glGetUniformLocation(line_program_, "u_soft_edge");

    geometry_ = build_graticule(15);
    upload_layer(dim_layer_, geometry_.dim_vertices);
    upload_layer(major_layer_, geometry_.major_vertices);
    upload_layer(special_layer_, geometry_.special_vertices);
    load_coastlines(gl::resolve_coastline_geojson_path());

    initialized_ = true;
}

void GlobeRenderer::shutdown() {
    destroy_layer(dim_layer_);
    destroy_layer(major_layer_);
    destroy_layer(special_layer_);
    destroy_layer(coastline_layer_);

    if (line_program_ != 0) {
        glDeleteProgram(line_program_);
        line_program_ = 0;
    }

    vp_uniform_ = -1;
    model_uniform_ = -1;
    color_uniform_ = -1;
    soft_edge_uniform_ = -1;
    coastlines_loaded_ = false;
    coastline_segment_count_ = 0;
    initialized_ = false;
}

void GlobeRenderer::draw(const glm::mat4 &vp) const {
    if (!initialized_) {
        return;
    }

    glUseProgram(line_program_);
    glUniformMatrix4fv(vp_uniform_, 1, GL_FALSE, glm::value_ptr(vp));
    const glm::mat4 identity(1.0f);
    glUniformMatrix4fv(model_uniform_, 1, GL_FALSE, glm::value_ptr(identity));

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    draw_layer(dim_layer_, glm::vec4(0.0f, 0.25f, 0.12f, 0.65f), 1.0f, 2.0f);
    draw_layer(major_layer_, glm::vec4(0.0f, 0.45f, 0.20f, 0.85f), 1.2f, 4.0f);
    draw_layer(special_layer_, glm::vec4(0.0f, 1.1f, 0.45f, 0.95f), 1.6f, 6.0f);
    draw_layer(coastline_layer_, glm::vec4(0.0f, 0.85f, 0.3f, 0.92f), 1.4f, 4.0f);

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

void GlobeRenderer::load_coastlines(const std::filesystem::path &geojson_path) {
    destroy_layer(coastline_layer_);
    coastlines_loaded_ = false;
    coastline_segment_count_ = 0;

    if (geojson_path.empty() || !std::filesystem::exists(geojson_path)) {
        return;
    }

    try {
        const std::string payload = gl::read_text_file(geojson_path);
        const CoastlineGeometry coast = parse_coastline_geojson(payload);
        upload_layer(coastline_layer_, coast.vertices);
        coastline_segment_count_ = coast.segment_count;
        coastlines_loaded_ = coastline_layer_.vertex_count > 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[Daedalus] Coastline load failed (%s): %s\n",
                     geojson_path.string().c_str(), e.what());
        destroy_layer(coastline_layer_);
        coastlines_loaded_ = false;
        coastline_segment_count_ = 0;
    }
}

} // namespace daedalus::world
