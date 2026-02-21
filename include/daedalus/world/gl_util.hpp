#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>

namespace daedalus::world::gl {

struct Fbo {
    GLuint fbo = 0;
    GLuint color_tex = 0;
    GLuint depth_rb = 0;
    int width = 0;
    int height = 0;

    static Fbo create(int w, int h);
    void resize(int w, int h);
    void destroy();
};

class GlStateGuard {
  public:
    GlStateGuard();
    ~GlStateGuard();

    GlStateGuard(const GlStateGuard &) = delete;
    GlStateGuard &operator=(const GlStateGuard &) = delete;

  private:
    GLint prev_fbo_ = 0;
    GLint prev_vao_ = 0;
    GLint prev_program_ = 0;
    GLint prev_active_tex_ = 0;
    GLint prev_tex_2d_ = 0;
    GLint prev_viewport_[4] = {0, 0, 0, 0};
    GLint prev_blend_eq_rgb_ = 0;
    GLint prev_blend_eq_alpha_ = 0;
    GLint prev_blend_src_rgb_ = 0;
    GLint prev_blend_dst_rgb_ = 0;
    GLint prev_blend_src_alpha_ = 0;
    GLint prev_blend_dst_alpha_ = 0;
    GLboolean prev_blend_ = GL_FALSE;
    GLboolean prev_depth_test_ = GL_FALSE;
    GLboolean prev_depth_mask_ = GL_TRUE;
    GLboolean prev_cull_face_ = GL_FALSE;
    GLboolean prev_scissor_test_ = GL_FALSE;
};

GLuint compile_shader(GLenum type, std::string_view source);
GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);
GLuint link_program(GLuint vertex_shader, GLuint geometry_shader, GLuint fragment_shader);

std::string read_text_file(const std::filesystem::path &path);
std::filesystem::path resolve_assets_dir();

} // namespace daedalus::world::gl
