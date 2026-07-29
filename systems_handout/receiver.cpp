// Low-delay UDP receiver for duplicated DATA and sparse XOR recovery packets.
//
// All relay packets are 164 bytes. DATA starts with its ordinary sequence
// number. XOR packets set the sequence high bit and identify the first two
// frames of a 20-frame block. Valid DATA and recovered frames are forwarded
// immediately, while bounded caches suppress duplicates and tolerate reorder.

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <sys/socket.h>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>

#include "protocol.hpp"

namespace {

constexpr std::size_t kMaxDatagramBytes = 2048;
constexpr std::size_t kMaxPairRecords = 256;
constexpr std::size_t kMaxRememberedEmissions = 512;
constexpr std::uint32_t kRedundancyPeriod = 20;
constexpr std::uint32_t kParityFlag = std::uint32_t{1} << 31U;

struct PairState {
    std::array<std::uint8_t, protocol::kPayloadBytes> data[2]{};
    std::array<std::uint8_t, protocol::kPayloadBytes> parity{};
    bool have_data[2]{};
    bool have_parity = false;
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

class RecoveryBuffer {
public:
    RecoveryBuffer(int output_fd, const sockaddr_in& player)
        : output_fd_(output_fd), player_(player) {}

    void handle_data(std::uint32_t sequence, const std::uint8_t* payload) {
        const std::uint32_t position = sequence % kRedundancyPeriod;
        if (position <= 1U) {
            const std::uint32_t pair_start = sequence - position;
            PairState& state = state_for(pair_start);
            const std::size_t slot = static_cast<std::size_t>(position);
            if (!state.have_data[slot]) {
                std::memcpy(state.data[slot].data(), payload, protocol::kPayloadBytes);
                state.have_data[slot] = true;
            }
        }

        emit_once(sequence, payload);
        if (position <= 1U) {
            const std::uint32_t pair_start = sequence - position;
            recover_if_possible(pair_start);
            remove_if_complete(pair_start);
        }
    }

    void handle_parity(std::uint32_t pair_start, const std::uint8_t* payload) {
        if (pair_start % kRedundancyPeriod != 0U ||
            (was_emitted(pair_start) && was_emitted(pair_start + 1U))) {
            return;
        }

        PairState& state = state_for(pair_start);
        if (!state.have_parity) {
            std::memcpy(state.parity.data(), payload, protocol::kPayloadBytes);
            state.have_parity = true;
        }
        recover_if_possible(pair_start);
        remove_if_complete(pair_start);
    }

private:
    PairState& state_for(std::uint32_t pair_start) {
        const auto [it, inserted] = pairs_.try_emplace(pair_start);
        if (inserted) {
            pair_order_.push_back(pair_start);
        }
        while (pairs_.size() > kMaxPairRecords && !pair_order_.empty()) {
            const std::uint32_t oldest = pair_order_.front();
            pair_order_.pop_front();
            pairs_.erase(oldest);
        }
        return pairs_.at(pair_start);
    }

    bool was_emitted(std::uint32_t sequence) const {
        return emitted_.find(sequence) != emitted_.end();
    }

    void emit_once(std::uint32_t sequence, const std::uint8_t* payload) {
        if (was_emitted(sequence)) {
            return;
        }

        const auto frame = protocol::make_harness_frame(sequence, payload);
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

    void recover_if_possible(std::uint32_t pair_start) {
        auto it = pairs_.find(pair_start);
        if (it == pairs_.end()) {
            return;
        }

        PairState& state = it->second;
        const unsigned int data_count = static_cast<unsigned int>(state.have_data[0]) +
                                        static_cast<unsigned int>(state.have_data[1]);
        if (!state.have_parity || data_count != 1U) {
            return;
        }

        const std::size_t known_slot = state.have_data[0] ? 0U : 1U;
        const std::size_t missing_slot = 1U - known_slot;
        protocol::xor_payload(state.data[missing_slot].data(), state.parity.data(),
                              state.data[known_slot].data());
        state.have_data[missing_slot] = true;
        emit_once(pair_start + static_cast<std::uint32_t>(missing_slot),
                  state.data[missing_slot].data());
    }

    void remove_if_complete(std::uint32_t pair_start) {
        if (was_emitted(pair_start) && was_emitted(pair_start + 1U)) {
            pairs_.erase(pair_start);
        }
    }

    int output_fd_;
    sockaddr_in player_{};
    std::unordered_map<std::uint32_t, PairState> pairs_;
    std::deque<std::uint32_t> pair_order_;
    std::unordered_set<std::uint32_t> emitted_;
    std::deque<std::uint32_t> emission_order_;
};

}  // namespace

int main() {
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
    RecoveryBuffer recovery(output_fd, player);

    for (;;) {
        const ssize_t received = recvfrom(input_fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("recvfrom relay");
            break;
        }

        if (static_cast<std::size_t>(received) != protocol::kHarnessFrameBytes) {
            continue;
        }

        const std::uint32_t wire_id = protocol::read_u32_be(buffer);
        const std::uint8_t* payload = buffer + protocol::kHarnessHeaderBytes;
        if ((wire_id & kParityFlag) != 0U) {
            recovery.handle_parity(wire_id & ~kParityFlag, payload);
        } else {
            recovery.handle_data(wire_id, payload);
        }
    }

    close(output_fd);
    close(input_fd);
    return EXIT_FAILURE;
}
