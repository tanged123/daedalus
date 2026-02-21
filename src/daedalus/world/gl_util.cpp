#include "daedalus/world/gl_util.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace daedalus::world::gl {

namespace {

[[noreturn]] void throw_with_log(const std::string &prefix, GLuint object,
                                 void (*log_query)(GLuint, GLsizei, GLsizei *, GLchar *),
                                 void (*length_query)(GLuint, GLenum, GLint *)) {
    GLint log_len = 0;
    length_query(object, GL_INFO_LOG_LENGTH, &log_len);

    std::string log;
    if (log_len > 1) {
        std::vector<GLchar> buf(static_cast<size_t>(log_len), '\0');
        GLsizei written = 0;
        log_query(object, log_len, &written, buf.data());
        log.assign(buf.data(), static_cast<size_t>(written));
    }

    throw std::runtime_error(prefix + (log.empty() ? " (no log output)" : ": " + log));
}

} // namespace

Fbo Fbo::create(int w, int h) {
    Fbo fbo_obj;
    fbo_obj.resize(w, h);
    return fbo_obj;
}

void Fbo::resize(int w, int h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (width == w && height == h && fbo != 0) {
        return;
    }

    if (fbo == 0) {
        glGenFramebuffers(1, &fbo);
    }
    if (color_tex == 0) {
        glGenTextures(1, &color_tex);
    }
    if (depth_rb == 0) {
        glGenRenderbuffers(1, &depth_rb);
    }

    GLint prev_fbo = 0;
    GLint prev_tex = 0;
    GLint prev_rb = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rb);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev_tex));
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prev_rb));
        throw std::runtime_error("Failed to create world view framebuffer");
    }

    width = w;
    height = h;

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev_tex));
    glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prev_rb));
}

void Fbo::destroy() {
    if (depth_rb != 0) {
        glDeleteRenderbuffers(1, &depth_rb);
        depth_rb = 0;
    }
    if (color_tex != 0) {
        glDeleteTextures(1, &color_tex);
        color_tex = 0;
    }
    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    width = 0;
    height = 0;
}

GlStateGuard::GlStateGuard() {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo_);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao_);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program_);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_tex_);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex_2d_);
    glGetIntegerv(GL_VIEWPORT, prev_viewport_);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prev_blend_eq_rgb_);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prev_blend_eq_alpha_);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prev_blend_src_rgb_);
    glGetIntegerv(GL_BLEND_DST_RGB, &prev_blend_dst_rgb_);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prev_blend_src_alpha_);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prev_blend_dst_alpha_);
    glGetBooleanv(GL_BLEND, &prev_blend_);
    glGetBooleanv(GL_DEPTH_TEST, &prev_depth_test_);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask_);
    glGetBooleanv(GL_CULL_FACE, &prev_cull_face_);
    glGetBooleanv(GL_SCISSOR_TEST, &prev_scissor_test_);
}

GlStateGuard::~GlStateGuard() {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo_));
    glBindVertexArray(static_cast<GLuint>(prev_vao_));
    glUseProgram(static_cast<GLuint>(prev_program_));
    glActiveTexture(static_cast<GLenum>(prev_active_tex_));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev_tex_2d_));
    glViewport(prev_viewport_[0], prev_viewport_[1], prev_viewport_[2], prev_viewport_[3]);
    glBlendEquationSeparate(static_cast<GLenum>(prev_blend_eq_rgb_),
                            static_cast<GLenum>(prev_blend_eq_alpha_));
    glBlendFuncSeparate(
        static_cast<GLenum>(prev_blend_src_rgb_), static_cast<GLenum>(prev_blend_dst_rgb_),
        static_cast<GLenum>(prev_blend_src_alpha_), static_cast<GLenum>(prev_blend_dst_alpha_));

    (prev_blend_ == GL_TRUE ? glEnable : glDisable)(GL_BLEND);
    (prev_depth_test_ == GL_TRUE ? glEnable : glDisable)(GL_DEPTH_TEST);
    glDepthMask(prev_depth_mask_);
    (prev_cull_face_ == GL_TRUE ? glEnable : glDisable)(GL_CULL_FACE);
    (prev_scissor_test_ == GL_TRUE ? glEnable : glDisable)(GL_SCISSOR_TEST);
}

GLuint compile_shader(GLenum type, std::string_view source) {
    const GLuint shader = glCreateShader(type);
    const char *src_ptr = source.data();
    const GLint src_len = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &src_ptr, &src_len);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        throw_with_log(
            "Shader compilation failed", shader,
            [](GLuint obj, GLsizei len, GLsizei *written, GLchar *log) {
                glGetShaderInfoLog(obj, len, written, log);
            },
            [](GLuint obj, GLenum pname, GLint *value) { glGetShaderiv(obj, pname, value); });
    }

    return shader;
}

GLuint link_program(GLuint vertex_shader, GLuint fragment_shader) {
    return link_program(vertex_shader, 0, fragment_shader);
}

GLuint link_program(GLuint vertex_shader, GLuint geometry_shader, GLuint fragment_shader) {
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    if (geometry_shader != 0) {
        glAttachShader(program, geometry_shader);
    }
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        throw_with_log(
            "Program linking failed", program,
            [](GLuint obj, GLsizei len, GLsizei *written, GLchar *log) {
                glGetProgramInfoLog(obj, len, written, log);
            },
            [](GLuint obj, GLenum pname, GLint *value) { glGetProgramiv(obj, pname, value); });
    }

    return program;
}

std::string read_text_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Could not open file: " + path.string());
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::filesystem::path resolve_assets_dir() {
    if (const char *env = std::getenv("DAEDALUS_ASSETS_DIR"); env != nullptr && env[0] != '\0') {
        return std::filesystem::path(env);
    }
#ifdef DAEDALUS_ASSETS_DIR_DEFAULT
    return std::filesystem::path(DAEDALUS_ASSETS_DIR_DEFAULT);
#else
    return std::filesystem::path("assets");
#endif
}

std::filesystem::path resolve_coastline_geojson_path() {
    if (const char *env = std::getenv("DAEDALUS_COASTLINE_GEOJSON");
        env != nullptr && env[0] != '\0') {
        return std::filesystem::path(env);
    }
    return resolve_assets_dir() / "data" / "ne_50m_coastline.geojson";
}

} // namespace daedalus::world::gl
