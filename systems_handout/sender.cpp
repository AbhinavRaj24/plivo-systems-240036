// Low-delay UDP sender using immediate duplication plus sparse XOR recovery.
//
// The relay wire format is the same 164-byte sequence + payload format used by
// the harness for DATA. Every frame is sent immediately. In each 20-frame
// block, 18 frames are duplicated and the first two share one XOR packet. The
// XOR packet marks its pair-start identifier with the high bit. This protects
// all frames while keeping byte overhead at or below 1.99875x.

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.hpp"

namespace {

constexpr std::size_t kMaxDatagramBytes = 2048;
constexpr std::uint32_t kRedundancyPeriod = 20;
constexpr std::uint32_t kParityFlag = std::uint32_t{1} << 31U;

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

bool send_frame(int fd, const sockaddr_in& relay, const std::uint8_t* data,
                std::size_t size) {
    const ssize_t sent =
        sendto(fd, data, size, 0, reinterpret_cast<const sockaddr*>(&relay), sizeof(relay));
    if (sent < 0) {
        std::perror("sendto relay");
        return false;
    }
    return static_cast<std::size_t>(sent) == size;
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
    std::uint8_t buffer[kMaxDatagramBytes];
    std::array<std::uint8_t, protocol::kPayloadBytes> block_first_payload{};
    std::uint32_t block_first_sequence = 0;
    bool have_block_first = false;

    for (;;) {
        const ssize_t received = recvfrom(input_fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("recvfrom source");
            break;
        }

        const auto frame =
            protocol::parse_harness_frame(buffer, static_cast<std::size_t>(received));
        if (!frame) {
            continue;
        }

        send_frame(output_fd, relay, buffer, protocol::kHarnessFrameBytes);

        const std::uint32_t position = frame->sequence % kRedundancyPeriod;
        if (position == 0U) {
            block_first_payload = frame->payload;
            block_first_sequence = frame->sequence;
            have_block_first = true;
        } else if (position == 1U && have_block_first &&
                   block_first_sequence + 1U == frame->sequence) {
            std::array<std::uint8_t, protocol::kHarnessFrameBytes> parity{};
            protocol::write_u32_be(parity.data(), kParityFlag | block_first_sequence);
            protocol::xor_payload(parity.data() + protocol::kHarnessHeaderBytes,
                                  block_first_payload.data(), frame->payload.data());
            send_frame(output_fd, relay, parity.data(), parity.size());
            have_block_first = false;
        } else {
            send_frame(output_fd, relay, buffer, protocol::kHarnessFrameBytes);
        }
    }

    close(output_fd);
    close(input_fd);
    return EXIT_FAILURE;
}
