#pragma once

// ── crucible::safety::CpuPinned<AffinityMask Mask, PinningPosture, Unit> ─
//
// FIXY-V-187 (Agent 6 §3.3 item 3): a MOVE-ONLY proof token composing the
// V-182 affinity axis (AffinityLattice — a 256-bit CPU mask) with a new
// PINNING-POSTURE axis {NotPinned, PinnedAuto, PinnedExplicit}.  It is the
// witness that "the producing thread was pinned to a specific core at
// construction" — REQUIRED for any value whose bytes come from `__rdtsc`,
// because a TSC read is only meaningful while the thread cannot migrate.
//
//   CpuPinned<Mask, Posture, Unit> carries:
//     - Mask    : AffinityMask    NTTP — WHICH cores the thread is pinned to
//     - Posture : PinningPosture  NTTP — HOW it was pinned (none / auto / explicit)
//     - Unit    : the wrapped value (e.g. the rdtsc timestamp bytes)
//
// ── Move-only (the Linear discipline) ───────────────────────────────
//
// CpuPinned is move-only (copy deleted, move defaulted) — the same
// consume-once discipline as safety::Linear.  This guarantees that at most
// ONE rdtsc reader can hold the proof for a given pin at a time: the V-190
// `mint_tsc_reader(ctx, proof)` takes the proof by-const-ref to read it, and
// a consumer that takes ownership moves it, so the pin claim cannot be
// silently duplicated across two readers racing the same core.
//
//   sizeof(CpuPinned<Mask, Posture, Unit>) == sizeof(Unit) — Mask and
//   Posture are NTTPs (type-level, zero storage); only `Unit value_` is a
//   data member.  Verified by the static_asserts below.
//
// ── The downstream gate (V-190 CtxFitsTscReader) ────────────────────
//
// CpuPinned is permissive at construction (any mask, any posture); the
// SINGLETON + EXPLICIT requirements are enforced where the proof is
// CONSUMED.  V-190's `mint_tsc_reader(ctx, proof)` asserts:
//   (a) ctx owns Init or TestRunner capability,
//   (b) AffinityLattice::is_singleton(Mask)  — exactly one core,
//   (c) SchedulerPolicyLattice::is_at_least(ctx.scheduler_policy, Other).
// CpuPinned exposes `is_singleton_pin` and `posture` so that gate (and the
// HotPath stance) reads them off the type with no runtime data.  The
// RUNTIME admissibility of the pinned mask under cgroup constraints is a
// separate check — `warden::allowed_cpus()` (warden/CpuTopology.h) — done by
// the pinning site, not this type-level proof.
//
// ── PinningPosture order (pin-strength) ─────────────────────────────
//
//   NotPinned (⊥) ⊏ PinnedAuto ⊏ PinnedExplicit (⊤)
//
// PinnedExplicit (an explicit sched_setaffinity to a singleton mask) is the
// strongest; PinnedAuto (the scheduler happened to keep us on one core, or
// a best-effort auto-pin) is weaker — it can still migrate, incurring a
// latency spike.  A HotPath stance therefore requires PinnedExplicit; the
// neg_cpu_pinned_auto_on_hotpath fixture pins that PinnedAuto is rejected.
//
// ── §XVI / row_hash ─────────────────────────────────────────────────
//
// CpuPinned is NOT a Graded carrier (it is a move-only proof, not a
// copyable graded value), so it has no GradedWrapper surface and no
// DimensionAxis.  Its row_hash_contribution (salt 0x32, centralized in
// safety/diag/RowHashFold.h) is specialized HERE rather than in RowHashFold
// — Mask is a CLASS NTTP, and keeping AffinityLattice.h out of the
// widely-included RowHashFold header avoids a compile-cost hit for every
// consumer.  The 256-bit mask words + the posture are folded so distinct
// pins / postures occupy distinct federation-cache slots.
//
//   Axiom coverage:
//     TypeSafe — Mask/Posture are strong NTTPs; two different-mask or
//                different-posture proofs are DISTINCT types.
//     MemSafe / LeakSafe — move-only; copy deleted so a pin is never
//                          duplicated; trivially destructible iff Unit is.
//     InitSafe — NSDMI on value_.
//     DetSafe — the pin proof is the type-level WITNESS that a __rdtsc read
//                is migration-stable.
//
// §XXI: `mint_cpu_pinned<Mask, Posture, Unit>(args...)`.  HS14 fixtures:
// neg_cpu_pinned_rdtsc_without_proof / neg_cpu_pinned_two_bit_mask_singleton
// / neg_cpu_pinned_auto_on_hotpath.

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/AffinityLattice.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <concepts>
#include <cstdint>
#include <cstdlib>      // std::abort in the runtime smoke test
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::AffinityLattice;
using ::crucible::algebra::lattices::AffinityMask;

// ── PinningPosture — HOW the thread was pinned (pin-strength order) ──
enum class PinningPosture : std::uint8_t {
    NotPinned      = 0,    // ⊥ — no affinity set; the thread may migrate freely
    PinnedAuto     = 1,    // best-effort / scheduler-incidental pin (can still migrate)
    PinnedExplicit = 2,    // ⊤ — explicit sched_setaffinity to a singleton mask
};

template <AffinityMask Mask, PinningPosture Posture, typename Unit>
class [[nodiscard]] CpuPinned {
public:
    using value_type = Unit;

    // Type-level proof surface — read by the V-190 mint_tsc_reader gate and
    // the HotPath stance without instantiating the wrapper.
    static constexpr AffinityMask    mask             = Mask;
    static constexpr PinningPosture  posture          = Posture;
    static constexpr bool            is_singleton_pin = AffinityLattice::is_singleton(Mask);
    static constexpr bool            is_pinned        = (Posture != PinningPosture::NotPinned);

private:
    Unit value_{};

public:
    // ── Construction ────────────────────────────────────────────────
    constexpr CpuPinned() noexcept(std::is_nothrow_default_constructible_v<Unit>) = default;

    constexpr explicit CpuPinned(Unit value) noexcept(
        std::is_nothrow_move_constructible_v<Unit>)
        : value_{std::move(value)} {}

    template <typename... Args>
        requires std::is_constructible_v<Unit, Args...>
    constexpr explicit CpuPinned(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<Unit, Args...>)
        : value_{Unit(std::forward<Args>(args)...)} {}

    // ── Move-only (the Linear consume-once discipline) ──────────────
    CpuPinned(const CpuPinned&)            = delete;
    CpuPinned& operator=(const CpuPinned&) = delete;
    constexpr CpuPinned(CpuPinned&&)                 = default;
    constexpr CpuPinned& operator=(CpuPinned&&)      = default;
    ~CpuPinned()                                     = default;

    // ── Access ──────────────────────────────────────────────────────
    [[nodiscard]] constexpr Unit const& peek() const& noexcept { return value_; }
    [[nodiscard]] constexpr Unit& peek_mut() & noexcept { return value_; }
    [[nodiscard]] constexpr Unit consume() &&
        noexcept(std::is_nothrow_move_constructible_v<Unit>)
    { return std::move(value_); }

    // ── runnable_on-style posture gate: does this proof meet a floor? ─
    //
    // TRUE iff this proof's posture is at-or-above the required posture
    // (pin-strength order).  A HotPath stance gates on
    // `meets_posture<PinnedExplicit>`.
    template <PinningPosture Required>
    static constexpr bool meets_posture =
        static_cast<std::uint8_t>(Posture) >= static_cast<std::uint8_t>(Required);
};

// ── §XXI mint factory (token mint — move-only proof) ────────────────
template <AffinityMask Mask, PinningPosture Posture, typename Unit, typename... Args>
    requires std::is_constructible_v<Unit, Args...>
[[nodiscard]] constexpr CpuPinned<Mask, Posture, Unit> mint_cpu_pinned(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<Unit, Args...>)
{
    return CpuPinned<Mask, Posture, Unit>{std::in_place, std::forward<Args>(args)...};
}

// ── Layout invariant — Mask/Posture are NTTPs, so sizeof == sizeof(Unit) ─
static_assert(sizeof(CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>)
              == sizeof(int));
static_assert(sizeof(CpuPinned<AffinityMask::single(7), PinningPosture::PinnedExplicit,
                               unsigned long long>) == sizeof(unsigned long long));
static_assert(!std::is_copy_constructible_v<
    CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>,
    "CpuPinned MUST be move-only — a pin proof cannot be duplicated.");
static_assert(std::is_move_constructible_v<
    CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>);

}  // namespace crucible::safety

// ── row_hash_contribution<CpuPinned<...>> (salt 0x32) ───────────────
//
// Specialized here (not in RowHashFold.h) because Mask is a class NTTP — see
// the WRAPPER_CPU_PINNED_TAG comment in safety/diag/RowHashFold.h.  Folds the
// 256-bit mask words + the posture so distinct pins / postures land in
// distinct federation-cache slots.
namespace crucible::safety::diag {

template <::crucible::algebra::lattices::AffinityMask Mask,
          ::crucible::safety::PinningPosture Posture, typename Inner>
struct row_hash_contribution<::crucible::safety::CpuPinned<Mask, Posture, Inner>> {
private:
    [[nodiscard]] static consteval std::uint64_t fold_mask() noexcept {
        std::uint64_t acc = static_cast<std::uint64_t>(Posture);
        for (std::size_t i = 0; i < ::crucible::algebra::lattices::AffinityMask::kWords; ++i) {
            acc = detail::combine_ids(acc, Mask.words[i]);
        }
        return acc;
    }

public:
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(detail::WRAPPER_CPU_PINNED_TAG, fold_mask()),
        row_hash_contribution_v<Inner>);
};

}  // namespace crucible::safety::diag

// ── Self-test ───────────────────────────────────────────────────────
namespace crucible::safety::detail::cpu_pinned_self_test {

inline constexpr AffinityMask kCore0  = AffinityMask::single(0);
inline constexpr AffinityMask kCore7  = AffinityMask::single(7);
inline constexpr AffinityMask kTwoBit = AffinityMask::range(0, 1);

using PinnedC0   = CpuPinned<kCore0,  PinningPosture::PinnedExplicit, int>;
using AutoC0     = CpuPinned<kCore0,  PinningPosture::PinnedAuto,     int>;
using UnpinnedC0 = CpuPinned<kCore0,  PinningPosture::NotPinned,      int>;
using TwoBitC    = CpuPinned<kTwoBit, PinningPosture::PinnedExplicit, int>;

// ── Construction + access ──────────────────────────────────────────
inline constexpr PinnedC0 c_default{};
static_assert(c_default.peek() == 0);
static_assert(PinnedC0::posture == PinningPosture::PinnedExplicit);

inline constexpr PinnedC0 c_explicit{42};
static_assert(c_explicit.peek() == 42);

inline constexpr PinnedC0 c_in_place{std::in_place, 7};
static_assert(c_in_place.peek() == 7);

// ── Singleton-pin gate (the V-190 mint_tsc_reader requirement) ──────
static_assert( PinnedC0::is_singleton_pin,
    "a single-core pin IS a singleton — admissible for a TSC read.");
static_assert(!TwoBitC::is_singleton_pin,
    "FIXY-V-187: a 2-core mask is NOT a singleton — the TSC reader gate "
    "rejects it (a read across two cores is unsound).");
static_assert(PinnedC0::is_pinned);
static_assert(!UnpinnedC0::is_pinned);

// ── Posture pin-strength order + HotPath gate ───────────────────────
static_assert( PinnedC0::meets_posture<PinningPosture::PinnedExplicit>);
static_assert( PinnedC0::meets_posture<PinningPosture::PinnedAuto>);
static_assert( AutoC0::meets_posture<PinningPosture::PinnedAuto>);
static_assert(!AutoC0::meets_posture<PinningPosture::PinnedExplicit>,
    "FIXY-V-187: PinnedAuto does NOT meet a PinnedExplicit floor — auto "
    "pinning can still migrate (latency spike), so a HotPath stance rejects it.");
static_assert(!UnpinnedC0::meets_posture<PinningPosture::PinnedAuto>);

// ── Distinct types per mask / posture ───────────────────────────────
static_assert(!std::is_same_v<PinnedC0, AutoC0>);
static_assert(!std::is_same_v<PinnedC0, CpuPinned<kCore7, PinningPosture::PinnedExplicit, int>>);

// ── row_hash distinctness ───────────────────────────────────────────
static_assert(diag::row_hash_contribution_v<PinnedC0>
              != diag::row_hash_contribution_v<AutoC0>,
    "different postures MUST hash to distinct slots.");
static_assert(diag::row_hash_contribution_v<PinnedC0>
              != diag::row_hash_contribution_v<CpuPinned<kCore7, PinningPosture::PinnedExplicit, int>>,
    "different pinned cores MUST hash to distinct slots.");
static_assert(diag::row_hash_contribution_v<PinnedC0>
              != diag::row_hash_contribution_v<int>,
    "a CpuPinned proof MUST hash differently from the bare wrapped value.");

// ── consume / peek_mut / move-only ──────────────────────────────────
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

// ── mint_cpu_pinned factory ─────────────────────────────────────────
[[nodiscard]] consteval bool mint_works() noexcept {
    auto p = mint_cpu_pinned<kCore0, PinningPosture::PinnedExplicit, int>(123);
    return p.peek() == 123 && p.is_singleton_pin;
}
static_assert(mint_works());

// ── TSC-reader gate simulation (the V-190 mint_tsc_reader shape) ────
template <typename Proof>
concept admissible_tsc_proof =
    Proof::is_singleton_pin && Proof::template meets_posture<PinningPosture::PinnedExplicit>;

static_assert( admissible_tsc_proof<PinnedC0>,
    "a single-core EXPLICIT pin proof MUST be admissible for a TSC read.");
static_assert(!admissible_tsc_proof<TwoBitC>,
    "a 2-core pin MUST be rejected (not a singleton).");
static_assert(!admissible_tsc_proof<AutoC0>,
    "an AUTO pin MUST be rejected (a TSC reader needs an explicit, "
    "non-migrating pin).");

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    int seed = 21;
    PinnedC0 p{seed * 2};
    if (p.peek() != 42) std::abort();
    p.peek_mut() = 9;
    if (p.peek() != 9) std::abort();

    auto m = mint_cpu_pinned<kCore7, PinningPosture::PinnedExplicit, unsigned long long>(
        static_cast<unsigned long long>(seed));
    if (std::move(m).consume() != 21) std::abort();

    [[maybe_unused]] bool g1 = PinnedC0::is_singleton_pin;
    [[maybe_unused]] bool g2 = TwoBitC::is_singleton_pin;
    if (!g1 || g2) std::abort();

    AutoC0 a{1};
    PinnedC0 moved{std::move(a).consume()};  // value moves out, proof claimed once
    if (moved.peek() != 1) std::abort();
}

}  // namespace crucible::safety::detail::cpu_pinned_self_test
