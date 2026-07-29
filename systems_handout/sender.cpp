// C++ sender using version-1 DATA packets and pairwise XOR parity.
//
// The harness sends fixed-format frames to port 47010.  Each valid frame is
// sent immediately as a DATA packet for relay port 47001.  Once both members
// of an even/odd pair are available, their XOR is sent as a PARITY packet.

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.hpp"

namespace {

constexpr std::size_t kMaxDatagramBytes = 2048;

bool send_packet(int fd, const sockaddr_in& destination, const std::uint8_t* data,
                 std::size_t size) {
    if (sendto(fd, data, size, 0, reinterpret_cast<const sockaddr*>(&destination),
               sizeof(destination)) < 0) {
        std::perror("sendto");
        return false;
    }
    return true;
}

sockaddr_in loopback_address(unsigned short port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

int make_bound_socket(unsigned short port) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return -1;
    }

    const sockaddr_in address = loopback_address(port);
    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        std::perror("bind");
        close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

int main() {
    const int input_fd = make_bound_socket(47010);
    if (input_fd < 0) {
        return EXIT_FAILURE;
    }

    const int output_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (output_fd < 0) {
        std::perror("socket");
        close(input_fd);
        return EXIT_FAILURE;
    }

    const sockaddr_in relay = loopback_address(47001);
    unsigned char buffer[kMaxDatagramBytes];
    std::array<std::uint8_t, protocol::kPayloadBytes> first_payload{};
    std::uint32_t pending_pair_start = 0;
    bool have_first_payload = false;

    for (;;) {
        const ssize_t received = recvfrom(input_fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("recvfrom");
            break;
        }

        const auto frame = protocol::parse_harness_frame(
            buffer, static_cast<std::size_t>(received));
        if (!frame) {
            continue;
        }

        const auto packet = protocol::make_media_packet(
            protocol::PacketType::Data, protocol::FecScheme::Xor,
            protocol::GroupSizeCode::Two, frame->sequence, frame->payload.data());
        send_packet(output_fd, relay, packet.data(), packet.size());

        const std::uint32_t pair_start = frame->sequence & ~std::uint32_t{1};
        if ((frame->sequence & 1U) == 0U) {
            first_payload = frame->payload;
            pending_pair_start = pair_start;
            have_first_payload = true;
            continue;
        }

        if (have_first_payload && pending_pair_start == pair_start) {
            std::array<std::uint8_t, protocol::kPayloadBytes> parity_payload{};
            protocol::xor_payload(parity_payload.data(), first_payload.data(),
                                  frame->payload.data());
            const auto parity_packet = protocol::make_media_packet(
                protocol::PacketType::Parity, protocol::FecScheme::Xor,
                protocol::GroupSizeCode::Two, pair_start, parity_payload.data());
            send_packet(output_fd, relay, parity_packet.data(), parity_packet.size());
        }
        have_first_payload = false;
    }

    close(output_fd);
    close(input_fd);
    return EXIT_FAILURE;
}
