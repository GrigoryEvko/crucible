#pragma once

// Proof that the thread producing a value was pinned to a set of cores.
// A timestamp-counter read is only meaningful while the thread cannot
// migrate, so a value carrying such a read carries this proof with it.
//
// The token is move-only, so one pin claim cannot be duplicated across
// two readers racing the same core.
//
// The proof is unforgeable.  Its one constructor is private, and the sole
// friend is crucible::fixy::sched::detail::cpu_pin_access, whose builder
// only crucible::fixy::sched::mint_affinity reaches, after
// sched_setaffinity succeeded for the same mask.  There is no default
// constructor, no in_place constructor and no free mint: each built a pin
// that nobody performed.  include/fixy/os/CpuPinned.h carries the same
// shape in the new tree.
//
// The mask and the posture stay permissive.  The tighter requirements, a
// single-core mask and an explicit pin, belong to the site that consumes
// the proof, which reads them off the type.

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/_AffinityLattice.h>
#include <crucible/safety/diag/_RowHashFold.h>

#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace crucible::fixy::sched::detail {
// The door that builds a pin proof, defined in include/crucible/fixy/Sched.h
// beside mint_affinity, which is its only caller.
struct cpu_pin_access;
}  // namespace crucible::fixy::sched::detail

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

    constexpr explicit CpuPinned(Unit value) noexcept(std::is_nothrow_move_constructible_v<Unit>)
        : value_{std::move(value)} {}

    friend struct ::crucible::fixy::sched::detail::cpu_pin_access;

public:
    CpuPinned() = delete("a default-constructed CpuPinned would claim a pin nobody performed.  Take one from "
                         "crucible::fixy::sched::mint_affinity, which returns it only after sched_setaffinity "
                         "succeeded.");
    CpuPinned(const CpuPinned&) = delete("a pin proof cannot be duplicated: two readers would race one core.");
    CpuPinned& operator=(const CpuPinned&) = delete("a pin proof cannot be duplicated.");
    // User-provided, so the class is not trivially copyable and
    // std::bit_cast cannot build a pin from bytes.
    constexpr CpuPinned(CpuPinned&& other) noexcept(std::is_nothrow_move_constructible_v<Unit>)
        : value_{std::move(other.value_)} {}
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

static_assert(sizeof(CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>) == sizeof(int));
static_assert(!std::is_default_constructible_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>
                  && !std::is_constructible_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>,
                                              int>
                  && !std::is_trivially_copyable_v<
                      CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>,
              "a pin proof comes only from mint_affinity: no default, value or byte route may build one");
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

// The value cells that stood here built a pin out of nothing, which is the
// forgery the closed constructor refuses.  Posture and mask are read off
// the type, so the cells below hold without building one.
static_assert(PinnedC0::posture == PinningPosture::PinnedExplicit);

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

template <typename Proof>
concept admissible_tsc_proof = Proof::is_singleton_pin && Proof::template meets_posture<PinningPosture::PinnedExplicit>;

static_assert(admissible_tsc_proof<PinnedC0>, "a single-core EXPLICIT pin proof MUST be admissible for a TSC read.");
static_assert(!admissible_tsc_proof<TwoBitC>, "a 2-core pin MUST be rejected (not a singleton).");
static_assert(!admissible_tsc_proof<AutoC0>, "an AUTO pin MUST be rejected (a TSC reader needs an explicit, "
                                             "non-migrating pin).");

// The accessors are exercised through a pin earned from mint_affinity in
// test/test_fixy_v_191_sched.cpp.  This smoke test reads the gates at run
// time, which needs no pin.
inline void runtime_smoke_test() {
    [[maybe_unused]] bool g1 = PinnedC0::is_singleton_pin;
    [[maybe_unused]] bool g2 = TwoBitC::is_singleton_pin;
    if (!g1 || g2) std::abort();
    [[maybe_unused]] bool g3 = AutoC0::meets_posture<PinningPosture::PinnedExplicit>;
    if (g3) std::abort();
}

}  // namespace crucible::safety::detail::cpu_pinned_self_test
