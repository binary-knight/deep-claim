#pragma once

#include <cstdint>

namespace sim {

// FNV-1a over an explicit little-endian byte stream. Used for state hashes
// so that the same state produces the same hash on every compiler and OS.
// Never use std::hash for this purpose.
class Fnv1a64 {
public:
    void addByte(std::uint8_t byte) {
        hash_ ^= byte;
        hash_ *= 0x100000001b3ULL;
    }

    void add(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64U; shift += 8U) {
            addByte(static_cast<std::uint8_t>(value >> shift));
        }
    }

    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xcbf29ce484222325ULL;
};

}  // namespace sim
