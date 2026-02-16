#include "daedalus/protocol/schema.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace daedalus::protocol {

namespace {

bool starts_with(const std::string &value, const std::string &prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::optional<std::pair<std::string, std::string>>
split_by_known_modules(const std::string &signal_path,
                       const std::unordered_map<std::string, size_t> &module_to_index) {
    size_t best_len = 0;
    std::string best_module;

    for (const auto &[module_name, index] : module_to_index) {
        (void)index;
        const std::string prefix = module_name + ".";
        if (starts_with(signal_path, prefix) && module_name.size() > best_len) {
            best_len = module_name.size();
            best_module = module_name;
        }
    }

    if (best_module.empty() || signal_path.size() <= best_len + 1) {
        return std::nullopt;
    }

    return std::make_pair(best_module, signal_path.substr(best_len + 1));
}

void append_component_signals(ModuleInfo &module,
                              const std::vector<IntrospectionSignalInfo> &signals,
                              std::unordered_map<std::string, size_t> &signal_index_by_name) {
    const std::string module_prefix = module.name + ".";
    for (const auto &signal : signals) {
        if (signal.name.empty()) {
            continue;
        }

        std::string local_name = signal.name;
        if (starts_with(local_name, module_prefix)) {
            local_name = local_name.substr(module_prefix.size());
        }

        if (local_name.empty()) {
            continue;
        }

        const auto existing = signal_index_by_name.find(local_name);
        if (existing == signal_index_by_name.end()) {
            SignalInfo schema_signal;
            schema_signal.name = local_name;
            schema_signal.type = "f64";
            schema_signal.unit = signal.unit;
            signal_index_by_name.emplace(local_name, module.signals.size());
            module.signals.push_back(std::move(schema_signal));
            continue;
        }

        if (!module.signals[existing->second].unit.has_value() && signal.unit.has_value()) {
            module.signals[existing->second].unit = signal.unit;
        }
    }
}

std::vector<IntrospectionSignalInfo>
parse_introspection_signals(const nlohmann::json &component_json, const char *field_name) {
    std::vector<IntrospectionSignalInfo> result;
    if (!component_json.contains(field_name) || !component_json[field_name].is_array()) {
        return result;
    }

    for (const auto &signal_json : component_json[field_name]) {
        if (!signal_json.is_object()) {
            continue;
        }
        if (!signal_json.contains("name") || !signal_json["name"].is_string()) {
            continue;
        }

        IntrospectionSignalInfo signal;
        signal.name = signal_json["name"].get<std::string>();
        if (signal_json.contains("unit") && signal_json["unit"].is_string()) {
            signal.unit = signal_json["unit"].get<std::string>();
        }
        result.push_back(std::move(signal));
    }
    return result;
}

} // namespace

Schema parse_schema(const nlohmann::json &msg) {
    if (!msg.contains("type") || msg["type"] != "schema") {
        throw std::runtime_error("Expected message type 'schema'");
    }
    if (!msg.contains("modules") || !msg["modules"].is_object()) {
        throw std::runtime_error("Schema missing 'modules' object");
    }

    Schema schema;
    for (auto &[mod_name, mod_data] : msg["modules"].items()) {
        ModuleInfo mod;
        mod.name = mod_name;

        if (!mod_data.contains("signals") || !mod_data["signals"].is_array()) {
            throw std::runtime_error("Module '" + mod_name + "' missing 'signals' array");
        }

        for (auto &sig_json : mod_data["signals"]) {
            SignalInfo sig;
            if (!sig_json.contains("name") || !sig_json["name"].is_string()) {
                throw std::runtime_error("Signal missing 'name' in module '" + mod_name + "'");
            }
            sig.name = sig_json["name"].get<std::string>();

            if (!sig_json.contains("type") || !sig_json["type"].is_string()) {
                throw std::runtime_error("Signal '" + sig.name + "' missing 'type' in module '" +
                                         mod_name + "'");
            }
            sig.type = sig_json["type"].get<std::string>();

            if (sig_json.contains("unit") && sig_json["unit"].is_string()) {
                sig.unit = sig_json["unit"].get<std::string>();
            }

            mod.signals.push_back(std::move(sig));
        }

        if (mod_data.contains("module_type") && mod_data["module_type"].is_string()) {
            mod.module_type = mod_data["module_type"].get<std::string>();
        }
        if (mod_data.contains("supports_introspection") &&
            mod_data["supports_introspection"].is_boolean()) {
            mod.supports_introspection = mod_data["supports_introspection"].get<bool>();
        }
        if (mod_data.contains("component_count") &&
            (mod_data["component_count"].is_number_unsigned() ||
             mod_data["component_count"].is_number_integer())) {
            const auto value = mod_data["component_count"].get<int64_t>();
            if (value >= 0) {
                mod.component_count = static_cast<size_t>(value);
            }
        }

        schema.modules.push_back(std::move(mod));
    }

    if (msg.contains("wiring") && msg["wiring"].is_array()) {
        for (const auto &wire_json : msg["wiring"]) {
            if (!wire_json.is_object()) {
                continue;
            }
            if (!wire_json.contains("src") || !wire_json["src"].is_string()) {
                continue;
            }
            if (!wire_json.contains("dst") || !wire_json["dst"].is_string()) {
                continue;
            }

            WireInfo wire;
            wire.src = wire_json["src"].get<std::string>();
            wire.dst = wire_json["dst"].get<std::string>();
            if (wire_json.contains("gain") && wire_json["gain"].is_number()) {
                wire.gain = wire_json["gain"].get<double>();
            }
            if (wire_json.contains("offset") && wire_json["offset"].is_number()) {
                wire.offset = wire_json["offset"].get<double>();
            }
            schema.wiring.push_back(std::move(wire));
        }
    }

    return schema;
}

SubscribeAck parse_subscribe_ack(const nlohmann::json &msg) {
    if (!msg.contains("type") || msg["type"] != "ack") {
        throw std::runtime_error("Expected message type 'ack'");
    }
    if (!msg.contains("action") || msg["action"] != "subscribe") {
        throw std::runtime_error("Expected action 'subscribe'");
    }

    SubscribeAck ack;

    if (!msg.contains("count") || !msg["count"].is_number_unsigned()) {
        throw std::runtime_error("Subscribe ack missing 'count'");
    }
    ack.count = msg["count"].get<uint32_t>();

    if (!msg.contains("signals") || !msg["signals"].is_array()) {
        throw std::runtime_error("Subscribe ack missing 'signals' array");
    }

    for (auto &sig : msg["signals"]) {
        ack.signals.push_back(sig.get<std::string>());
    }

    return ack;
}

IntrospectAck parse_introspect_ack(const nlohmann::json &msg) {
    if (!msg.contains("type") || msg["type"] != "ack") {
        throw std::runtime_error("Expected message type 'ack'");
    }
    if (!msg.contains("action") || msg["action"] != "introspect") {
        throw std::runtime_error("Expected action 'introspect'");
    }
    if (!msg.contains("module") || !msg["module"].is_string()) {
        throw std::runtime_error("Introspect ack missing 'module'");
    }

    IntrospectAck ack;
    ack.module = msg["module"].get<std::string>();
    ack.summary = nlohmann::json::object();

    if (msg.contains("module_type") && msg["module_type"].is_string()) {
        ack.module_type = msg["module_type"].get<std::string>();
    }

    if (msg.contains("components") && msg["components"].is_array()) {
        for (const auto &component_json : msg["components"]) {
            if (!component_json.is_object()) {
                continue;
            }
            if (!component_json.contains("name") || !component_json["name"].is_string()) {
                continue;
            }

            IntrospectionComponentInfo component;
            component.name = component_json["name"].get<std::string>();
            if (component_json.contains("type") && component_json["type"].is_string()) {
                component.type = component_json["type"].get<std::string>();
            }

            component.inputs = parse_introspection_signals(component_json, "inputs");
            component.outputs = parse_introspection_signals(component_json, "outputs");
            component.parameters = parse_introspection_signals(component_json, "parameters");
            component.config = parse_introspection_signals(component_json, "config");
            ack.components.push_back(std::move(component));
        }
    }

    if (msg.contains("internal_wiring") && msg["internal_wiring"].is_array()) {
        for (const auto &wire_json : msg["internal_wiring"]) {
            if (!wire_json.is_object()) {
                continue;
            }
            if (!wire_json.contains("src") || !wire_json["src"].is_string()) {
                continue;
            }
            if (!wire_json.contains("dst") || !wire_json["dst"].is_string()) {
                continue;
            }

            WireInfo wire;
            wire.src = wire_json["src"].get<std::string>();
            wire.dst = wire_json["dst"].get<std::string>();
            ack.internal_wiring.push_back(std::move(wire));
        }
    }

    if (msg.contains("execution_order") && msg["execution_order"].is_array()) {
        for (const auto &entry : msg["execution_order"]) {
            if (entry.is_string()) {
                ack.execution_order.push_back(entry.get<std::string>());
            }
        }
    }

    if (msg.contains("summary") && msg["summary"].is_object()) {
        ack.summary = msg["summary"];
    }

    return ack;
}

Schema make_schema_from_introspection(const IntrospectAck &ack) {
    Schema schema;
    schema.modules.reserve(ack.components.size());
    schema.wiring.reserve(ack.internal_wiring.size());

    for (const auto &component : ack.components) {
        ModuleInfo module;
        module.name = component.name;
        module.module_type = ack.module_type;
        module.supports_introspection = true;
        module.component_count = ack.components.size();

        std::unordered_map<std::string, size_t> signal_index_by_name;
        append_component_signals(module, component.inputs, signal_index_by_name);
        append_component_signals(module, component.outputs, signal_index_by_name);
        append_component_signals(module, component.parameters, signal_index_by_name);
        append_component_signals(module, component.config, signal_index_by_name);

        schema.modules.push_back(std::move(module));
    }

    for (const auto &wire : ack.internal_wiring) {
        if (wire.src.empty() || wire.dst.empty()) {
            continue;
        }
        schema.wiring.push_back(wire);
    }

    std::unordered_map<std::string, size_t> module_to_index;
    module_to_index.reserve(schema.modules.size());
    for (size_t i = 0; i < schema.modules.size(); ++i) {
        module_to_index.emplace(schema.modules[i].name, i);
    }

    auto add_signal_from_wire = [&](const std::string &signal_path) {
        const auto parsed = split_by_known_modules(signal_path, module_to_index);
        if (!parsed.has_value()) {
            return;
        }

        const auto module_it = module_to_index.find(parsed->first);
        if (module_it == module_to_index.end()) {
            return;
        }

        auto &module = schema.modules[module_it->second];
        const auto exists =
            std::find_if(module.signals.begin(), module.signals.end(),
                         [&](const SignalInfo &signal) { return signal.name == parsed->second; });
        if (exists != module.signals.end()) {
            return;
        }

        module.signals.push_back(SignalInfo{
            .name = parsed->second,
            .type = "f64",
            .unit = std::nullopt,
        });
    };

    for (const auto &wire : schema.wiring) {
        add_signal_from_wire(wire.src);
        add_signal_from_wire(wire.dst);
    }

    return schema;
}

} // namespace daedalus::protocol
