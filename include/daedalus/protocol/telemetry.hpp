#pragma once

#include <cstdint>
#include <cstring>
#include <span>

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

/// Magic bytes: "HERT" in little-endian = 0x48455254
inline constexpr uint32_t kTelemetryMagic = 0x48455254;

/// Decode a binary telemetry frame.
/// Returns true if the frame is valid (correct magic, sufficient length).
/// On success, populates hdr and values.
inline bool decode_frame(const uint8_t *data, size_t len, TelemetryHeader &hdr,
                         std::span<const double> &values) {
    if (len < sizeof(TelemetryHeader)) {
        return false;
    }

    std::memcpy(&hdr, data, sizeof(TelemetryHeader));

    if (hdr.magic != kTelemetryMagic) {
        return false;
    }

    const size_t expected_payload = hdr.count * sizeof(double);
    if (len < sizeof(TelemetryHeader) + expected_payload) {
        return false;
    }

    values = std::span<const double>(
        reinterpret_cast<const double *>(data + sizeof(TelemetryHeader)), hdr.count);

    return true;
}

} // namespace daedalus::protocol
