#pragma once

#include "daedalus/protocol/schema.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace daedalus::data {

/// A node in the hierarchical signal tree.
/// Internal nodes represent namespace segments (e.g., "vehicle", "position").
/// Leaf nodes represent actual signals (e.g., "x" with full_path "vehicle.position.x").
struct SignalTreeNode {
    std::string name;
    std::string full_path;
    bool is_leaf = false;
    std::optional<size_t> signal_index; // set after subscribe ack
    std::vector<std::unique_ptr<SignalTreeNode>> children;

    /// Find a direct child by name.
    SignalTreeNode *find_child(const std::string &child_name) const;
};

/// Hierarchical signal namespace built from the Hermes schema.
/// Thread safety: render thread only.
class SignalTree {
  public:
    /// Build tree from a parsed schema.
    /// Signal paths are "module.signal_name" (e.g., "vehicle.position.x").
    void build_from_schema(const protocol::Schema &schema);

    /// Assign subscription indices to leaf nodes based on subscribe ack.
    /// The ack's signal list order = binary telemetry payload order.
    void update_subscription(const protocol::SubscribeAck &ack);

    /// Access the root node.
    [[nodiscard]] const SignalTreeNode &root() const { return root_; }

    /// Lookup a node by its full path (e.g., "vehicle.position.x").
    /// Returns nullptr if not found.
    [[nodiscard]] const SignalTreeNode *find(const std::string &path) const;

    /// Get all leaf signal paths.
    [[nodiscard]] std::vector<std::string> all_signals() const;

    /// Clear the tree.
    void clear();

  private:
    /// Ensure a path of nodes exists, creating intermediate nodes as needed.
    /// Returns the final (deepest) node.
    SignalTreeNode &ensure_path(const std::string &full_path);

    SignalTreeNode root_;
    std::unordered_map<std::string, SignalTreeNode *> path_index_;
};

} // namespace daedalus::data
