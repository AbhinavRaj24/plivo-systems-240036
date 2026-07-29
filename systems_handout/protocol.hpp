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
constexpr std::size_t kWireHeaderBytes = 5;
constexpr std::size_t kMediaPacketBytes = kWireHeaderBytes + kPayloadBytes;

constexpr std::uint8_t kProtocolVersion = 1;

enum class PacketType : std::uint8_t {
    Data = 0,
    Parity = 1,
    Nack = 2,
};

enum class FecScheme : std::uint8_t {
    Xor = 0,
};

enum class GroupSizeCode : std::uint8_t {
    Two = 0,
    Four = 1,
    Eight = 2,
};

struct Header {
    PacketType type;
    FecScheme scheme;
    GroupSizeCode group_size;
    std::uint32_t id;
};

struct HarnessFrame {
    std::uint32_t sequence;
    std::array<std::uint8_t, kPayloadBytes> payload;
};

struct ParsedPacket {
    Header header;
    const std::uint8_t* payload;
    std::size_t payload_size;
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

inline std::uint8_t make_control_byte(PacketType type, FecScheme scheme,
                                      GroupSizeCode group_size) {
    return static_cast<std::uint8_t>((kProtocolVersion << 6U) |
                                     (static_cast<std::uint8_t>(type) << 4U) |
                                     (static_cast<std::uint8_t>(scheme) << 2U) |
                                     static_cast<std::uint8_t>(group_size));
}

inline std::optional<Header> parse_header(const std::uint8_t* data, std::size_t size) {
    if (size < kWireHeaderBytes) {
        return std::nullopt;
    }

    const std::uint8_t control = data[0];
    const std::uint8_t version = control >> 6U;
    const std::uint8_t type_bits = (control >> 4U) & 0x03U;
    const std::uint8_t scheme_bits = (control >> 2U) & 0x03U;
    const std::uint8_t group_bits = control & 0x03U;

    if (version != kProtocolVersion || type_bits > static_cast<std::uint8_t>(PacketType::Nack) ||
        scheme_bits != static_cast<std::uint8_t>(FecScheme::Xor) ||
        group_bits > static_cast<std::uint8_t>(GroupSizeCode::Eight)) {
        return std::nullopt;
    }

    return Header{static_cast<PacketType>(type_bits), static_cast<FecScheme>(scheme_bits),
                  static_cast<GroupSizeCode>(group_bits), read_u32_be(data + 1)};
}

inline std::optional<ParsedPacket> parse_packet(const std::uint8_t* data, std::size_t size) {
    const std::optional<Header> header = parse_header(data, size);
    if (!header) {
        return std::nullopt;
    }

    const bool is_media = header->type == PacketType::Data || header->type == PacketType::Parity;
    const std::size_t expected_size = is_media ? kMediaPacketBytes : kWireHeaderBytes;
    if (size != expected_size) {
        return std::nullopt;
    }

    return ParsedPacket{*header, is_media ? data + kWireHeaderBytes : nullptr,
                        is_media ? kPayloadBytes : 0};
}

inline std::array<std::uint8_t, kMediaPacketBytes> make_media_packet(
    PacketType type, FecScheme scheme, GroupSizeCode group_size, std::uint32_t id,
    const std::uint8_t* payload) {
    std::array<std::uint8_t, kMediaPacketBytes> packet{};
    packet[0] = make_control_byte(type, scheme, group_size);
    write_u32_be(packet.data() + 1, id);
    std::memcpy(packet.data() + kWireHeaderBytes, payload, kPayloadBytes);
    return packet;
}

inline void xor_payload(std::uint8_t* output, const std::uint8_t* left,
                        const std::uint8_t* right) {
    for (std::size_t i = 0; i < kPayloadBytes; ++i) {
        output[i] = static_cast<std::uint8_t>(left[i] ^ right[i]);
    }
}

}  // namespace protocol
