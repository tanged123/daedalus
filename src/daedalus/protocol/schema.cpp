#include "daedalus/protocol/schema.hpp"

namespace daedalus::protocol {

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

        schema.modules.push_back(std::move(mod));
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

} // namespace daedalus::protocol
