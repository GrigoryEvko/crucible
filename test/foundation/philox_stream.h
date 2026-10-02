#pragma once

// A Philox4x32-10 stream for the property tests of the base families.
//
// The rounds are the rounds of include/crucible/Philox.h.  A test of the
// base layer cannot include that header: it is above the base, and its
// API gives std::array values.  The checks below compare this copy with
// the published reference vectors, which test/test_philox.cpp also
// checks, so a drift of either copy fails a build.
//
// The stream gives the words of block 0, then of block 1, and so on, for
// one key.  The same key gives the same words on each run and on each
// host.

#include <cstdint>

namespace foundation::test {

struct PhiloxBlock {
    std::uint32_t word0 = 0;
    std::uint32_t word1 = 0;
    std::uint32_t word2 = 0;
    std::uint32_t word3 = 0;
};

[[nodiscard]] constexpr PhiloxBlock philox4x32(PhiloxBlock counter, std::uint32_t key0, std::uint32_t key1) noexcept {
    constexpr std::uint32_t weyl0 = 0x9E3779B9u;
    constexpr std::uint32_t weyl1 = 0xBB67AE85u;
    constexpr std::uint64_t multiplier0 = 0xD2511F53u;
    constexpr std::uint64_t multiplier1 = 0xCD9E8D57u;
    for (int round = 0; round < 10; ++round) {
        const std::uint64_t product0 = multiplier0 * counter.word0;
        const std::uint64_t product1 = multiplier1 * counter.word2;
        counter = PhiloxBlock{
            static_cast<std::uint32_t>(product1 >> 32) ^ counter.word1 ^ key0,
            static_cast<std::uint32_t>(product1),
            static_cast<std::uint32_t>(product0 >> 32) ^ counter.word3 ^ key1,
            static_cast<std::uint32_t>(product0),
        };
        key0 += weyl0;
        key1 += weyl1;
    }
    return counter;
}

static_assert(philox4x32(PhiloxBlock{}, 0u, 0u).word0 == 0x6627E8D5u);
static_assert(philox4x32(PhiloxBlock{}, 0u, 0u).word3 == 0x9B00DBD8u);
static_assert(
    philox4x32(PhiloxBlock{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}, 0xFFFFFFFFu, 0xFFFFFFFFu).word1
    == 0x41C83B0Eu);
static_assert(
    philox4x32(PhiloxBlock{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}, 0xFFFFFFFFu, 0xFFFFFFFFu).word2
    == 0xA20BC7C6u);

class PhiloxStream {
public:
    constexpr explicit PhiloxStream(std::uint64_t seed) noexcept
        : key0_{static_cast<std::uint32_t>(seed)}, key1_{static_cast<std::uint32_t>(seed >> 32)} {}

    [[nodiscard]] constexpr std::uint32_t next_word() noexcept {
        if (used_ == 4) {
            block_ = philox4x32(PhiloxBlock{static_cast<std::uint32_t>(block_number_),
                                            static_cast<std::uint32_t>(block_number_ >> 32), 0u, 0u},
                                key0_, key1_);
            ++block_number_;
            used_ = 0;
        }
        const int position = used_++;
        if (position == 0) return block_.word0;
        if (position == 1) return block_.word1;
        if (position == 2) return block_.word2;
        return block_.word3;
    }

    [[nodiscard]] constexpr std::uint64_t next_wide() noexcept {
        const std::uint64_t high = next_word();
        return (high << 32) | next_word();
    }

    // A value in [0, bound).  The bound must not be zero.  The small bias
    // of the remainder does not matter to a property test.
    [[nodiscard]] constexpr std::uint64_t below(std::uint64_t bound) noexcept { return next_wide() % bound; }

private:
    std::uint32_t key0_ = 0;
    std::uint32_t key1_ = 0;
    std::uint64_t block_number_ = 0;
    PhiloxBlock block_{};
    int used_ = 4;
};

}  // namespace foundation::test
