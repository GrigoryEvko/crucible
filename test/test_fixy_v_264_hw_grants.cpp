// Sentinel TU: compiles the two hot headers under the project warning flags so
// their static_asserts run.

#include <crucible/TraceRing.h>
#include <crucible/concurrent/ChaseLevDeque.h>

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Hw.h>

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/atoms/Hw.h>

#include <cstddef>
#include <optional>
#include <type_traits>

namespace {

namespace fg = ::crucible::fixy::grant;
namespace th = ::crucible::tracering_hw;
namespace ch = ::crucible::concurrent::chaselev_hw;
using D = ::crucible::fixy::dim::DimensionAxis;

// The ring states its instruction class as an atom of the new tree, so no
// grant layer sits between the claim and the resolver.
static_assert(::fixy::atom::IsAtom<th::InstructionTier>, "the TraceRing instruction tier must be a shipped atom.");
static_assert(th::InstructionTier::axis == ::fixy::Axis::HwInstruction,
              "the TraceRing instruction tier must engage the HwInstruction axis.");
static_assert(std::is_same_v<th::InstructionTier, ::fixy::atom::hw::scalar>,
              "the ring issues loads, stores and a prefetch hint, which is the scalar tier.");
static_assert(!::fixy::atom::hw::at_or_above(th::InstructionTier::tier,
                                             ::fixy::atom::hw::HwInstruction::NonDeterministicTsc),
              "the append runs on the hot path, which refuses the timestamp and privileged tiers.");
static_assert(th::kPrefetchLocality == 3, "the wired prefetch locality must be 3, the highest reuse, which is the "
                                          "value the four __builtin_prefetch calls share.");

static_assert(fg::IsGrantTag<ch::ActiveBarrierGrant>,
              "the ChaseLevDeque barrier grant must be a well-formed grant tag.");
static_assert(fg::which_dim_v<ch::ActiveBarrierGrant> == D::BarrierStrength,
              "the ChaseLevDeque barrier grant must route to the BarrierStrength axis.");

// A copy-paste that put the barrier grant on the instruction axis would trip
// this.
static_assert(fg::which_dim_v<ch::ActiveBarrierGrant> != D::HwInstruction,
              "the barrier grant must not occupy the instruction axis of the ring.");

}  // namespace

int main() {
    // The push and pop round-trip ODR-uses the deque whose header carries the
    // barrier declaration.
    ::crucible::concurrent::ChaseLevDeque<int, 16> deque{};
    if (!deque.push_bottom(7)) return 1;
    const std::optional<int> popped = deque.pop_bottom();
    if (!popped.has_value() || *popped != 7) return 1;

    // The return value ODR-uses the declared locality constant.
    return (th::kPrefetchLocality == 3) ? 0 : 1;
}
