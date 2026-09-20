#pragma once

// Proof that the thread producing a value was pinned to a set of cores.
// A timestamp-counter read is only meaningful while the thread cannot
// migrate, so a value carrying such a read carries this proof with it.
//
// The token is move-only, so one pin claim cannot be duplicated across
// two readers racing the same core.
//
// Construction is permissive in both the mask and the posture.  The
// tighter requirements, a single-core mask and an explicit pin, belong
// to the site that consumes the proof, which reads them off the type.
// Whether the mask is admissible under the process's cgroup limits is a
// separate run-time question that the pinning site answers.
//
// Old spelling: include/crucible/safety/CpuPinned.h.
//
// OPEN DOOR, reported as a finding and NOT closed in this port.  The
// word "proof" above overstates what the type carries.  Three public
// doors mint one without evidence: mint_cpu_pinned below, and the two
// value constructors.  Only fixy::mint_affinity earns the token, by a
// sched_setaffinity call whose failure it reports.  A forged
// CpuPinned<single(0), PinnedExplicit, T> passes the singleton and the
// posture gate on a thread that never pinned, and the TSC reader then
// compares counters across cores.  The old tree forges one in its own
// smoke test, at include/crucible/fixy/Time.h:286.
//
// The port keeps the old shape so that the flip stage compares like
// with like.  Closing the door needs a passkey whose only friend is
// mint_affinity, which is a change to the published surface.
//
// row_hash_contribution is NOT ported.  The old specialization folds
// the mask words and the posture into the federation key.  The rework
// that replaces all 52 such specializations with one fold over the
// Graded nesting has to land first, and that rework is itself blocked:
// changing the fold changes every published federation key.

#include <foundation/Platform.h>
#include <foundation/algebra/lattices/AffinityLattice.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy {

using ::foundation::algebra::lattices::AffinityLattice;
using ::foundation::algebra::lattices::AffinityMask;

// Ordered by pin strength.
enum class PinningPosture : std::uint8_t {
    NotPinned = 0,  // No affinity set.  The thread migrates freely.
    PinnedAuto = 1,  // Best-effort or incidental pin.  Migration is still possible.
    PinnedExplicit = 2,  // Explicit affinity call.  Migration is excluded.
};

template <AffinityMask Mask, PinningPosture Posture, typename Unit>
class [[nodiscard]] CpuPinned {
public:
    using value_type = Unit;

    static constexpr AffinityMask mask = Mask;
    static constexpr PinningPosture posture = Posture;
    static constexpr bool is_singleton_pin = AffinityLattice::is_singleton(Mask);
    static constexpr bool is_pinned = (Posture != PinningPosture::NotPinned);

private:
    Unit value_{};

public:
    constexpr CpuPinned() noexcept(std::is_nothrow_default_constructible_v<Unit>) = default;

    constexpr explicit CpuPinned(Unit value) noexcept(std::is_nothrow_move_constructible_v<Unit>)
        : value_{std::move(value)} {}

    template <typename... Args>
        requires std::is_constructible_v<Unit, Args...>
    constexpr explicit CpuPinned(std::in_place_t,
                                 Args&&... args) noexcept(std::is_nothrow_constructible_v<Unit, Args...>)
        : value_{Unit(std::forward<Args>(args)...)} {}

    CpuPinned(const CpuPinned&) = delete;
    CpuPinned& operator=(const CpuPinned&) = delete;
    constexpr CpuPinned(CpuPinned&&) = default;
    constexpr CpuPinned& operator=(CpuPinned&&) = default;
    ~CpuPinned() = default;

    [[nodiscard]] constexpr Unit const& peek() const& noexcept { return value_; }
    [[nodiscard]] constexpr Unit& peek_mut() & noexcept { return value_; }
    [[nodiscard]] constexpr Unit consume() && noexcept(std::is_nothrow_move_constructible_v<Unit>) {
        return std::move(value_);
    }

    template <PinningPosture Required>
    static constexpr bool meets_posture = static_cast<std::uint8_t>(Posture) >= static_cast<std::uint8_t>(Required);
};

template <AffinityMask Mask, PinningPosture Posture, typename Unit, typename... Args>
    requires std::is_constructible_v<Unit, Args...>
[[nodiscard]] constexpr CpuPinned<Mask, Posture, Unit>
mint_cpu_pinned(Args&&... args) noexcept(std::is_nothrow_constructible_v<Unit, Args...>) {
    return CpuPinned<Mask, Posture, Unit>{std::in_place, std::forward<Args>(args)...};
}

static_assert(sizeof(CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>) == sizeof(int));
static_assert(sizeof(CpuPinned<AffinityMask::single(7), PinningPosture::PinnedExplicit, unsigned long long>)
              == sizeof(unsigned long long));
static_assert(!std::is_copy_constructible_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>,
              "CpuPinned MUST be move-only — a pin proof cannot be duplicated.");
static_assert(std::is_move_constructible_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>);

}  // namespace fixy

namespace fixy::detail::cpu_pinned_invariants {

inline constexpr AffinityMask kCore0 = AffinityMask::single(0);
inline constexpr AffinityMask kCore7 = AffinityMask::single(7);
inline constexpr AffinityMask kTwoBit = AffinityMask::range(0, 1);

using PinnedC0 = CpuPinned<kCore0, PinningPosture::PinnedExplicit, int>;
using AutoC0 = CpuPinned<kCore0, PinningPosture::PinnedAuto, int>;
using UnpinnedC0 = CpuPinned<kCore0, PinningPosture::NotPinned, int>;
using TwoBitC = CpuPinned<kTwoBit, PinningPosture::PinnedExplicit, int>;

inline constexpr PinnedC0 c_default{};
static_assert(c_default.peek() == 0);
static_assert(PinnedC0::posture == PinningPosture::PinnedExplicit);

inline constexpr PinnedC0 c_explicit{42};
static_assert(c_explicit.peek() == 42);

inline constexpr PinnedC0 c_in_place{std::in_place, 7};
static_assert(c_in_place.peek() == 7);

static_assert(PinnedC0::is_singleton_pin, "a single-core pin IS a singleton — admissible for a TSC read.");
static_assert(!TwoBitC::is_singleton_pin, "a 2-core mask is NOT a singleton — the TSC reader gate "
                                          "rejects it (a read across two cores is unsound).");
static_assert(PinnedC0::is_pinned);
static_assert(!UnpinnedC0::is_pinned);

static_assert(PinnedC0::meets_posture<PinningPosture::PinnedExplicit>);
static_assert(PinnedC0::meets_posture<PinningPosture::PinnedAuto>);
static_assert(AutoC0::meets_posture<PinningPosture::PinnedAuto>);
static_assert(!AutoC0::meets_posture<PinningPosture::PinnedExplicit>,
              "PinnedAuto does NOT meet a PinnedExplicit floor — auto "
              "pinning can still migrate, so a HotPath stance rejects it.");
static_assert(!UnpinnedC0::meets_posture<PinningPosture::PinnedAuto>);

static_assert(!std::is_same_v<PinnedC0, AutoC0>);
static_assert(!std::is_same_v<PinnedC0, CpuPinned<kCore7, PinningPosture::PinnedExplicit, int>>);

// The three row-hash distinctness assertions the old header carried are
// not ported, because the specialization they read is not ported.  The
// row-hash rework restores both together.

[[nodiscard]] consteval bool consume_moves_out() noexcept {
    PinnedC0 p{99};
    return std::move(p).consume() == 99;
}
static_assert(consume_moves_out());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    PinnedC0 p{1};
    p.peek_mut() = 55;
    return p.peek() == 55;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool cpu_pinned_mint_works() noexcept {
    auto p = mint_cpu_pinned<kCore0, PinningPosture::PinnedExplicit, int>(123);
    return p.peek() == 123 && p.is_singleton_pin;
}
static_assert(cpu_pinned_mint_works());

template <typename Proof>
concept admissible_tsc_proof = Proof::is_singleton_pin && Proof::template meets_posture<PinningPosture::PinnedExplicit>;

static_assert(admissible_tsc_proof<PinnedC0>, "a single-core EXPLICIT pin proof MUST be admissible for a TSC read.");
static_assert(!admissible_tsc_proof<TwoBitC>, "a 2-core pin MUST be rejected (not a singleton).");
static_assert(!admissible_tsc_proof<AutoC0>, "an AUTO pin MUST be rejected (a TSC reader needs an explicit, "
                                             "non-migrating pin).");

}  // namespace fixy::detail::cpu_pinned_invariants
