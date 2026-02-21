#pragma once

#include "daedalus/data/signal_buffer.hpp"
#include "daedalus/world/camera_controller.hpp"
#include "daedalus/world/gl_util.hpp"
#include "daedalus/world/globe_renderer.hpp"

#include <map>

namespace daedalus::world {

class WorldView {
  public:
    WorldView() = default;
    ~WorldView();

    WorldView(const WorldView &) = delete;
    WorldView &operator=(const WorldView &) = delete;

    void init();
    void shutdown();

    void update(const std::map<size_t, data::SignalBuffer> &signal_buffers);
    void render();

  private:
    void handle_input();

    bool initialized_ = false;
    gl::Fbo scene_fbo_;
    CameraController camera_;
    GlobeRenderer globe_;
};

} // namespace daedalus::world
