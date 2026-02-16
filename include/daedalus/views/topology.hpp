#pragma once

#include "daedalus/data/signal_buffer.hpp"
#include "daedalus/protocol/schema.hpp"

#include <imgui-node-editor/imgui_node_editor.h>
#include <imgui.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace daedalus::views {

struct TopologyPin {
    uintptr_t id = 0;
    std::string signal_path;
    std::string signal_name;
    ax::NodeEditor::PinKind kind = ax::NodeEditor::PinKind::Input;
    std::optional<size_t> signal_index;
    std::optional<std::string> unit;
};

struct TopologyNode {
    uintptr_t id = 0;
    std::string module_name;
    ImVec2 position = {0.0f, 0.0f};
    std::vector<TopologyPin> input_pins;
    std::vector<TopologyPin> output_pins;
    size_t unwired_signal_count = 0;
    size_t total_signal_count = 0;
};

struct TopologyLink {
    uintptr_t id = 0;
    uintptr_t source_pin_id = 0;
    uintptr_t dest_pin_id = 0;
    std::string source_signal;
    std::string dest_signal;
    double gain = 1.0;
    double offset = 0.0;
};

class TopologyGraph {
  public:
    void build_from_schema(const protocol::Schema &schema);
    void compute_layout();
    void update_subscription(const protocol::SubscribeAck &ack);
    void update_units(const std::unordered_map<std::string, std::string> &units);
    void clear();

    [[nodiscard]] const std::vector<TopologyNode> &nodes() const { return nodes_; }
    [[nodiscard]] const std::vector<TopologyLink> &links() const { return links_; }
    [[nodiscard]] bool empty() const { return nodes_.empty(); }
    [[nodiscard]] bool has_wiring() const { return !links_.empty(); }

    static constexpr uintptr_t kNodeIdBase = 1;
    static constexpr uintptr_t kPinIdBase = 10001;
    static constexpr uintptr_t kMaxPinsPerModule = 256;
    static constexpr uintptr_t kLinkIdBase = 100001;

  private:
    std::vector<TopologyNode> nodes_;
    std::vector<TopologyLink> links_;
};

class TopologyView {
  public:
    using InspectCallback = std::function<void(const std::string &module_name)>;

    TopologyView();
    ~TopologyView();

    TopologyView(const TopologyView &) = delete;
    TopologyView &operator=(const TopologyView &) = delete;

    void render(const TopologyGraph &graph, const std::map<size_t, data::SignalBuffer> &buffers);
    void reset();
    void apply_layout(const TopologyGraph &graph);
    void set_inspect_callback(InspectCallback callback);

  private:
    void render_toolbar();
    void render_node(const TopologyNode &node, const std::map<size_t, data::SignalBuffer> &buffers);
    void render_pin(const TopologyPin &pin, const std::map<size_t, data::SignalBuffer> &buffers);
    void render_links(const TopologyGraph &graph,
                      const std::map<size_t, data::SignalBuffer> &buffers);
    void render_wire_labels(const TopologyGraph &graph,
                            const std::map<size_t, data::SignalBuffer> &buffers);
    void handle_hover_tooltips(const TopologyGraph &graph,
                               const std::map<size_t, data::SignalBuffer> &buffers);
    void handle_node_context_menu(const TopologyGraph &graph);

    ax::NodeEditor::EditorContext *context_ = nullptr;
    bool first_frame_ = true;
    bool layout_dirty_ = true;
    ax::NodeEditor::NodeId context_node_id_;
    InspectCallback inspect_callback_;
};

} // namespace daedalus::views
