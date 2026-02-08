#include "daedalus/protocol/telemetry.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

using namespace daedalus::protocol;

namespace {

// Helper: build a valid binary telemetry frame.
std::vector<uint8_t> make_frame(uint64_t frame_num, double time,
                                const std::vector<double> &values) {
    TelemetryHeader hdr{};
    hdr.magic = kTelemetryMagic;
    hdr.frame = frame_num;
    hdr.time = time;
    hdr.count = static_cast<uint32_t>(values.size());

    std::vector<uint8_t> buf(sizeof(TelemetryHeader) + values.size() * sizeof(double));
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    std::memcpy(buf.data() + sizeof(hdr), values.data(), values.size() * sizeof(double));
    return buf;
}

} // namespace

TEST(TelemetryDecoder, ValidFrame) {
    std::vector<double> vals = {1.0, 2.0, 3.0, 4.0};
    auto buf = make_frame(42, 1.5, vals);

    TelemetryHeader hdr{};
    std::span<const double> values;
    ASSERT_TRUE(decode_frame(buf.data(), buf.size(), hdr, values));

    EXPECT_EQ(hdr.magic, kTelemetryMagic);
    EXPECT_EQ(hdr.frame, 42u);
    EXPECT_DOUBLE_EQ(hdr.time, 1.5);
    EXPECT_EQ(hdr.count, 4u);
    ASSERT_EQ(values.size(), 4u);
    EXPECT_DOUBLE_EQ(values[0], 1.0);
    EXPECT_DOUBLE_EQ(values[1], 2.0);
    EXPECT_DOUBLE_EQ(values[2], 3.0);
    EXPECT_DOUBLE_EQ(values[3], 4.0);
}

TEST(TelemetryDecoder, WrongMagic) {
    auto buf = make_frame(0, 0.0, {1.0});
    // Corrupt magic
    buf[0] = 0xFF;

    TelemetryHeader hdr{};
    std::span<const double> values;
    EXPECT_FALSE(decode_frame(buf.data(), buf.size(), hdr, values));
}

TEST(TelemetryDecoder, TruncatedHeader) {
    std::array<uint8_t, 10> buf{};
    TelemetryHeader hdr{};
    std::span<const double> values;
    EXPECT_FALSE(decode_frame(buf.data(), buf.size(), hdr, values));
}

TEST(TelemetryDecoder, TruncatedPayload) {
    // Header says 4 values but only provide 2 values worth of data
    std::vector<double> vals = {1.0, 2.0};
    auto buf = make_frame(0, 0.0, vals);
    // Patch count to claim 4 values
    TelemetryHeader hdr{};
    std::memcpy(&hdr, buf.data(), sizeof(hdr));
    hdr.count = 4;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    std::span<const double> values;
    EXPECT_FALSE(decode_frame(buf.data(), buf.size(), hdr, values));
}

TEST(TelemetryDecoder, ZeroSignals) {
    auto buf = make_frame(100, 5.0, {});

    TelemetryHeader hdr{};
    std::span<const double> values;
    ASSERT_TRUE(decode_frame(buf.data(), buf.size(), hdr, values));

    EXPECT_EQ(hdr.count, 0u);
    EXPECT_EQ(values.size(), 0u);
    EXPECT_EQ(hdr.frame, 100u);
    EXPECT_DOUBLE_EQ(hdr.time, 5.0);
}

TEST(TelemetryDecoder, LargeFrame) {
    std::vector<double> vals(200);
    for (size_t i = 0; i < vals.size(); ++i) {
        vals[i] = static_cast<double>(i) * 0.1;
    }
    auto buf = make_frame(999, 33.3, vals);

    TelemetryHeader hdr{};
    std::span<const double> values;
    ASSERT_TRUE(decode_frame(buf.data(), buf.size(), hdr, values));

    EXPECT_EQ(hdr.count, 200u);
    ASSERT_EQ(values.size(), 200u);
    EXPECT_DOUBLE_EQ(values[0], 0.0);
    EXPECT_DOUBLE_EQ(values[199], 19.9);
}

TEST(TelemetryDecoder, EmptyBuffer) {
    TelemetryHeader hdr{};
    std::span<const double> values;
    EXPECT_FALSE(decode_frame(nullptr, 0, hdr, values));
}
