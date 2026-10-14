// The compile-time checks of crucible/NumericalRecipe.h.

#include <crucible/NumericalRecipe.h>

#include <fixy/Core.h>

#include <cstdint>

namespace crucible {

static_assert(sizeof(NumericalRecipe) == 16, "NumericalRecipe must stay 16 bytes: both the intern table and the "
                                             "kernel nodes that point at it depend on the layout");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(NumericalRecipe);

namespace {

// The sixteen bytes of a recipe image, as two words.  The low word holds the
// eight semantic bytes in the order of the packed word of compute_recipe_hash.
struct RecipeImage {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
};

constexpr std::uint64_t kSentinel = RecipeHash::sentinel().raw();

// A finalizer result of 0 or of the sentinel takes the finalizer result of
// its XOR with the salt, the stand-in word.
constexpr std::uint64_t kZeroStandIn = 0 ^ detail_recipe::kReservedSalt;
constexpr std::uint64_t kSentinelStandIn = kSentinel ^ detail_recipe::kReservedSalt;

// The finalizer maps the zero word to 0, and the zero word is a recipe.  So
// compute_recipe_hash cannot give the finalizer result of each word.
static_assert(::fixy::decode<NumericalRecipe>(RecipeImage{}).is_some(),
              "the image whose bytes are zero decodes to a recipe, and the finalizer maps it to 0");

// No recipe packs to a stand-in word, so no recipe has the hash of a stand-in.
static_assert(::fixy::decode<NumericalRecipe>(RecipeImage{.low = kZeroStandIn}).is_none(),
              "a recipe packs to the stand-in word of the result 0, and two recipes share a hash");
static_assert(::fixy::decode<NumericalRecipe>(RecipeImage{.low = kSentinelStandIn}).is_none(),
              "a recipe packs to the stand-in word of the sentinel, and two recipes share a hash");

// The hash of a stand-in is not a reserved value.
static_assert(detail_recipe::fmix64(kZeroStandIn) != 0 && detail_recipe::fmix64(kZeroStandIn) != kSentinel,
              "the hash of the stand-in of the result 0 is a reserved value");
static_assert(detail_recipe::fmix64(kSentinelStandIn) != 0 && detail_recipe::fmix64(kSentinelStandIn) != kSentinel,
              "the hash of the stand-in of the sentinel is a reserved value");

}  // namespace

}  // namespace crucible
