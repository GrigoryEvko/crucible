// Sentinel TU: compiles the two hot headers under the project warning flags so
// their static_asserts run.

#include <crucible/TraceRing.h>
#include <crucible/concurrent/ChaseLevDeque.h>

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Hw.h>

#include <cstddef>
#include <optional>
#include <type_traits>

namespace {

namespace fg = ::crucible::fixy::grant;
namespace th = ::crucible::tracering_hw;
namespace ch = ::crucible::concurrent::chaselev_hw;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(fg::IsGrantTag<th::ActiveCacheGrant>, "the TraceRing cache grant must be a well-formed grant tag.");
static_assert(fg::which_dim_v<th::ActiveCacheGrant> == D::HwInstruction,
              "the TraceRing cache grant must route to the HwInstruction axis.");
static_assert(th::kPrefetchLocality == 3, "the wired prefetch locality must be 3, the highest reuse — the value the "
                                          "four __builtin_prefetch calls share with the grant tag.");

static_assert(fg::IsGrantTag<ch::ActiveBarrierGrant>,
              "the ChaseLevDeque barrier grant must be a well-formed grant tag.");
static_assert(fg::which_dim_v<ch::ActiveBarrierGrant> == D::BarrierStrength,
              "the ChaseLevDeque barrier grant must route to the BarrierStrength axis.");

// A copy-paste that put the barrier grant on the cache grant's axis would trip
// this.
static_assert(fg::which_dim_v<th::ActiveCacheGrant> != fg::which_dim_v<ch::ActiveBarrierGrant>,
              "the cache and barrier grants must occupy distinct axes.");

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
