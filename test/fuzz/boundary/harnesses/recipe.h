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

#include <array>
#include <cstdint>
#include <utility>

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
        && byte_of(words.low, 2) <= std::to_underlying(ReductionAlgo::BLOCK_STABLE)
        && byte_of(words.low, 3) <= std::to_underlying(RoundingMode::RP)
        && byte_of(words.low, 4) <= std::to_underlying(ScalePolicy::PER_CHANNEL)
        && byte_of(words.low, 5) <= std::to_underlying(SoftmaxRecurrence::FLASH3)
        && byte_of(words.low, 6) <= std::to_underlying(ReductionDeterminism::BITEXACT_STRICT);
}

[[nodiscard]] inline Seeds seeds_recipe() {
    std::vector<std::uint8_t> image;
    append_raw(image, NumericalRecipe{});
    return {image, std::vector<std::uint8_t>(sizeof(NumericalRecipe), 0xFF)};
}

inline void run_recipe(std::span<const std::uint8_t> bytes) {
    std::array<std::uint8_t, sizeof(NumericalRecipe)> image{};
    const auto head = bytes.first(std::min(bytes.size(), image.size()));
    std::ranges::copy(head, image.begin());
    const RecipeWords words = ::fixy::decode<RecipeWords>(image).expect("each image of sixteen bytes is two words");
    const auto decoded = ::fixy::decode<NumericalRecipe>(words);
    CRUCIBLE_FUZZ_CLAIM("recipe", decoded.is_some() == names_each_enumerator(words));
    for (const NumericalRecipe& recipe : decoded) {
        const auto first = compute_recipe_hash(recipe);
        const auto second = compute_recipe_hash(recipe);
        CRUCIBLE_FUZZ_CLAIM("recipe", first == second);
    }
}

}  // namespace crucible::fuzz::boundary
