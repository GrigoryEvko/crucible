// The compile-time checks of fixy/os/CpuPinned.h.

#include <fixy/os/CpuPinned.h>

namespace fixy {

namespace detail {

static_assert(AffinityMask::kBits == CPU_SETSIZE,
              "an affinity mask must hold each CPU that the kernel can pin, and no more");

}  // namespace detail

static_assert(sizeof(CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>)
                  == 2 * sizeof(std::uint64_t),
              "a pin proof holds its unit payload and its pin event, and nothing more");
static_assert(sizeof(CpuPinned<AffinityMask::single(7), PinningPosture::PinnedExplicit, unsigned long long>)
              == sizeof(unsigned long long) + sizeof(std::uint64_t));
static_assert(!std::is_copy_constructible_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>,
              "CpuPinned MUST be move-only — a pin proof cannot be duplicated.");
static_assert(std::is_move_constructible_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>);
static_assert(
    !std::is_trivially_copyable_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>
        && !std::is_implicit_lifetime_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>,
    "std::bit_cast and std::start_lifetime_as must not build a pin proof");

}  // namespace fixy

namespace fixy::detail::cpu_pinned_invariants {

inline constexpr AffinityMask kCore0 = AffinityMask::single(0);
inline constexpr AffinityMask kCore7 = AffinityMask::single(7);
inline constexpr AffinityMask kTwoBit = AffinityMask::range(0, 1);

using PinnedC0 = CpuPinned<kCore0, PinningPosture::PinnedExplicit, int>;
using AutoC0 = CpuPinned<kCore0, PinningPosture::PinnedAuto, int>;
using UnpinnedC0 = CpuPinned<kCore0, PinningPosture::NotPinned, int>;
using TwoBitC = CpuPinned<kTwoBit, PinningPosture::PinnedExplicit, int>;

// The gate, from a scope that mint_affinity does not befriend.  These
// three cells are the whole self-test on the door: each names a route
// that builds a proof out of nothing, and each fails the moment that
// route opens.  test/fixy/neg/ carries the same three as
// negative-compile fixtures, so an open door is caught whether or not
// this header is the thing that was edited.
static_assert(!std::is_default_constructible_v<PinnedC0>,
              "The default constructor of CpuPinned must not be public.  It would claim a pin that nobody performed.");
static_assert(!std::is_constructible_v<PinnedC0, int>,
              "The value constructor of CpuPinned must not be public.  It would claim a pin that nobody performed.");
static_assert(!std::is_constructible_v<PinnedC0, std::in_place_t, int>,
              "CpuPinned must have no in_place constructor.  It would be a third route to the same forgery.");

// Posture and mask are read off the type, so these hold without ever
// building one.
static_assert(PinnedC0::posture == PinningPosture::PinnedExplicit);
static_assert(PinnedC0::mask == kCore0);

static_assert(PinnedC0::is_singleton_pin, "a single-core pin IS a singleton — admissible for a TSC read.");
static_assert(!TwoBitC::is_singleton_pin, "a 2-core mask is NOT a singleton — the TSC reader gate "
                                          "rejects it (a read across two cores is unsound).");
static_assert(PinnedC0::is_pinned);
static_assert(!UnpinnedC0::is_pinned);

static_assert(pin_meets_posture(^^PinnedC0, PinningPosture::PinnedExplicit));
static_assert(pin_meets_posture(^^PinnedC0, PinningPosture::PinnedAuto));
static_assert(pin_meets_posture(^^AutoC0, PinningPosture::PinnedAuto));
static_assert(!pin_meets_posture(^^AutoC0, PinningPosture::PinnedExplicit),
              "PinnedAuto does NOT meet a PinnedExplicit floor — auto "
              "pinning can still migrate, so a HotPath stance rejects it.");
static_assert(!pin_meets_posture(^^UnpinnedC0, PinningPosture::PinnedAuto));
static_assert(!pin_meets_posture(^^int, PinningPosture::NotPinned), "a type that is no pin meets no posture");

static_assert(!std::is_same_v<PinnedC0, AutoC0>);
static_assert(!std::is_same_v<PinnedC0, CpuPinned<kCore7, PinningPosture::PinnedExplicit, int>>);

// NotPinned is refused by the mint's own gate, so this type has no
// constructor at all.  The cell states that, because an unbuildable
// type is easy to reintroduce by accident.
using InitOnlyCtx = ::foundation::effects::ExecCtx<::foundation::effects::Init,
                                                   ::foundation::effects::Row<::foundation::effects::Effect::Init>>;
using ForegroundCtx = ::foundation::effects::ExecCtx<>;

static_assert(CtxFitsAffinityMint<InitOnlyCtx, PinningPosture::PinnedExplicit>);
static_assert(!CtxFitsAffinityMint<InitOnlyCtx, PinningPosture::NotPinned>,
              "there is no proof of NOT being pinned, so the mint must refuse the NotPinned posture.");
static_assert(!CtxFitsAffinityMint<ForegroundCtx, PinningPosture::PinnedExplicit>,
              "the foreground hot path owns neither Bg nor Init, so it must not be able to pin a thread.");

// No check here reaches an accessor, because a proof built out of
// nothing is the forgery this header refuses.  The accessors are
// exercised in test/fixy/test_os_sched.cpp through a pin earned from
// mint_affinity.

}  // namespace fixy::detail::cpu_pinned_invariants
