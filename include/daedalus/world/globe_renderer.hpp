#pragma once

#include "daedalus/world/gl_util.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace daedalus::world {

struct LineVertex {
    glm::vec3 pos;
    float dist = 0.0f;
};

struct GraticuleGeometry {
    std::vector<LineVertex> dim_vertices;
    std::vector<LineVertex> major_vertices;
    std::vector<LineVertex> special_vertices;

    std::size_t dim_segment_count = 0;
    std::size_t major_segment_count = 0;
    std::size_t special_segment_count = 0;
    std::size_t special_line_count = 0;

    [[nodiscard]] std::size_t base_segment_count() const {
        return dim_segment_count + major_segment_count;
    }
};

struct CoastlineGeometry {
    std::vector<LineVertex> vertices;
    std::size_t segment_count = 0;
};

class GlobeRenderer {
  public:
    GlobeRenderer() = default;
    ~GlobeRenderer();

    GlobeRenderer(const GlobeRenderer &) = delete;
    GlobeRenderer &operator=(const GlobeRenderer &) = delete;

    static GraticuleGeometry build_graticule(int step_deg = 15);
    static CoastlineGeometry parse_coastline_geojson(std::string_view geojson_text);

    void init(const std::filesystem::path &shader_dir);
    void shutdown();
    void draw(const glm::mat4 &vp) const;

    [[nodiscard]] bool is_initialized() const { return initialized_; }
    [[nodiscard]] const GraticuleGeometry &geometry() const { return geometry_; }
    [[nodiscard]] bool has_coastlines() const { return coastlines_loaded_; }
    [[nodiscard]] std::size_t coastline_segment_count() const { return coastline_segment_count_; }

  private:
    struct Layer {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei vertex_count = 0;
    };

    static glm::vec3 latlon_to_unit_sphere(float lat_rad, float lon_rad);
    static void append_latitude_circle(std::vector<LineVertex> &vertices, float lat_rad,
                                       int segments_per_circle);
    static void append_longitude_circle(std::vector<LineVertex> &vertices, float lon_rad,
                                        int segments_per_half_circle);

    static void upload_layer(Layer &layer, const std::vector<LineVertex> &vertices);
    static void destroy_layer(Layer &layer);

    void draw_layer(const Layer &layer, const glm::vec4 &color, float line_width,
                    float soft_edge) const;
    void load_coastlines(const std::filesystem::path &geojson_path);

    bool initialized_ = false;
    GLuint line_program_ = 0;
    GLint vp_uniform_ = -1;
    GLint model_uniform_ = -1;
    GLint color_uniform_ = -1;
    GLint soft_edge_uniform_ = -1;

    GraticuleGeometry geometry_;
    Layer dim_layer_;
    Layer major_layer_;
    Layer special_layer_;
    Layer coastline_layer_;
    bool coastlines_loaded_ = false;
    std::size_t coastline_segment_count_ = 0;
};

} // namespace daedalus::world
