#pragma once

// A NumericalRecipe image.  Recipes cross the federation boundary as their
// 16-byte image, and many byte values name no enumerator.  Hashing any
// image must be defined, and two calls on one image give one hash.

#include "../harness.h"

#include <crucible/NumericalRecipe.h>

#include <array>
#include <bit>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_recipe() {
    std::vector<std::uint8_t> image;
    append_raw(image, NumericalRecipe{});
    return {image, std::vector<std::uint8_t>(sizeof(NumericalRecipe), 0xFF)};
}

inline void run_recipe(std::span<const std::uint8_t> bytes) {
    std::array<std::uint8_t, sizeof(NumericalRecipe)> image{};
    const auto head = bytes.first(std::min(bytes.size(), image.size()));
    std::ranges::copy(head, image.begin());
    const auto recipe = std::bit_cast<NumericalRecipe>(image);
    const auto first = compute_recipe_hash(recipe);
    const auto second = compute_recipe_hash(recipe);
    CRUCIBLE_FUZZ_CLAIM("recipe", first == second);
}

}  // namespace crucible::fuzz::boundary
