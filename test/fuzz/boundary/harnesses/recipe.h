#pragma once

// A NumericalRecipe image.  Recipes cross the federation boundary as their
// 16-byte image, and many byte values name no enumerator.  The decoder
// gives a recipe only when each enumeration byte names an enumerator, and
// an oracle that compares each byte with the enumerators agrees with it.
// Two hashes of one decoded recipe are equal.

#include "../harness.h"

#include <crucible/NumericalRecipe.h>
#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>

namespace crucible::fuzz::boundary {

// The sixteen bytes of a recipe image, as two little-endian words.
struct RecipeWords {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
};

// The byte at the index 0 to 7 of a word.
[[nodiscard]] constexpr std::uint8_t byte_of(std::uint64_t word, unsigned index) noexcept {
    return static_cast<std::uint8_t>(word >> (8 * index));
}

// True when each enumeration byte of the image names an enumerator.  The
// oracle does not use the decoder.  It reads the two element types with
// the switch of valid_scalar_type, and each other enumeration counts up
// from zero, so the oracle compares its byte with the last enumerator.  The
// flags and the hash take each byte.
[[nodiscard]] constexpr bool names_each_enumerator(RecipeWords words) noexcept {
    return valid_scalar_type(byte_of(words.low, 0)) && valid_scalar_type(byte_of(words.low, 1))
        && byte_of(words.low, 2) <= static_cast<std::uint8_t>(ReductionAlgo::BLOCK_STABLE)
        && byte_of(words.low, 3) <= static_cast<std::uint8_t>(::foundation::core::RoundingMode::RP)
        && byte_of(words.low, 4) <= static_cast<std::uint8_t>(ScalePolicy::PER_CHANNEL)
        && byte_of(words.low, 5) <= static_cast<std::uint8_t>(SoftmaxRecurrence::FLASH3)
        && byte_of(words.low, 6) <= static_cast<std::uint8_t>(ReductionDeterminism::BITEXACT_STRICT);
}

[[nodiscard]] inline SeedList seeds_recipe() noexcept {
    SeedWriter image{};
    image.append_raw(NumericalRecipe{});
    SeedBytes all_ones = ::fixy::mint_heap_array(test_alloc(), sizeof(NumericalRecipe), std::byte{0xFF});
    SeedList seeds = empty_seed_list();
    add_seed(seeds, image.finish());
    add_seed(seeds, std::move(all_ones));
    return seeds;
}

inline void run_recipe(::fixy::View<std::byte const> bytes) noexcept {
    ::fixy::FixedArray<std::byte, sizeof(NumericalRecipe)> image{};
    const std::size_t taken = bytes.size() < image.size() ? bytes.size() : image.size();
    image.with_mut([bytes, taken](auto run) noexcept {
        ::fixy::copy(::fixy::View<std::byte>{run.view()}.window(0, taken).expect("the image holds the bytes it takes"),
                     bytes.window(0, taken).expect("the input holds the bytes that the image takes"))
            .expect("the two windows hold the same count");
    });
    const ::fixy::FixedArray<std::byte, sizeof(NumericalRecipe)>& read = image;
    const RecipeWords words = ::fixy::decode<RecipeWords>(read).expect("each image of sixteen bytes is two words");
    const auto decoded = ::fixy::decode<NumericalRecipe>(words);
    CRUCIBLE_FUZZ_CLAIM("recipe", decoded.is_some() == names_each_enumerator(words));
    for (const NumericalRecipe& recipe : decoded) {
        const auto first = compute_recipe_hash(recipe);
        const auto second = compute_recipe_hash(recipe);
        CRUCIBLE_FUZZ_CLAIM("recipe", first == second);
    }
}

}  // namespace crucible::fuzz::boundary
