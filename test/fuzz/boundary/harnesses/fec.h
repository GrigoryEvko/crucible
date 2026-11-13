#pragma once

// Reed-Solomon shards from the network.  The decoder must survive any
// received block and any erasure mask.  With at most M erasures, the shards
// that an encoder wrote decode back to the input it was given.

#include "../harness.h"

#include <crucible/cntp/Fec.h>
#include <fixy/Core.h>

#include <vector>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_fec() {
    std::vector<std::uint8_t> seed;
    append_raw(seed, std::uint8_t{0x05});
    append_raw(seed, std::uint16_t{8});
    append_raw(seed, std::uint16_t{12});
    for (std::uint8_t i = 0; i < 12; ++i)
        seed.push_back(i);
    append_bytes(seed, text_bytes("reed-solomon seed input"));
    return {seed};
}

// The bytes of a run of the input, in a heap array that a codec reads as a
// View.
[[nodiscard]] inline ::fixy::HeapArray<std::byte> heap_bytes(auto const& run) {
    auto out = ::fixy::mint_heap_array(::foundation::effects::Alloc{}, run.size(), std::byte{0});
    std::size_t position = 0;
    for (std::byte& byte : out) {
        byte = std::byte{run[position]};
        ++position;
    }
    return out;
}

inline void run_fec(std::span<const std::uint8_t> bytes) {
    using Codec = ::crucible::cntp::ReedSolomon<4, 2>;
    constexpr Codec codec{};
    ByteCursor cursor{bytes};

    // Any block, any mask, any output size.
    const auto mask_bits = cursor.take<std::uint8_t>();
    const auto output_size = cursor.take<std::uint16_t>();
    const auto received = heap_bytes(cursor.take_bytes(cursor.take<std::uint16_t>()));
    ::foundation::core::FixedArray<bool, Codec::total_shards> mask{};
    const auto bit_is_set = [mask_bits](std::size_t i) noexcept { return ((unsigned{mask_bits} >> i) & 1U) != 0; };
    for (auto shard : mask.indices())
        mask.at(shard) = bit_is_set(shard.raw());
    auto output = ::fixy::mint_heap_array(::foundation::effects::Alloc{}, output_size, std::byte{0});
    (void)codec.decode(received.view(), mask.view(), output.view());

    // Encode the rest of the input, erase at most two shards, decode.
    const auto input = heap_bytes(cursor.rest());
    if (input.size() == 0) return;
    auto encoded =
        ::fixy::mint_heap_array(::foundation::effects::Alloc{}, Codec::encoded_size_for(input.size()), std::byte{0});
    CRUCIBLE_FUZZ_CLAIM("fec", codec.encode(input.view(), encoded.view()).is_ok());
    const std::size_t shard = encoded.size() / Codec::total_shards;
    ::foundation::core::FixedArray<bool, Codec::total_shards> erased{};
    std::size_t erased_count = 0;
    for (auto index : erased.indices()) {
        if (erased_count >= Codec::parity_shards) break;
        if (!bit_is_set(index.raw())) continue;
        erased.at(index) = true;
        ++erased_count;
        ::fixy::fill(encoded.view().window(index.raw() * shard, shard).expect("the shard lies inside the block"),
                     std::byte{0xA5});
    }
    auto decoded = ::fixy::mint_heap_array(::foundation::effects::Alloc{}, input.size(), std::byte{0});
    CRUCIBLE_FUZZ_CLAIM("fec", codec.decode(encoded.view(), erased.view(), decoded.view()).is_ok());
    CRUCIBLE_FUZZ_CLAIM("fec", ::fixy::equal(decoded.view(), input.view()));
}

}  // namespace crucible::fuzz::boundary
