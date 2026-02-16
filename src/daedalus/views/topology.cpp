#include "daedalus/views/topology.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <queue>
#include <unordered_set>
#include <utility>

namespace daedalus::views {

namespace {

struct PathParts {
    std::string module;
    std::string signal;
};

std::optional<PathParts> split_signal_path(const std::string &signal_path) {
    const size_t dot = signal_path.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= signal_path.size()) {
        return std::nullopt;
    }
    return PathParts{
        .module = signal_path.substr(0, dot),
        .signal = signal_path.substr(dot + 1),
    };
}

std::string signal_name_from_path(const std::string &signal_path) {
    const size_t dot = signal_path.find('.');
    if (dot == std::string::npos || dot + 1 >= signal_path.size()) {
        return signal_path;
    }
    return signal_path.substr(dot + 1);
}

uint64_t edge_key(size_t src, size_t dst) {
    return (static_cast<uint64_t>(src) << 32U) | static_cast<uint64_t>(dst);
}

} // namespace

void TopologyGraph::build_from_schema(const protocol::Schema &schema) {
    clear();
    if (schema.modules.empty()) {
        return;
    }

    nodes_.reserve(schema.modules.size());

    std::unordered_map<std::string, size_t> module_to_index;
    module_to_index.reserve(schema.modules.size());

    std::vector<std::unordered_set<std::string>> wired_signals_by_module;
    wired_signals_by_module.resize(schema.modules.size());

    struct ModulePinState {
        uintptr_t next_pin_index = 0;
        std::unordered_map<std::string, uintptr_t> input_pin_by_signal;
        std::unordered_map<std::string, uintptr_t> output_pin_by_signal;
    };
    std::vector<ModulePinState> pin_state(schema.modules.size());

    for (size_t i = 0; i < schema.modules.size(); ++i) {
        TopologyNode node;
        node.id = kNodeIdBase + i;
        node.module_name = schema.modules[i].name;
        nodes_.push_back(std::move(node));
        module_to_index.emplace(schema.modules[i].name, i);
    }

    auto get_or_create_pin_id = [&](size_t module_index, const std::string &signal_path,
                                    ax::NodeEditor::PinKind kind) -> uintptr_t {
        auto &state = pin_state[module_index];
        auto &signal_map = (kind == ax::NodeEditor::PinKind::Input) ? state.input_pin_by_signal
                                                                    : state.output_pin_by_signal;
        const auto existing = signal_map.find(signal_path);
        if (existing != signal_map.end()) {
            return existing->second;
        }

        const uintptr_t pin_index = state.next_pin_index++;
        const uintptr_t pin_id = kPinIdBase + (module_index * kMaxPinsPerModule) + pin_index;

        TopologyPin pin;
        pin.id = pin_id;
        pin.signal_path = signal_path;
        pin.signal_name = signal_name_from_path(signal_path);
        pin.kind = kind;

        auto &node = nodes_[module_index];
        if (kind == ax::NodeEditor::PinKind::Input) {
            node.input_pins.push_back(pin);
        } else {
            node.output_pins.push_back(pin);
        }

        signal_map.emplace(signal_path, pin_id);
        return pin_id;
    };

    links_.reserve(schema.wiring.size());
    for (const auto &wire : schema.wiring) {
        const auto src = split_signal_path(wire.src);
        const auto dst = split_signal_path(wire.dst);
        if (!src.has_value() || !dst.has_value()) {
            continue;
        }

        const auto src_module_it = module_to_index.find(src->module);
        const auto dst_module_it = module_to_index.find(dst->module);
        if (src_module_it == module_to_index.end() || dst_module_it == module_to_index.end()) {
            continue;
        }

        const size_t src_module_index = src_module_it->second;
        const size_t dst_module_index = dst_module_it->second;

        wired_signals_by_module[src_module_index].insert(src->signal);
        wired_signals_by_module[dst_module_index].insert(dst->signal);

        TopologyLink link;
        link.id = kLinkIdBase + links_.size();
        link.source_signal = wire.src;
        link.dest_signal = wire.dst;
        link.gain = wire.gain;
        link.offset = wire.offset;
        link.source_pin_id =
            get_or_create_pin_id(src_module_index, wire.src, ax::NodeEditor::PinKind::Output);
        link.dest_pin_id =
            get_or_create_pin_id(dst_module_index, wire.dst, ax::NodeEditor::PinKind::Input);
        links_.push_back(std::move(link));
    }

    for (size_t i = 0; i < schema.modules.size(); ++i) {
        size_t wired_count = 0;
        for (const auto &signal : schema.modules[i].signals) {
            if (wired_signals_by_module[i].contains(signal.name)) {
                ++wired_count;
            }
        }
        nodes_[i].unwired_signal_count = schema.modules[i].signals.size() - wired_count;
    }

    compute_layout();
}

void TopologyGraph::compute_layout() {
    if (nodes_.empty()) {
        return;
    }

    std::unordered_map<std::string, size_t> name_to_index;
    name_to_index.reserve(nodes_.size());
    for (size_t i = 0; i < nodes_.size(); ++i) {
        name_to_index.emplace(nodes_[i].module_name, i);
    }

    std::vector<std::vector<size_t>> adjacency(nodes_.size());
    std::vector<int> in_degree(nodes_.size(), 0);
    std::unordered_set<uint64_t> seen_edges;
    seen_edges.reserve(links_.size());

    for (const auto &link : links_) {
        const auto src = split_signal_path(link.source_signal);
        const auto dst = split_signal_path(link.dest_signal);
        if (!src.has_value() || !dst.has_value()) {
            continue;
        }

        const auto src_module_it = name_to_index.find(src->module);
        const auto dst_module_it = name_to_index.find(dst->module);
        if (src_module_it == name_to_index.end() || dst_module_it == name_to_index.end()) {
            continue;
        }

        const size_t src_index = src_module_it->second;
        const size_t dst_index = dst_module_it->second;
        const uint64_t key = edge_key(src_index, dst_index);
        if (seen_edges.contains(key)) {
            continue;
        }

        adjacency[src_index].push_back(dst_index);
        in_degree[dst_index] += 1;
        seen_edges.insert(key);
    }

    std::queue<size_t> queue;
    std::vector<int> layer(nodes_.size(), 0);
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (in_degree[i] == 0) {
            queue.push(i);
        }
    }

    while (!queue.empty()) {
        const size_t node_index = queue.front();
        queue.pop();

        for (const size_t neighbor : adjacency[node_index]) {
            layer[neighbor] = std::max(layer[neighbor], layer[node_index] + 1);
            in_degree[neighbor] -= 1;
            if (in_degree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }

    constexpr float kHorizontalSpacing = 300.0f;
    constexpr float kVerticalSpacing = 200.0f;

    std::map<int, std::vector<size_t>> nodes_by_layer;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        nodes_by_layer[layer[i]].push_back(i);
    }

    for (auto &[layer_index, indices] : nodes_by_layer) {
        const float total_height = static_cast<float>(indices.size() - 1) * kVerticalSpacing;
        const float start_y = -total_height * 0.5f;

        for (size_t i = 0; i < indices.size(); ++i) {
            nodes_[indices[i]].position = {
                static_cast<float>(layer_index) * kHorizontalSpacing,
                start_y + static_cast<float>(i) * kVerticalSpacing,
            };
        }
    }
}

void TopologyGraph::update_subscription(const protocol::SubscribeAck &ack) {
    std::unordered_map<std::string, size_t> index_by_signal;
    index_by_signal.reserve(ack.signals.size());
    for (size_t i = 0; i < ack.signals.size(); ++i) {
        index_by_signal.emplace(ack.signals[i], i);
    }

    for (auto &node : nodes_) {
        for (auto &pin : node.input_pins) {
            pin.signal_index.reset();
            const auto it = index_by_signal.find(pin.signal_path);
            if (it != index_by_signal.end()) {
                pin.signal_index = it->second;
            }
        }
        for (auto &pin : node.output_pins) {
            pin.signal_index.reset();
            const auto it = index_by_signal.find(pin.signal_path);
            if (it != index_by_signal.end()) {
                pin.signal_index = it->second;
            }
        }
    }
}

void TopologyGraph::update_units(const std::unordered_map<std::string, std::string> &units) {
    for (auto &node : nodes_) {
        for (auto &pin : node.input_pins) {
            pin.unit.reset();
            const auto it = units.find(pin.signal_path);
            if (it != units.end()) {
                pin.unit = it->second;
            }
        }
        for (auto &pin : node.output_pins) {
            pin.unit.reset();
            const auto it = units.find(pin.signal_path);
            if (it != units.end()) {
                pin.unit = it->second;
            }
        }
    }
}

void TopologyGraph::clear() {
    nodes_.clear();
    links_.clear();
}

TopologyView::TopologyView() {
    ax::NodeEditor::Config config;
    config.SettingsFile = nullptr;
    context_ = ax::NodeEditor::CreateEditor(&config);
}

TopologyView::~TopologyView() {
    if (context_ != nullptr) {
        ax::NodeEditor::DestroyEditor(context_);
    }
}

void TopologyView::render(const TopologyGraph &graph,
                          const std::map<size_t, data::SignalBuffer> &buffers) {
    render_toolbar();

    if (graph.empty()) {
        ImGui::TextDisabled("No schema received. Waiting for connection...");
        return;
    }

    if (!graph.has_wiring()) {
        ImGui::TextDisabled("No wiring information in schema.");
        ImGui::TextDisabled("Showing modules only.");
        ImGui::Separator();
    }

    if (context_ == nullptr) {
        ImGui::TextDisabled("Topology editor unavailable.");
        return;
    }

    ax::NodeEditor::SetCurrentEditor(context_);
    ax::NodeEditor::Begin("TopologyEditor");

    if (layout_dirty_) {
        apply_layout(graph);
        layout_dirty_ = false;
    }

    for (const auto &node : graph.nodes()) {
        render_node(node, buffers);
    }

    render_links(graph, buffers);
    handle_hover_tooltips(graph, buffers);
    handle_node_context_menu(graph);

    ax::NodeEditor::End();

    if (first_frame_) {
        ax::NodeEditor::NavigateToContent(0.0f);
        first_frame_ = false;
    }
    ax::NodeEditor::SetCurrentEditor(nullptr);
}

void TopologyView::reset() {
    first_frame_ = true;
    layout_dirty_ = true;
}

void TopologyView::apply_layout(const TopologyGraph &graph) {
    if (context_ == nullptr) {
        return;
    }
    for (const auto &node : graph.nodes()) {
        ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(node.id), node.position);
    }
}

void TopologyView::set_inspect_callback(InspectCallback callback) {
    inspect_callback_ = std::move(callback);
}

void TopologyView::render_toolbar() {
    if (ImGui::SmallButton("Reset Layout")) {
        layout_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit to View") && context_ != nullptr) {
        ax::NodeEditor::SetCurrentEditor(context_);
        ax::NodeEditor::NavigateToContent(0.35f);
        ax::NodeEditor::SetCurrentEditor(nullptr);
    }
    ImGui::Separator();
}

void TopologyView::render_node(const TopologyNode &node,
                               const std::map<size_t, data::SignalBuffer> &buffers) {
    ax::NodeEditor::BeginNode(ax::NodeEditor::NodeId(node.id));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.2f, 1.0f));
    ImGui::TextUnformatted(node.module_name.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    for (const auto &pin : node.input_pins) {
        render_pin(pin, buffers);
    }
    for (const auto &pin : node.output_pins) {
        render_pin(pin, buffers);
    }

    if (node.unwired_signal_count > 0) {
        ImGui::TextDisabled("... %zu more", node.unwired_signal_count);
    }

    ax::NodeEditor::EndNode();
}

void TopologyView::render_pin(const TopologyPin &pin,
                              const std::map<size_t, data::SignalBuffer> &buffers) {
    ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(pin.id), pin.kind);
    if (pin.kind == ax::NodeEditor::PinKind::Input) {
        ImGui::Text("-> %s", pin.signal_name.c_str());
    } else {
        ImGui::Text("%s ->", pin.signal_name.c_str());
    }
    ax::NodeEditor::EndPin();

    if (!pin.signal_index.has_value()) {
        return;
    }
    const auto it = buffers.find(pin.signal_index.value());
    if (it == buffers.end() || it->second.empty()) {
        return;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%.4g", it->second.last_value());
}

void TopologyView::render_links(const TopologyGraph &graph,
                                const std::map<size_t, data::SignalBuffer> &buffers) {
    const bool telemetry_active = std::any_of(
        buffers.begin(), buffers.end(), [](const auto &entry) { return !entry.second.empty(); });

    const ImVec4 link_color = ImVec4(0.5f, 0.8f, 0.5f, 1.0f);
    for (const auto &link : graph.links()) {
        ax::NodeEditor::Link(ax::NodeEditor::LinkId(link.id),
                             ax::NodeEditor::PinId(link.source_pin_id),
                             ax::NodeEditor::PinId(link.dest_pin_id), link_color, 2.0f);
        if (telemetry_active) {
            ax::NodeEditor::Flow(ax::NodeEditor::LinkId(link.id));
        }
    }
}

void TopologyView::handle_hover_tooltips(const TopologyGraph &graph,
                                         const std::map<size_t, data::SignalBuffer> &buffers) {
    auto find_pin_by_id = [&](uintptr_t pin_id) -> const TopologyPin * {
        for (const auto &node : graph.nodes()) {
            for (const auto &pin : node.input_pins) {
                if (pin.id == pin_id) {
                    return &pin;
                }
            }
            for (const auto &pin : node.output_pins) {
                if (pin.id == pin_id) {
                    return &pin;
                }
            }
        }
        return nullptr;
    };

    auto hovered_link = ax::NodeEditor::GetHoveredLink();
    if (hovered_link) {
        for (const auto &link : graph.links()) {
            if (link.id != hovered_link.Get()) {
                continue;
            }

            const TopologyPin *source_pin = find_pin_by_id(link.source_pin_id);
            ax::NodeEditor::Suspend();
            ImGui::BeginTooltip();
            ImGui::Text("%s -> %s", link.source_signal.c_str(), link.dest_signal.c_str());
            if (source_pin != nullptr && source_pin->signal_index.has_value()) {
                const auto it = buffers.find(source_pin->signal_index.value());
                if (it != buffers.end() && !it->second.empty()) {
                    ImGui::Separator();
                    ImGui::Text("Value: %.6g", it->second.last_value());
                    if (source_pin->unit.has_value()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", source_pin->unit->c_str());
                    }
                }
            }
            if (link.gain != 1.0 || link.offset != 0.0) {
                ImGui::Separator();
                ImGui::TextDisabled("Gain: %.4g  Offset: %.4g", link.gain, link.offset);
            }
            ImGui::EndTooltip();
            ax::NodeEditor::Resume();
            break;
        }
    }

    auto hovered_pin = ax::NodeEditor::GetHoveredPin();
    if (!hovered_pin) {
        return;
    }

    const TopologyPin *pin = find_pin_by_id(hovered_pin.Get());
    if (pin == nullptr) {
        return;
    }

    ax::NodeEditor::Suspend();
    ImGui::BeginTooltip();
    ImGui::Text("%s", pin->signal_path.c_str());
    if (pin->signal_index.has_value()) {
        const auto it = buffers.find(pin->signal_index.value());
        if (it != buffers.end() && !it->second.empty()) {
            ImGui::SameLine();
            ImGui::Text("= %.6g", it->second.last_value());
            if (pin->unit.has_value()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", pin->unit->c_str());
            }
        }
    }
    ImGui::EndTooltip();
    ax::NodeEditor::Resume();
}

void TopologyView::handle_node_context_menu(const TopologyGraph &graph) {
    if (context_ == nullptr) {
        return;
    }

    ax::NodeEditor::Suspend();
    ax::NodeEditor::NodeId context_node_id;
    if (ax::NodeEditor::ShowNodeContextMenu(&context_node_id)) {
        context_node_id_ = context_node_id;
        ImGui::OpenPopup("TopologyNodeContextMenu");
    }

    if (ImGui::BeginPopup("TopologyNodeContextMenu")) {
        std::string module_name = "Unknown";
        for (const auto &node : graph.nodes()) {
            if (node.id == context_node_id_.Get()) {
                module_name = node.module_name;
                break;
            }
        }

        ImGui::TextDisabled("Module: %s", module_name.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Inspect (requires Hermes support)") && inspect_callback_) {
            inspect_callback_(module_name);
        }
        if (ImGui::MenuItem("Navigate to Content")) {
            ax::NodeEditor::NavigateToContent(0.35f);
        }
        ImGui::EndPopup();
    }
    ax::NodeEditor::Resume();
}

} // namespace daedalus::views
