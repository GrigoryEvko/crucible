#pragma once

// The Scalar family: the counter-based generator Philox4x32-10.
//
//   Philox4x32::Counter         the four words of a counter
//   Philox4x32::Key             the two words of a key
//   Philox4x32::generate(c, k)  the block of ten rounds over the counter c
//                               with the key k
//   Philox4x32::generate(o, k)  the block of the counter whose two low words
//                               hold the offset o, with the key k of 64 bits
//   Philox4x32::multiplier0, multiplier1  the multipliers of a round
//   Philox4x32::weyl_step0, weyl_step1    the steps of the key in each round
//
// The four constants and the ten rounds come from the Philox4x32-10
// specification (Salmon, Moraes, Dror and Shaw, SC 2011).  A change of one of
// them changes each stream that the runtime made, so the replay of earlier
// state breaks.
//
// A round does two products of 32-bit words with their high halves, two xor
// operations of each high half, and two additions of a step to the key.  Each
// operation is an integer operation, so the same counter and key give the same
// bits on each target.  A product uses a multiply of 64 bits, and each
// addition wraps modulo 2^32.  The generator has no state: the counter is the
// position in the stream, and a block depends only on its counter and its key.
//
// std::philox4x32 makes the same block, but its seed sets only the first word
// of the key.  It cannot make each block of a key of 64 bits.

#include <foundation/core/Arithmetic.h>
#include <foundation/core/Array.h>
#include <foundation/core/Index.h>

#include <cstdint>

namespace foundation::core {

struct Philox4x32 final {
    using Counter = FixedArray<std::uint32_t, 4>;
    using Key = FixedArray<std::uint32_t, 2>;

    static constexpr std::uint32_t multiplier0 = 0xD2511F53u;
    static constexpr std::uint32_t multiplier1 = 0xCD9E8D57u;
    static constexpr std::uint32_t weyl_step0 = 0x9E3779B9u;
    static constexpr std::uint32_t weyl_step1 = 0xBB67AE85u;

    // The two products read the words of the counter before the round
    // writes them.  Each round is about ten integer instructions.
    [[nodiscard]] static constexpr Counter generate(Counter counter, Key key) noexcept {
        std::uint32_t word0 = counter.at(index<0>());
        std::uint32_t word1 = counter.at(index<1>());
        std::uint32_t word2 = counter.at(index<2>());
        std::uint32_t word3 = counter.at(index<3>());
        std::uint32_t key0 = key.at(index<0>());
        std::uint32_t key1 = key.at(index<1>());
        for (int round = 0; round < 10; ++round) {
            std::uint64_t const product0 = std::uint64_t{multiplier0} * word0;
            std::uint64_t const product1 = std::uint64_t{multiplier1} * word2;
            word0 = static_cast<std::uint32_t>(product1 >> 32) ^ word1 ^ key0;
            word1 = static_cast<std::uint32_t>(product1);
            word2 = static_cast<std::uint32_t>(product0 >> 32) ^ word3 ^ key1;
            word3 = static_cast<std::uint32_t>(product0);
            key0 = wrapping_add(key0, weyl_step0);
            key1 = wrapping_add(key1, weyl_step1);
        }
        return Counter{word0, word1, word2, word3};
    }

    [[nodiscard]] static constexpr Counter generate(std::uint64_t offset, std::uint64_t key) noexcept {
        return generate(Counter{static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(offset >> 32), 0u, 0u},
                        Key{static_cast<std::uint32_t>(key), static_cast<std::uint32_t>(key >> 32)});
    }
};

}  // namespace foundation::core
