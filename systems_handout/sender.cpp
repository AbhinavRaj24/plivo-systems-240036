// Low-delay sender using an immediate (32,18) GF(256) erasure code.
//
// Each 160-byte source payload plus its two-byte sequence check is split into
// eighteen 9-byte symbols. A Vandermonde code produces 32 fragments, any 18
// of which reconstruct the frame. Each relay datagram is exactly 10 bytes, so
// 32 fragments consume exactly 2.0x the raw 160-byte stream.

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.hpp"

namespace {

constexpr std::size_t kMaxDatagramBytes = 2048;

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

void send_encoded_frame(int fd, const sockaddr_in& relay,
                        const protocol::HarnessFrame& frame) {
    std::array<std::uint8_t, protocol::kCodedSourceBytes> source{};
    std::memcpy(source.data(), frame.payload.data(), protocol::kPayloadBytes);
    source[protocol::kPayloadBytes] =
        static_cast<std::uint8_t>((frame.sequence >> 8U) & 0xffU);
    source[protocol::kPayloadBytes + 1U] =
        static_cast<std::uint8_t>(frame.sequence & 0xffU);

    const std::uint8_t sequence_tag =
        static_cast<std::uint8_t>(frame.sequence % protocol::kSequenceModulo);
    for (std::size_t fragment_index = 0;
         fragment_index < protocol::kCodedSymbolCount; ++fragment_index) {
        std::array<std::uint8_t, protocol::kFragmentBytes> fragment{};
        fragment[0] = static_cast<std::uint8_t>(
            (sequence_tag << 5U) | static_cast<std::uint8_t>(fragment_index));

        for (std::size_t data_symbol = 0;
             data_symbol < protocol::kDataSymbolCount; ++data_symbol) {
            const std::uint8_t coefficient =
                protocol::generator_coefficient(fragment_index, data_symbol);
            for (std::size_t byte = 0; byte < protocol::kSymbolBytes; ++byte) {
                fragment[protocol::kFragmentHeaderBytes + byte] ^=
                    protocol::gf_multiply(
                        coefficient,
                        source[data_symbol * protocol::kSymbolBytes + byte]);
            }
        }

        if (sendto(fd, fragment.data(), fragment.size(), 0,
                   reinterpret_cast<const sockaddr*>(&relay), sizeof(relay)) < 0) {
            std::perror("sendto relay");
        }
    }
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
        if (frame) {
            send_encoded_frame(output_fd, relay, *frame);
        }
    }

    close(output_fd);
    close(input_fd);
    return EXIT_FAILURE;
}
