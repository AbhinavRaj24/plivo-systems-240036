#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace protocol {

constexpr std::size_t kPayloadBytes = 160;
constexpr std::size_t kHarnessHeaderBytes = 4;
constexpr std::size_t kHarnessFrameBytes = kHarnessHeaderBytes + kPayloadBytes;

struct HarnessFrame {
    std::uint32_t sequence;
    std::array<std::uint8_t, kPayloadBytes> payload;
};

inline std::uint32_t read_u32_be(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

inline void write_u32_be(std::uint8_t* bytes, std::uint32_t value) {
    bytes[0] = static_cast<std::uint8_t>(value >> 24U);
    bytes[1] = static_cast<std::uint8_t>(value >> 16U);
    bytes[2] = static_cast<std::uint8_t>(value >> 8U);
    bytes[3] = static_cast<std::uint8_t>(value);
}

inline std::optional<HarnessFrame> parse_harness_frame(const std::uint8_t* data,
                                                        std::size_t size) {
    if (size != kHarnessFrameBytes) {
        return std::nullopt;
    }

    HarnessFrame frame{};
    frame.sequence = read_u32_be(data);
    std::memcpy(frame.payload.data(), data + kHarnessHeaderBytes, kPayloadBytes);
    return frame;
}

inline std::array<std::uint8_t, kHarnessFrameBytes> make_harness_frame(
    std::uint32_t sequence, const std::uint8_t* payload) {
    std::array<std::uint8_t, kHarnessFrameBytes> frame{};
    write_u32_be(frame.data(), sequence);
    std::memcpy(frame.data() + kHarnessHeaderBytes, payload, kPayloadBytes);
    return frame;
}

inline void xor_payload(std::uint8_t* output, const std::uint8_t* left,
                        const std::uint8_t* right) {
    for (std::size_t i = 0; i < kPayloadBytes; ++i) {
        output[i] = static_cast<std::uint8_t>(left[i] ^ right[i]);
    }
}

}  // namespace protocol
