// The compile-time checks of crucible/SwissTable.h.  Each build compiles
// this file with the ISA flags of the build, so the checks of the arm that
// the preprocessor selects run one time in each build.

#include <crucible/SwissTable.h>

namespace crucible {
namespace detail {

// The wrapper is not trivially copyable, so no byte copy builds a group
// width that the predicate did not see.  Its copy and move constructors
// and its destructor are trivial, so it still passes in a register.
static_assert(sizeof(GroupWidth) == sizeof(std::size_t));
static_assert(std::is_trivially_copy_constructible_v<GroupWidth> && std::is_trivially_move_constructible_v<GroupWidth>
              && std::is_trivially_destructible_v<GroupWidth>);
static_assert(std::is_standard_layout_v<GroupWidth>);

// The upper bound is 64 because BitMask carries the group in a uint64_t.
// A wider group needs a wider mask type and a re-audit of the H2 tag width.
static_assert(::foundation::decide::is_power_of_two_le<std::size_t>(group_width(), std::size_t{64}),
              "kGroupWidth must be a power of two ≤ 64 (AVX-512 width)");

namespace swiss_hw {

static_assert(::fixy::atom::IsAtom<ActiveSimdIsa> && ActiveSimdIsa::axis == ::fixy::Axis::SimdIsa,
              "the probe's ISA is a shipped atom of the SimdIsa axis");
static_assert(::fixy::atom::IsAtom<InstructionTier> && InstructionTier::axis == ::fixy::Axis::HwInstruction,
              "the probe's instruction class is a shipped atom of the HwInstruction axis");
static_assert(!fah::at_or_above(InstructionTier::tier, fah::HwInstruction::NonDeterministicTsc),
              "the probe runs on the hot path, which refuses the timestamp and privileged tiers");

// A vector arm loads one control-byte group into one register, so the group
// is exactly one register wide.  The portable arm has no register to match.
static_assert(fas::register_bits(ActiveSimdIsa::isa) == 0
                  || group_width() * 8U == fas::register_bits(ActiveSimdIsa::isa),
              "the control-byte group must be exactly one vector register of the active ISA");

}  // namespace swiss_hw

static_assert(h2_tag(0x0000000000000000ULL) == 0, "h2_tag(0) must be 0");
static_assert(h2_tag(0xFFFFFFFFFFFFFFFFULL) == 127, "h2_tag(UINT64_MAX) must be 127 (top 7 bits all set, "
                                                    "bit 7 of int8_t clear)");
static_assert(h2_tag(0x8000000000000000ULL) == 64, "h2_tag with only bit 63 set must map to 0x40");

static_assert(sizeof(CtrlGroup) == group_width(), "CtrlGroup size must match SIMD group width");

}  // namespace detail
}  // namespace crucible
