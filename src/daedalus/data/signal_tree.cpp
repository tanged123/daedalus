#include "daedalus/data/signal_tree.hpp"

#include <algorithm>
#include <sstream>

namespace daedalus::data {

SignalTreeNode *SignalTreeNode::find_child(const std::string &child_name) const {
    for (auto &child : children) {
        if (child->name == child_name) {
            return child.get();
        }
    }
    return nullptr;
}

void SignalTree::build_from_schema(const protocol::Schema &schema) {
    clear();
    root_.name = "<root>";

    for (auto &mod : schema.modules) {
        for (auto &sig : mod.signals) {
            // Full path: "module.signal_name"
            std::string full_path = mod.name + "." + sig.name;
            auto &node = ensure_path(full_path);
            node.is_leaf = true;
        }
    }
}

void SignalTree::update_subscription(const protocol::SubscribeAck &ack) {
    for (size_t i = 0; i < ack.signals.size(); ++i) {
        auto it = path_index_.find(ack.signals[i]);
        if (it != path_index_.end()) {
            it->second->signal_index = i;
        }
    }
}

const SignalTreeNode *SignalTree::find(const std::string &path) const {
    auto it = path_index_.find(path);
    if (it != path_index_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> SignalTree::all_signals() const {
    std::vector<std::string> result;
    for (auto &[path, node] : path_index_) {
        if (node->is_leaf) {
            result.push_back(path);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void SignalTree::clear() {
    root_.children.clear();
    root_.full_path.clear();
    root_.is_leaf = false;
    root_.signal_index.reset();
    path_index_.clear();
}

SignalTreeNode &SignalTree::ensure_path(const std::string &full_path) {
    SignalTreeNode *current = &root_;
    std::istringstream stream(full_path);
    std::string segment;
    std::string built_path;

    while (std::getline(stream, segment, '.')) {
        if (!built_path.empty()) {
            built_path += ".";
        }
        built_path += segment;

        auto *child = current->find_child(segment);
        if (child == nullptr) {
            auto new_node = std::make_unique<SignalTreeNode>();
            new_node->name = segment;
            new_node->full_path = built_path;
            child = new_node.get();
            current->children.push_back(std::move(new_node));
        }
        path_index_[built_path] = child;
        current = child;
    }

    return *current;
}

} // namespace daedalus::data
