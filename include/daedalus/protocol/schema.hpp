#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace daedalus::protocol {

struct SignalInfo {
    std::string name;
    std::string type;
    std::optional<std::string> unit;
};

struct ModuleInfo {
    std::string name;
    std::vector<SignalInfo> signals;
};

struct Schema {
    std::vector<ModuleInfo> modules;
};

struct SubscribeAck {
    uint32_t count;
    std::vector<std::string> signals;
};

/// Parse a schema JSON message into a Schema struct.
/// Expects: {"type": "schema", "modules": {"name": {"signals": [...]}}}
Schema parse_schema(const nlohmann::json &msg);

/// Parse a subscribe acknowledgment message.
/// Expects: {"type": "ack", "action": "subscribe", "count": N, "signals": [...]}
/// The signal order in the ack defines binary telemetry payload layout.
SubscribeAck parse_subscribe_ack(const nlohmann::json &msg);

} // namespace daedalus::protocol
