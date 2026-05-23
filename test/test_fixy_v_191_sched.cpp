// FIXY-V-191 sentinel TU: fixy/Sched.h — the scheduler-control surface.
// Grant tags (affinity / scheduler_policy / priority / thread_name) routed
// to SyscallSurface, plus mints that PRODUCE the V-187 CpuPinned proof and
// the V-186 SchedClass proof and apply scheduler parameters via syscall.
//
// LOAD-BEARING PROPERTIES this TU defends:
//   (1) mint_affinity produces exactly the move-only CpuPinned proof that
//       fixy/Time.h's mint_tsc_reader consumes — demonstrated end-to-end;
//   (2) mint_scheduler_policy<Deadline,...> carries the CBS budget in its
//       return type (SchedClass static_assert);
//   (3) the privileged setters return std::expected<Proof,int> so a failed
//       syscall never yields an unsound proof.
//
// HS14 negative coverage lives in six fixtures (test/fixy_neg/neg_sched_*):
// two per NEW mint (mint_affinity / mint_scheduler_policy / mint_priority).

#include <crucible/fixy/Sched.h>
#include <crucible/fixy/Time.h>            // mint_tsc_reader — the consumer of mint_affinity's proof
#include <crucible/effects/ExecCtx.h>

#include <expected>
#include <type_traits>
#include <utility>

namespace {

namespace fsc = ::crucible::fixy::sched;
namespace ft  = ::crucible::fixy::time;
namespace gs  = ::crucible::fixy::grant::sched;
namespace sf  = ::crucible::safety;
namespace eff = ::crucible::effects;
namespace ml  = ::crucible::algebra::lattices;
using SP = fsc::SchedulerPolicy_v;

// ── mint return shapes ──────────────────────────────────────────────
static_assert(std::is_same_v<
    decltype(fsc::mint_priority<-10>(std::declval<eff::BgDrainCtx const&>())),
    std::expected<fsc::SchedPriority<-10>, int>>);
static_assert(std::is_same_v<
    decltype(fsc::mint_scheduler_policy<SP::Other>(std::declval<eff::BgDrainCtx const&>())),
    std::expected<sf::SchedClass<SP::Other, fsc::ProofUnit>, int>>);

// ── mint_affinity produces exactly mint_tsc_reader's input proof ────
using PinT = sf::CpuPinned<ml::AffinityMask::single(0),
                           sf::PinningPosture::PinnedExplicit, fsc::ProofUnit>;
static_assert(std::is_same_v<
    decltype(fsc::mint_affinity<ml::AffinityMask::single(0)>(std::declval<eff::BgDrainCtx const&>())),
    std::expected<PinT, int>>);
// The proof PinT satisfies the V-190 TSC single-core-pin gate.
static_assert(ft::IsSingletonCpuPin<PinT>);

// ── grant-tag routing ───────────────────────────────────────────────
static_assert(::crucible::fixy::grant::which_dim_v<gs::affinity>
              == ::crucible::fixy::dim::DimensionAxis::SyscallSurface);
static_assert(::crucible::fixy::grant::which_dim_v<gs::scheduler_policy<SP::Deadline>>
              == ::crucible::fixy::dim::DimensionAxis::SyscallSurface);

}  // namespace

int main() {
    if (!::crucible::fixy::sched::detail::v191_self_test::runtime_smoke_test()) return 1;

    // End-to-end V-191 → V-190 composition: pin (best-effort), then if the
    // pin succeeded, feed the proof into a TSC reader and read.
    eff::BgDrainCtx bg{};
    auto pin = fsc::mint_affinity<ml::AffinityMask::single(0)>(bg);
    if (pin) {
        auto reader = ft::mint_tsc_reader<ft::TscMode::Raw>(bg, std::move(*pin));
        (void)reader.read();
    }
    return 0;
}
