#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace daedalus::protocol {

/// Binary telemetry header — 24 bytes, little-endian.
/// Layout: magic(u32) + frame(u64) + time(f64) + count(u32)
/// Packed to match the wire format exactly (no alignment padding).
#pragma pack(push, 1)
struct TelemetryHeader {
    uint32_t magic;
    uint64_t frame;
    double time;
    uint32_t count;
};
#pragma pack(pop)
static_assert(sizeof(TelemetryHeader) == 24);

/// ASCII "HERT" -> integer 0x48455254 (big-endian representation).
/// On little-endian systems, this integer is laid out in memory as bytes
/// 0x54 0x52 0x45 0x48 ("T", "R", "E", "H").
inline constexpr uint32_t kTelemetryMagic = 0x48455254;

/// Decode a binary telemetry frame.
/// Returns true if the frame is valid (correct magic, sufficient length).
/// On success, populates hdr and values.
inline bool decode_frame(const uint8_t *data, size_t len, TelemetryHeader &hdr,
                         std::span<const double> &values, std::vector<double> &value_storage) {
    if (len < sizeof(TelemetryHeader)) {
        return false;
    }

    std::memcpy(&hdr, data, sizeof(TelemetryHeader));

    if (hdr.magic != kTelemetryMagic) {
        return false;
    }

    const size_t payload_bytes = len - sizeof(TelemetryHeader);
    if (hdr.count > payload_bytes / sizeof(double)) {
        return false;
    }
    const size_t expected_payload = hdr.count * sizeof(double);

    value_storage.resize(hdr.count);
    if (expected_payload > 0) {
        std::memcpy(value_storage.data(), data + sizeof(TelemetryHeader), expected_payload);
    }
    values = std::span<const double>(value_storage.data(), hdr.count);

    return true;
}

inline bool decode_frame(const uint8_t *data, size_t len, TelemetryHeader &hdr,
                         std::span<const double> &values) {
    thread_local std::vector<double> value_storage;
    return decode_frame(data, len, hdr, values, value_storage);
}

} // namespace daedalus::protocol
