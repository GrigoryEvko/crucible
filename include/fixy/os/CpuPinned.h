#pragma once

// Proof that the calling thread was pinned to a set of cores.
// A timestamp-counter read is only meaningful while the thread cannot
// migrate, so a value carrying such a read carries this proof with it.
//
// The proof is unforgeable.  Every constructor that builds one is
// private, and the sole friend is fixy::sched::mint_affinity, which
// hands one back only after sched_setaffinity succeeded for the same
// mask.  A caller that never pinned has no path to a CpuPinned, so the
// singleton-and-posture gate on the TSC reader now stands on the pin
// rather than on the shape of a token anyone could build.
//
// The token is move-only, so one pin claim cannot be duplicated across
// two readers racing the same core.  It is neither default-constructible
// nor constructible from a value: both of those were open doors, and
// both are closed.  Moving one transfers the claim rather than copying
// it, which is why the move operations stay public.
//
// Posture is a parameter of the mint rather than a fact the syscall
// reports, because the syscall reports only success.  Asking for
// PinnedAuto while pinning explicitly under-claims, and a consumer that
// demands PinnedExplicit rejects it, so under-claiming is safe.
// NotPinned is refused by the mint's own gate, which leaves
// CpuPinned<M, NotPinned, U> nameable and unbuildable.  That is the
// right shape: there is no such thing as a proof of not being pinned.
//
// Unit stays a parameter, but only PinProofUnit is ever built, because
// the sole friend fixes it.  Other units remain nameable so the shape
// checks here and in fixy/os/Time.h can keep naming them.
//
// Old spelling: include/crucible/safety/CpuPinned.h.  The old tree
// forges a pin in its own smoke test, at include/crucible/fixy/Time.h,
// and reads the counter through it.  That is how the open door survived
// review: the canonical example of using CpuPinned was an example of
// forging it.  The leg is not ported in that shape.  test/fixy/
// test_os_time.cpp earns its pin from mint_affinity instead, and skips
// when the cpuset refuses.
//
// row_hash_contribution is NOT ported.  The old specialization folds
// the mask words and the posture into the federation key.  The rework
// that replaces all 52 such specializations with one fold over the
// Graded nesting has to land first, and that rework is itself blocked:
// changing the fold changes every published federation key.

#include <foundation/Platform.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/effects/Ctx.h>

#include <sched.h>

#include <cerrno>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
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
class CpuPinned;

// The payload of a pinning proof carries nothing.  The authority is the
// type, so a unit value is enough.
using PinProofUnit = int;

namespace detail {

// AffinityMask::kBits is smaller than CPU_SETSIZE, so every set bit is a
// valid CPU_SET index and the loop needs no bound check of its own.
CRUCIBLE_INLINE void fill_cpu_set(AffinityMask mask, ::cpu_set_t& set) noexcept {
    CPU_ZERO(&set);
    for (std::uint16_t core = 0; core < AffinityMask::kBits; ++core) {
        if (((mask.words[core / 64] >> (core % 64)) & 1ULL) != 0ULL) {
            CPU_SET(static_cast<std::size_t>(core), &set);
        }
    }
}

// Pins the calling thread and reports the failure.  Returns 0 on
// success, the errno otherwise.
//
// This helper is reachable, and that is deliberate: calling it pins the
// thread and hands back an int.  It mints nothing.  The proof is built
// by mint_affinity, on this helper's success, and nowhere else.
[[nodiscard]] inline int pin_calling_thread(AffinityMask mask) noexcept {
    ::cpu_set_t set;
    fill_cpu_set(mask, set);
    if (::sched_setaffinity(0, sizeof(set), &set) != 0)
        [[unlikely]] {  // SYSCALL-CAP-OK: detail helper for mint_affinity ctx-gate (CtxFitsAffinityMint)
        return errno;
    }
    return 0;
}

}  // namespace detail

template <typename Ctx, PinningPosture Posture>
concept CtxFitsAffinityMint = ::foundation::effects::IsExecCtx<Ctx> && (Posture != PinningPosture::NotPinned);

// Declared here and defined in fixy/os/Sched.h, beside the other
// scheduling mints.  The declaration has to live here because the class
// below names it as its sole friend, and a friend must already have
// been declared.  The default argument belongs to this declaration and
// is therefore absent from both the friend declaration and the
// definition.
namespace sched {

// §XXI carve-out: cx=alloc — setting affinity is a kernel side effect.
template <AffinityMask Mask, PinningPosture Posture = PinningPosture::PinnedExplicit,
          ::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::CtxFitsAffinityMint<Ctx, Posture>
[[nodiscard]] std::expected<::fixy::CpuPinned<Mask, Posture, ::fixy::PinProofUnit>, int>
mint_affinity(Ctx const&) noexcept;

}  // namespace sched

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

    // The only constructor that builds a proof, and it is private.
    constexpr explicit CpuPinned(Unit value) noexcept(std::is_nothrow_move_constructible_v<Unit>)
        : value_{std::move(value)} {}

    // The sole friend, and the whole gate.  It is reached only after
    // detail::pin_calling_thread returned 0 for this same Mask, so a
    // caller holding a CpuPinned has pinned the thread it runs on.
    //
    // Keep this friend a function that PERFORMS AND CHECKS the syscall.
    // A friend that merely forwards an argument would prove nothing, and
    // that is exactly what the three constructors this replaced did.
    // The trailing return type is not a style choice.  Written the other
    // way round the declaration ends `..., int> ::fixy::sched::...`, and
    // the parser takes the `>::` as a nested-name-specifier inside the
    // template-id, so it reports "expected ')' before 'const'" on the
    // parameter.  Leading `auto` puts the qualified name after a plain
    // identifier and the ambiguity disappears.  Matching against the
    // leading-return-type declaration above still succeeds: the two
    // spell the same type.
    template <AffinityMask FriendMask, PinningPosture FriendPosture, ::foundation::effects::IsExecCtx FriendCtx>
        requires ::fixy::CtxFitsAffinityMint<FriendCtx, FriendPosture>
    friend auto ::fixy::sched::mint_affinity(FriendCtx const&) noexcept
        -> std::expected<::fixy::CpuPinned<FriendMask, FriendPosture, ::fixy::PinProofUnit>, int>;

public:
    CpuPinned() = delete("a default-constructed CpuPinned would claim a pin nobody performed.  Take one from "
                         "fixy::sched::mint_affinity, which returns it only after sched_setaffinity succeeded.");
    CpuPinned(const CpuPinned&) = delete("a pin proof cannot be duplicated — two readers would race one core.");
    CpuPinned& operator=(const CpuPinned&) = delete("a pin proof cannot be duplicated.");
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

// The gate, from a scope that mint_affinity does not befriend.  These
// three cells are the whole self-test on the door: each names a route
// that built a proof out of nothing before, and each fails the moment
// that route reopens.  test/fixy/neg/ carries the same three as
// negative-compile fixtures, so a reopened door is caught whether or not
// this header is the thing that was edited.
static_assert(!std::is_default_constructible_v<PinnedC0>,
              "The default constructor of CpuPinned must not be public.  It claimed a pin that nobody performed.");
static_assert(!std::is_constructible_v<PinnedC0, int>,
              "The value constructor of CpuPinned must not be public.  It claimed a pin that nobody performed.");
static_assert(!std::is_constructible_v<PinnedC0, std::in_place_t, int>,
              "The in_place constructor of CpuPinned is removed, not hidden.  It was a third route to the same "
              "forgery.");

// Posture and mask are read off the type, so these hold without ever
// building one.
static_assert(PinnedC0::posture == PinningPosture::PinnedExplicit);
static_assert(PinnedC0::mask == kCore0);

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

// NotPinned is refused by the mint's own gate, so this type has no
// constructor at all.  The cell states that, because an unbuildable
// type is easy to reintroduce by accident.
static_assert(!CtxFitsAffinityMint<::foundation::effects::ExecCtx<::foundation::effects::Init,
                                                                 ::foundation::effects::Row<
                                                                     ::foundation::effects::Effect::Init>>,
                                   PinningPosture::NotPinned>,
              "there is no proof of NOT being pinned, so the mint must refuse the NotPinned posture.");

// The three row-hash distinctness assertions the old header carried are
// not ported, because the specialization they read is not ported.  The
// row-hash rework restores both together.
//
// consume_moves_out, peek_mut_works and cpu_pinned_mint_works are not
// ported in this shape.  Each built a proof out of nothing to reach the
// accessor it was testing, which is the forgery this header now
// refuses.  The accessors are exercised in test/fixy/test_os_sched.cpp
// through a pin earned from mint_affinity.

}  // namespace fixy::detail::cpu_pinned_invariants
