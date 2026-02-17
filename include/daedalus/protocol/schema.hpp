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
    std::optional<std::string> module_type;
    std::optional<bool> supports_introspection;
    std::optional<size_t> component_count;
    std::optional<size_t> edge_count;
};

struct WireInfo {
    std::string src;
    std::string dst;
    double gain = 1.0;
    double offset = 0.0;
    std::string kind = "route";
};

struct Schema {
    std::vector<ModuleInfo> modules;
    std::vector<WireInfo> wiring;
};

struct SubscribeAck {
    uint32_t count;
    std::vector<std::string> signals;
};

struct IntrospectionSignalInfo {
    std::string name;
    std::optional<std::string> unit;
};

struct IntrospectionComponentInfo {
    std::string name;
    std::string type;
    std::vector<IntrospectionSignalInfo> inputs;
    std::vector<IntrospectionSignalInfo> outputs;
    std::vector<IntrospectionSignalInfo> parameters;
    std::vector<IntrospectionSignalInfo> config;
};

struct IntrospectAck {
    std::string module;
    std::optional<std::string> module_type;
    std::vector<IntrospectionComponentInfo> components;
    std::vector<WireInfo> edges;
    std::vector<WireInfo> internal_wiring;
    std::vector<std::string> execution_order;
    nlohmann::json summary;
};

/// Parse a schema JSON message into a Schema struct.
/// Expects: {"type": "schema", "modules": {"name": {"signals": [...]}}}
Schema parse_schema(const nlohmann::json &msg);

/// Parse a subscribe acknowledgment message.
/// Expects: {"type": "ack", "action": "subscribe", "count": N, "signals": [...]}
/// The signal order in the ack defines binary telemetry payload layout.
SubscribeAck parse_subscribe_ack(const nlohmann::json &msg);

/// Parse an introspection acknowledgment message.
/// Expects: {"type":"ack","action":"introspect","module":"...","components":[...],...}
IntrospectAck parse_introspect_ack(const nlohmann::json &msg);

/// Convert an introspection payload into a topology-compatible schema.
Schema make_schema_from_introspection(const IntrospectAck &ack);

} // namespace daedalus::protocol
