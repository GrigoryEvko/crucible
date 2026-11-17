// The compile-time checks of foundation/os/X86Level.h.

#include <foundation/os/X86Level.h>

namespace foundation::os {

#if defined(__x86_64__)

// Words with no bit give v1, and v1 needs no feature.  The first missing
// feature of v2 is the first row of the table.
static_assert(x86_level_of(X86FeatureWords{}) == X86Level::v1);
static_assert(first_missing_feature(X86FeatureWords{}, X86Level::v1) == X86Feature::none);
static_assert(first_missing_feature(X86FeatureWords{}, X86Level::v2) == X86Feature::cx16);

// Words with each bit give v4, and hold each feature but none.
inline constexpr X86FeatureWords kEachBitForCheck{.xcr0 = ~std::uint64_t{0},
                                                  .leaf1_ecx = ~std::uint32_t{0},
                                                  .leaf7_ebx = ~std::uint32_t{0},
                                                  .extended1_ecx = ~std::uint32_t{0}};
static_assert(x86_level_of(kEachBitForCheck) == X86Level::v4);
static_assert(has_x86_feature(kEachBitForCheck, X86Feature::hi16_zmm_state));
static_assert(!has_x86_feature(kEachBitForCheck, X86Feature::none));

// The CPUID bits of v3 with no YMM state in XCR0 give v2, and the missing
// feature is the state.
inline constexpr X86FeatureWords kNoYmmStateForCheck{.xcr0 = std::uint64_t{0x3},
                                                     .leaf1_ecx = ~std::uint32_t{0},
                                                     .leaf7_ebx = ~std::uint32_t{0},
                                                     .extended1_ecx = ~std::uint32_t{0}};
static_assert(x86_level_of(kNoYmmStateForCheck) == X86Level::v2);
static_assert(first_missing_feature(kNoYmmStateForCheck, X86Level::v3) == X86Feature::ymm_state);

// The parse takes only the four texts.
static_assert(parse_x86_level("v3").value_or(X86Level::v1) == X86Level::v3);
static_assert(parse_x86_level("v5").is_none());
static_assert(parse_x86_level("").is_none());

// The pick of a level above the host gives the first missing feature.
static_assert(pick_x86_level(kNoYmmStateForCheck, core::none).value_or(X86Level::v4) == X86Level::v2);
static_assert(pick_x86_level(kNoYmmStateForCheck, core::some(X86Level::v4)).err().value_or(X86Feature::none)
              == X86Feature::ymm_state);

// The level of the target follows the order of the levels.
static_assert(x86_level_of_target() >= X86Level::v1 && x86_level_of_target() <= X86Level::v4);

#endif

}  // namespace foundation::os
