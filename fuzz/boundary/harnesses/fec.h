#pragma once

// Reed-Solomon shards from the network.  The decoder must survive any
// received block and any erasure mask.  With at most M erasures, the shards
// that an encoder wrote decode back to the input it was given.

#include "../harness.h"

#include <crucible/cntp/Fec.h>

#include <array>
#include <vector>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_fec() {
    std::vector<std::uint8_t> seed;
    append_raw(seed, std::uint8_t{0x05});
    append_raw(seed, std::uint16_t{8});
    append_raw(seed, std::uint16_t{12});
    for (std::uint8_t i = 0; i < 12; ++i) seed.push_back(i);
    append_bytes(seed, text_bytes("reed-solomon seed input"));
    return {seed};
}

inline void run_fec(std::span<const std::uint8_t> bytes) {
    using Codec = ::crucible::cntp::ReedSolomon<4, 2>;
    constexpr Codec codec{};
    ByteCursor cursor{bytes};

    // Any block, any mask, any output size.
    const auto mask_bits = cursor.take<std::uint8_t>();
    const auto output_size = cursor.take<std::uint16_t>();
    const auto received = cursor.take_bytes(cursor.take<std::uint16_t>());
    std::array<bool, Codec::total_shards> mask{};
    const auto bit_is_set = [mask_bits](std::size_t i) noexcept { return ((unsigned{mask_bits} >> i) & 1U) != 0; };
    for (std::size_t i = 0; i < mask.size(); ++i) mask[i] = bit_is_set(i);
    std::vector<std::byte> output(output_size);
    (void)codec.decode(as_bytes_view(received), std::span<const bool>{mask}, std::span<std::byte>{output});

    // Encode the rest of the input, erase at most two shards, decode.
    const auto input = cursor.rest();
    if (input.empty()) return;
    std::vector<std::byte> encoded(Codec::encoded_size_for(input.size()));
    CRUCIBLE_FUZZ_CLAIM("fec", codec.encode(as_bytes_view(input), std::span<std::byte>{encoded}).has_value());
    const std::size_t shard = encoded.size() / Codec::total_shards;
    std::array<bool, Codec::total_shards> erased{};
    std::size_t erased_count = 0;
    for (std::size_t i = 0; i < erased.size() && erased_count < Codec::parity_shards; ++i) {
        if (!bit_is_set(i)) continue;
        erased[i] = true;
        ++erased_count;
        for (std::size_t b = 0; b < shard; ++b) encoded[i * shard + b] = std::byte{0xA5};
    }
    std::vector<std::byte> decoded(input.size());
    CRUCIBLE_FUZZ_CLAIM("fec", codec.decode(encoded, std::span<const bool>{erased}, std::span<std::byte>{decoded}).has_value());
    CRUCIBLE_FUZZ_CLAIM("fec", std::ranges::equal(decoded, as_bytes_view(input)));
}

}  // namespace crucible::fuzz::boundary
