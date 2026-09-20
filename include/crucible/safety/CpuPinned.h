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

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/_AffinityLattice.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::AffinityLattice;
using ::crucible::algebra::lattices::AffinityMask;

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

}  // namespace crucible::safety

// This specialization lives here rather than beside its sibling folds
// because the mask is a class template parameter.  Declaring it
// centrally would pull the affinity lattice into a header that almost
// every translation unit includes, at a compile-time cost none of them
// need.
namespace crucible::safety::diag {

template <::crucible::algebra::lattices::AffinityMask Mask, ::crucible::safety::PinningPosture Posture, typename Inner>
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
        detail::combine_ids(detail::WRAPPER_CPU_PINNED_TAG, fold_mask()), row_hash_contribution_v<Inner>);
};

}  // namespace crucible::safety::diag

namespace crucible::safety::detail::cpu_pinned_self_test {

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

static_assert(diag::row_hash_contribution_v<PinnedC0> != diag::row_hash_contribution_v<AutoC0>,
              "different postures MUST hash to distinct slots.");
static_assert(diag::row_hash_contribution_v<PinnedC0>
                  != diag::row_hash_contribution_v<CpuPinned<kCore7, PinningPosture::PinnedExplicit, int>>,
              "different pinned cores MUST hash to distinct slots.");
static_assert(diag::row_hash_contribution_v<PinnedC0> != diag::row_hash_contribution_v<int>,
              "a CpuPinned proof MUST hash differently from the bare wrapped value.");

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
    PinnedC0 moved{std::move(a).consume()};
    if (moved.peek() != 1) std::abort();
}

}  // namespace crucible::safety::detail::cpu_pinned_self_test
