#pragma once

namespace daedalus {

/// Application entry point and lifecycle management.
/// Initializes windowing, ImGui context, and the Hermes protocol client.
class App {
  public:
    App();
    ~App();

    /// Run the main application loop.
    /// Returns exit code (0 = success).
    int run(int argc, char *argv[]);
};

} // namespace daedalus
