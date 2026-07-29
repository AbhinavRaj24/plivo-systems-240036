// Low-delay receiver for the immediate (32,18) GF(256) erasure code.
//
// A one-byte fragment header carries a three-bit sequence tag and five-bit
// fragment index. T0 and DELAY_MS identify the unique still-playable sequence
// with that tag. Any 18 distinct fragments are inverted to recover the frame.

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <optional>
#include <sys/socket.h>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>

#include "protocol.hpp"

namespace {

constexpr std::size_t kMaxDatagramBytes = 2048;
constexpr std::size_t kMaxFrameStates = 32;
constexpr std::size_t kMaxRememberedEmissions = 512;
constexpr double kFrameIntervalMs = 20.0;
constexpr double kBoundaryToleranceMs = 0.01;

struct FrameState {
    std::array<std::array<std::uint8_t, protocol::kSymbolBytes>,
               protocol::kCodedSymbolCount>
        fragments{};
    std::array<bool, protocol::kCodedSymbolCount> present{};
    std::size_t count = 0;
};

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

double environment_number(const char* name) {
    const char* text = std::getenv(name);
    if (text == nullptr) {
        std::fprintf(stderr, "%s is required\n", name);
        std::exit(EXIT_FAILURE);
    }
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(value)) {
        std::fprintf(stderr, "%s is invalid\n", name);
        std::exit(EXIT_FAILURE);
    }
    return value;
}

double epoch_seconds() {
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

class Decoder {
public:
    Decoder(int output_fd, const sockaddr_in& player, double t0, double delay_ms)
        : output_fd_(output_fd), player_(player), t0_(t0), delay_ms_(delay_ms) {}

    void handle_fragment(const std::uint8_t* packet) {
        const std::uint8_t sequence_tag = packet[0] >> 5U;
        const std::size_t fragment_index = packet[0] & 0x1fU;
        const auto sequence = playable_sequence(sequence_tag);
        if (!sequence || was_emitted(*sequence)) {
            return;
        }

        FrameState& state = state_for(*sequence);
        if (state.present[fragment_index]) {
            return;
        }
        std::memcpy(state.fragments[fragment_index].data(),
                    packet + protocol::kFragmentHeaderBytes,
                    protocol::kSymbolBytes);
        state.present[fragment_index] = true;
        ++state.count;

        if (state.count >= protocol::kDataSymbolCount) {
            decode_and_emit(*sequence, state);
            frames_.erase(*sequence);
        }
    }

private:
    std::optional<std::uint32_t> playable_sequence(std::uint8_t sequence_tag) const {
        const double elapsed_ms = (epoch_seconds() - t0_) * 1000.0;
        const long long earliest = std::max<long long>(
            0, static_cast<long long>(std::ceil(
                   (elapsed_ms - delay_ms_ - kBoundaryToleranceMs) /
                   kFrameIntervalMs)));
        const long long latest = static_cast<long long>(
            std::floor((elapsed_ms + kBoundaryToleranceMs) / kFrameIntervalMs));

        std::optional<std::uint32_t> match;
        for (long long candidate = earliest; candidate <= latest; ++candidate) {
            if (candidate >= 0 &&
                static_cast<std::uint32_t>(candidate) %
                        protocol::kSequenceModulo ==
                    sequence_tag) {
                if (match) {
                    return std::nullopt;
                }
                match = static_cast<std::uint32_t>(candidate);
            }
        }
        return match;
    }

    FrameState& state_for(std::uint32_t sequence) {
        const auto [it, inserted] = frames_.try_emplace(sequence);
        if (inserted) {
            frame_order_.push_back(sequence);
        }
        while (frames_.size() > kMaxFrameStates && !frame_order_.empty()) {
            frames_.erase(frame_order_.front());
            frame_order_.pop_front();
        }
        return frames_.at(sequence);
    }

    bool was_emitted(std::uint32_t sequence) const {
        return emitted_.find(sequence) != emitted_.end();
    }

    void decode_and_emit(std::uint32_t sequence, const FrameState& state) {
        std::array<std::size_t, protocol::kDataSymbolCount> selected{};
        std::size_t selected_count = 0;
        for (std::size_t fragment = 0;
             fragment < protocol::kCodedSymbolCount &&
             selected_count < protocol::kDataSymbolCount;
             ++fragment) {
            if (state.present[fragment]) {
                selected[selected_count++] = fragment;
            }
        }
        if (selected_count != protocol::kDataSymbolCount) {
            return;
        }

        constexpr std::size_t kMatrixWidth = protocol::kDataSymbolCount * 2U;
        std::array<std::array<std::uint8_t, kMatrixWidth>,
                   protocol::kDataSymbolCount>
            matrix{};
        for (std::size_t row = 0; row < protocol::kDataSymbolCount; ++row) {
            for (std::size_t column = 0; column < protocol::kDataSymbolCount;
                 ++column) {
                matrix[row][column] =
                    protocol::generator_coefficient(selected[row], column);
            }
            matrix[row][protocol::kDataSymbolCount + row] = 1;
        }

        for (std::size_t column = 0; column < protocol::kDataSymbolCount;
             ++column) {
            std::size_t pivot = column;
            while (pivot < protocol::kDataSymbolCount &&
                   matrix[pivot][column] == 0U) {
                ++pivot;
            }
            if (pivot == protocol::kDataSymbolCount) {
                return;
            }
            if (pivot != column) {
                std::swap(matrix[pivot], matrix[column]);
            }

            const std::uint8_t inverse =
                protocol::gf_inverse(matrix[column][column]);
            for (std::size_t entry = 0; entry < kMatrixWidth; ++entry) {
                matrix[column][entry] =
                    protocol::gf_multiply(matrix[column][entry], inverse);
            }
            for (std::size_t row = 0; row < protocol::kDataSymbolCount; ++row) {
                if (row == column || matrix[row][column] == 0U) {
                    continue;
                }
                const std::uint8_t factor = matrix[row][column];
                for (std::size_t entry = 0; entry < kMatrixWidth; ++entry) {
                    matrix[row][entry] ^= protocol::gf_multiply(
                        factor, matrix[column][entry]);
                }
            }
        }

        std::array<std::uint8_t, protocol::kCodedSourceBytes> source{};
        for (std::size_t data_symbol = 0;
             data_symbol < protocol::kDataSymbolCount; ++data_symbol) {
            for (std::size_t byte = 0; byte < protocol::kSymbolBytes; ++byte) {
                std::uint8_t value = 0;
                for (std::size_t row = 0; row < protocol::kDataSymbolCount;
                     ++row) {
                    value ^= protocol::gf_multiply(
                        matrix[data_symbol][protocol::kDataSymbolCount + row],
                        state.fragments[selected[row]][byte]);
                }
                source[data_symbol * protocol::kSymbolBytes + byte] = value;
            }
        }

        const std::uint16_t decoded_sequence =
            (static_cast<std::uint16_t>(source[protocol::kPayloadBytes]) << 8U) |
            source[protocol::kPayloadBytes + 1U];
        if (decoded_sequence != static_cast<std::uint16_t>(sequence)) {
            return;
        }

        const auto frame = protocol::make_harness_frame(sequence, source.data());
        if (sendto(output_fd_, frame.data(), frame.size(), 0,
                   reinterpret_cast<const sockaddr*>(&player_), sizeof(player_)) < 0) {
            std::perror("sendto player");
            return;
        }

        emitted_.insert(sequence);
        emission_order_.push_back(sequence);
        while (emission_order_.size() > kMaxRememberedEmissions) {
            emitted_.erase(emission_order_.front());
            emission_order_.pop_front();
        }
    }

    int output_fd_;
    sockaddr_in player_{};
    double t0_;
    double delay_ms_;
    std::unordered_map<std::uint32_t, FrameState> frames_;
    std::deque<std::uint32_t> frame_order_;
    std::unordered_set<std::uint32_t> emitted_;
    std::deque<std::uint32_t> emission_order_;
};

}  // namespace

int main() {
    const double t0 = environment_number("T0");
    const double delay_ms = environment_number("DELAY_MS");

    const int input_fd = make_bound_socket(47002);
    if (input_fd < 0) {
        return EXIT_FAILURE;
    }

    const int output_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (output_fd < 0) {
        std::perror("socket");
        close(input_fd);
        return EXIT_FAILURE;
    }

    const sockaddr_in player = loopback_address(47020);
    std::uint8_t buffer[kMaxDatagramBytes];
    Decoder decoder(output_fd, player, t0, delay_ms);

    for (;;) {
        const ssize_t received = recvfrom(input_fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("recvfrom relay");
            break;
        }
        if (static_cast<std::size_t>(received) == protocol::kFragmentBytes) {
            decoder.handle_fragment(buffer);
        }
    }

    close(output_fd);
    close(input_fd);
    return EXIT_FAILURE;
}
