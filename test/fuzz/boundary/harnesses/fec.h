#pragma once

// Reed-Solomon shards from the network.  The decoder must survive any
// received block and any erasure mask.  With at most M erasures, the shards
// that an encoder wrote decode back to the input it was given.

#include "../harness.h"

#include <crucible/cntp/Fec.h>
#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline SeedList seeds_fec() noexcept {
    SeedWriter seed{};
    seed.append_raw(std::uint8_t{0x05});
    seed.append_raw(std::uint16_t{8});
    seed.append_raw(std::uint16_t{12});
    for (std::uint8_t i = 0; i < 12; ++i)
        seed.append_raw(i);
    seed.append_text("reed-solomon seed input");
    SeedList seeds = empty_seed_list();
    add_seed(seeds, seed.finish());
    return seeds;
}

inline void run_fec(::fixy::View<std::byte const> bytes) noexcept {
    using Codec = ::crucible::cntp::ReedSolomon<4, 2>;
    constexpr Codec codec{};
    InputCursor cursor{bytes};

    // Any block, any mask, any output size.
    const auto mask_bits = cursor.take<std::uint8_t>();
    const auto output_size = cursor.take<std::uint16_t>();
    const auto received_count = cursor.take<std::uint16_t>();
    const SeedBytes received = copy_seed(cursor.take_bytes(received_count));
    ::foundation::core::FixedArray<bool, Codec::total_shards> mask{};
    const auto bit_is_set = [mask_bits](std::size_t i) noexcept { return ((unsigned{mask_bits} >> i) & 1U) != 0; };
    for (auto shard : mask.indices())
        mask.with_element(shard, [bit_is_set, shard](bool& element) noexcept { element = bit_is_set(shard.raw()); });
    auto output = ::fixy::mint_heap_array(test_alloc(), output_size, std::byte{0});
    output = ::fixy::with_mut(std::move(output), [&codec, &received, &mask](auto run) noexcept {
        (void)codec.decode(received.view(), mask.view(), run.view());
    });

    // Encode the rest of the input, erase at most two shards, decode.
    const SeedBytes input = copy_seed(cursor.rest());
    if (input.size() == 0) return;
    auto encoded = ::fixy::mint_heap_array(test_alloc(), Codec::encoded_size_for(input.size()), std::byte{0});
    encoded = ::fixy::with_mut(std::move(encoded), [&codec, &input](auto run) noexcept {
        CRUCIBLE_FUZZ_CLAIM("fec", codec.encode(input.view(), run.view()).is_ok());
    });
    const std::size_t shard = encoded.size() / Codec::total_shards;
    ::foundation::core::FixedArray<bool, Codec::total_shards> erased{};
    std::size_t erased_count = 0;
    encoded = ::fixy::with_mut(std::move(encoded), [&erased, &erased_count, &bit_is_set, shard](auto run) noexcept {
        for (auto index : erased.indices()) {
            if (erased_count >= Codec::parity_shards) break;
            if (!bit_is_set(index.raw())) continue;
            erased.with_element(index, [](bool& element) noexcept { element = true; });
            ++erased_count;
            ::fixy::fill(run.view().window(index.raw() * shard, shard).expect("the shard lies inside the block"),
                         std::byte{0xA5});
        }
    });
    auto decoded = ::fixy::mint_heap_array(test_alloc(), input.size(), std::byte{0});
    decoded = ::fixy::with_mut(std::move(decoded), [&codec, &encoded, &erased](auto run) noexcept {
        CRUCIBLE_FUZZ_CLAIM("fec", codec.decode(encoded.view(), erased.view(), run.view()).is_ok());
    });
    CRUCIBLE_FUZZ_CLAIM("fec", ::fixy::equal(decoded.view(), input.view()));
}

}  // namespace crucible::fuzz::boundary
