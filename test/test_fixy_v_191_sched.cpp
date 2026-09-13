// Sentinel TU: compiles the header under the project warning flags so its
// static_asserts run.

#include <crucible/fixy/Sched.h>
#include <crucible/fixy/Time.h>
#include <crucible/effects/ExecCtx.h>

#include <expected>
#include <type_traits>
#include <utility>

namespace {

namespace fsc = ::crucible::fixy::sched;
namespace ft = ::crucible::fixy::time;
namespace gs = ::crucible::fixy::grant::sched;
namespace sf = ::crucible::safety;
namespace eff = ::crucible::effects;
namespace ml = ::crucible::algebra::lattices;
using SP = fsc::SchedulerPolicy_v;

static_assert(std::is_same_v<decltype(fsc::mint_priority<-10>(std::declval<eff::BgDrainCtx const&>())),
                             std::expected<fsc::SchedPriority<-10>, int>>);
static_assert(std::is_same_v<decltype(fsc::mint_scheduler_policy<SP::Other>(std::declval<eff::BgDrainCtx const&>())),
                             std::expected<sf::SchedClass<SP::Other, fsc::ProofUnit>, int>>);

using PinT = sf::CpuPinned<ml::AffinityMask::single(0), sf::PinningPosture::PinnedExplicit, fsc::ProofUnit>;
static_assert(
    std::is_same_v<decltype(fsc::mint_affinity<ml::AffinityMask::single(0)>(std::declval<eff::BgDrainCtx const&>())),
                   std::expected<PinT, int>>);
// The affinity proof is exactly the input the TSC reader gate demands.
static_assert(ft::IsSingletonCpuPin<PinT>);

static_assert(::crucible::fixy::grant::which_dim_v<gs::affinity>
              == ::crucible::fixy::dim::DimensionAxis::SyscallSurface);
static_assert(::crucible::fixy::grant::which_dim_v<gs::scheduler_policy<SP::Deadline>>
              == ::crucible::fixy::dim::DimensionAxis::SyscallSurface);

}  // namespace

int main() {
    if (!::crucible::fixy::sched::detail::v191_self_test::runtime_smoke_test()) return 1;

    // The pin is best-effort.  When it succeeds, the proof feeds a TSC reader
    // and the read runs.
    eff::BgDrainCtx bg{};
    auto pin = fsc::mint_affinity<ml::AffinityMask::single(0)>(bg);
    if (pin) {
        auto reader = ft::mint_tsc_reader<ft::TscMode::Raw>(bg, std::move(*pin));
        (void)reader.read();
    }
    return 0;
}
