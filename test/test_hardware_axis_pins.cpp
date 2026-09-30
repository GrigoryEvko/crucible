// Sentinel TU for the hardware pins of the hot headers.  Each header
// restates its compile-time-selected hardware construct as fixy atoms:
// the SwissTable probe names its SIMD ISA and its instruction class, and
// the TraceRing append its instruction class.  This file compiles the
// headers under the project warning flags, so their own static_asserts
// run, and it pins the atoms that the active build selects.
//
// The rows below hold each hardware-axis block to the axes it states.
// utils/scripts/check-fixy-hw-discipline.py reads them from the parse tree, and
// every `*_hw` namespace under include/ and src/ needs a row here.

#include "fixy/hw_axis_pins.h"

#include <crucible/SwissTable.h>
#include <crucible/TraceRing.h>

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/atoms/Hw.h>
#include <fixy/atoms/Simd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// The control-byte probe is emitted for the ISA that the preprocessor
// selects, and it issues the instruction class of that ISA.
static_assert(hw_axis_pins::pinned<^^::crucible::detail::swiss_hw, ::fixy::Axis::SimdIsa, ::fixy::Axis::HwInstruction>);

// The append issues a prefetch, and the hot path bounds its instruction class.
static_assert(hw_axis_pins::pinned<^^::crucible::tracering_hw, ::fixy::Axis::HwInstruction>);

namespace {

namespace sw = ::crucible::detail::swiss_hw;
namespace th = ::crucible::tracering_hw;
namespace fas = ::fixy::atom::simd;
namespace fah = ::fixy::atom::hw;

// ── SwissTable: the probe arm ────────────────────────────────────────
#if defined(__AVX512BW__)
static_assert(std::is_same_v<sw::ActiveSimdIsa, fas::avx512bw>);
#elif defined(__AVX2__)
static_assert(std::is_same_v<sw::ActiveSimdIsa, fas::avx2>);
#elif defined(__SSE2__)
static_assert(std::is_same_v<sw::ActiveSimdIsa, fas::sse2>);
#elif defined(__aarch64__)
static_assert(std::is_same_v<sw::ActiveSimdIsa, fas::neon>);
#else
static_assert(std::is_same_v<sw::ActiveSimdIsa, fas::scalar>, "the portable probe arm declares the scalar ISA.");
#endif

static_assert(::fixy::atom::IsAtom<sw::ActiveSimdIsa> && sw::ActiveSimdIsa::axis == ::fixy::Axis::SimdIsa);
static_assert(::fixy::atom::IsAtom<sw::InstructionTier> && sw::InstructionTier::axis == ::fixy::Axis::HwInstruction);

// A vector arm issues intrinsics, and the portable arm is scalar SWAR.
static_assert(fas::is_trunk_pinned(sw::ActiveSimdIsa::isa) == std::is_same_v<sw::InstructionTier, fah::vectorizable>);
static_assert(fas::is_trunk_pinned(sw::ActiveSimdIsa::isa) || std::is_same_v<sw::InstructionTier, fah::scalar>);

// The group width is in bytes and the register width is in bits.
static_assert(!fas::is_trunk_pinned(sw::ActiveSimdIsa::isa)
                  || ::crucible::detail::group_width() * 8U == fas::register_bits(sw::ActiveSimdIsa::isa),
              "the control-byte group must be exactly one vector register of the active ISA.");

// ── TraceRing: the append ────────────────────────────────────────────
static_assert(::fixy::atom::IsAtom<th::InstructionTier>, "the TraceRing instruction tier must be a shipped atom.");
static_assert(th::InstructionTier::axis == ::fixy::Axis::HwInstruction,
              "the TraceRing instruction tier must engage the HwInstruction axis.");
static_assert(std::is_same_v<th::InstructionTier, fah::scalar>,
              "the ring issues loads, stores and a prefetch hint, which is the scalar tier.");
static_assert(!fah::at_or_above(th::InstructionTier::tier, fah::HwInstruction::NonDeterministicTsc),
              "the append runs on the hot path, which refuses the timestamp and privileged tiers.");
static_assert(th::kPrefetchLocality == 3, "the wired prefetch locality must be 3, the highest reuse, which is the "
                                          "value the four __builtin_prefetch calls share.");

}  // namespace

int main() {
    // A real probe ODR-uses the machinery the SwissTable pins describe.
    const std::size_t bytes = ::crucible::detail::group_width();
    if (bytes != 16 && bytes != 32 && bytes != 64) return 1;

    // Sized for the widest group any arm uses.  A narrower arm reads a prefix.
    alignas(64) std::array<std::int8_t, 64> ctrl{};
    for (std::size_t i = 0; i < ctrl.size(); ++i)
        ctrl[i] = static_cast<std::int8_t>(i & 0x7F);  // H2 tags 0x00..0x7F
    ctrl[3] = ::crucible::detail::kEmpty;  // the sole empty slot

    const auto group = ::crucible::detail::CtrlGroup::load(ctrl.data());
    const auto empties = group.match_empty();
    const auto fives = group.match(static_cast<std::int8_t>(5));

    // kEmpty (0x80) is the only byte with bit 7 set, so index 3 is the one match.
    if (!static_cast<bool>(empties)) return 2;
    if (empties.lowest() != 3U) return 3;
    // H2 tag 5 is unique at index 5 within any group width.
    if (!static_cast<bool>(fives)) return 4;
    if (fives.lowest() != 5U) return 5;

    // The return value ODR-uses the declared locality constant.
    return (th::kPrefetchLocality == 3) ? 0 : 6;
}
