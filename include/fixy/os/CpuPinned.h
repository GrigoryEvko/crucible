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
// A pin holds for one thread, and only until that thread pins again.  So
// the proof names one pin event.  Each successful sched_setaffinity
// through this header takes the next value of one count for the process,
// and records it as the pin event in force on the calling thread.  The
// proof keeps the value, and is_in_force() compares it with the value in
// force on the thread that asks.  A proof moved to another thread, or
// kept after its thread pinned again, is not in force, and a consumer
// that reads the counter of one core refuses it.  The proof can move, and
// a move takes the event from the source, so one claim is never held
// two times.  It is neither default-constructible nor constructible from
// a value: both of those were open doors, and both are closed.
//
// A pin that does not go through this header is not recorded.  A raw
// sched_setaffinity call, a call from another thread that names this
// thread, and a taskset from outside the process move the thread with no
// record, and a proof then stays in force for a mask the thread no
// longer has.  The syscall-capability guard keeps raw calls out of the
// tree, so the first case needs an allowlist line.
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
// Old spelling: include/crucible/safety/_CpuPinned.h.  The old tree
// forges a pin in its own smoke test, at include/crucible/fixy/_Time.h,
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

#include <atomic>
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

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace row_discipline {
template <AffinityMask Mask, PinningPosture Posture>
struct cpu_pinned;
}  // namespace row_discipline

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

// The pin event in force on a thread that never pinned through this
// header, and the event of a proof that a move emptied.  The count
// starts at no_pin_event and each event adds one, so no event is
// no_pin_event.  At one event each nanosecond, the count gets to
// released_pin_event after more than 500 years, so no event of a process
// is released_pin_event.  A thread never has released_pin_event in force,
// and a proof never names no_pin_event.  So one comparison tells an emptied
// proof, a proof of another thread and a stale proof from a proof in
// force.
inline constexpr std::uint64_t no_pin_event = 0;
inline constexpr std::uint64_t released_pin_event = ~std::uint64_t{0};

// One count for the process, whatever shared library pins a thread.  Two
// copies of the count, one in each library, could give two threads one
// event value, and a proof of one thread would then be in force on the
// other.
CRUCIBLE_PROCESS_WIDE inline constinit std::atomic<std::uint64_t> pin_event_count{no_pin_event};

// The pin event in force on the calling thread.
CRUCIBLE_PROCESS_WIDE inline constinit thread_local std::uint64_t tls_pin_event = no_pin_event;

// Sets the affinity of the calling thread to the set, and records a new
// pin event.  Returns the event, or the errno of a failed call.  A failed
// call leaves the affinity as it was, so the event in force stays.
//
// Each change of affinity in the new tree comes through this function,
// so a proof of an earlier pin on this thread stops being in force.  The
// function mints nothing.  The proof is built by mint_affinity, on its
// success, and nowhere else.
[[nodiscard]] inline std::expected<std::uint64_t, int> set_calling_thread_affinity(::cpu_set_t const& set) noexcept {
    if (::sched_setaffinity(0, sizeof(set), &set) != 0)
        [[unlikely]] {  // SYSCALL-CAP-OK: detail helper for mint_affinity and apply_affinity_to_cpu ctx-gates (CtxFitsAffinityMint, CtxFitsRuntimeAffinity)
        return std::unexpected{errno};
    }
    const std::uint64_t event = pin_event_count.fetch_add(1, std::memory_order_acq_rel) + 1;
    tls_pin_event = event;
    return event;
}

// Pins the calling thread to the mask.  Returns the new pin event, or
// the errno of a failed call.
[[nodiscard]] inline std::expected<std::uint64_t, int> pin_calling_thread(AffinityMask mask) noexcept {
    ::cpu_set_t set;
    fill_cpu_set(mask, set);
    return set_calling_thread_affinity(set);
}

}  // namespace detail

namespace sched {

// The gate on each change to where and how a thread runs.  Four doors
// read it: mint_affinity, mint_scheduler_policy and mint_priority in
// fixy/os/Sched.h, and apply_affinity_to_cpu, which is the runtime door
// with no proof to give back.
//
// A context passes when it owns Bg or Init.  A background worker pins
// itself, sets its policy and sets its nice value at startup, and that is
// the primary use of the four doors.  An init context prepares the
// threads that it starts.
//
// The foreground hot path owns neither effect, and the gate refuses it.
// A new pin moves the thread between two recorded operations, so two
// timestamp reads come from two cores.  A new policy or nice value moves
// each deadline that the replay measured.  A test context also owns
// neither effect.  A test that must pin makes a background or an init
// context, as production code does.
//
// A gate on Init alone is too narrow.  It refuses a background worker
// that pins itself at startup.
template <typename Ctx>
concept CtxFitsRuntimeAffinity = ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Bg,
                                                                     ::foundation::effects::Effect::Init>;

}  // namespace sched

template <typename Ctx, PinningPosture Posture>
concept CtxFitsAffinityMint = sched::CtxFitsRuntimeAffinity<Ctx> && (Posture != PinningPosture::NotPinned);

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
    using row_discipline = ::fixy::row_discipline::cpu_pinned<Mask, Posture>;
    using row_payload = Unit;

private:
    Unit value_{};
    std::uint64_t pin_event_ = detail::released_pin_event;

    // The only constructor that builds a proof, and it is private.  The
    // event is the one that set_calling_thread_affinity returned for this
    // same mask.
    constexpr CpuPinned(Unit value, std::uint64_t pin_event) noexcept(std::is_nothrow_move_constructible_v<Unit>)
        : value_{std::move(value)}, pin_event_{pin_event} {}

    // The sole friend, and the whole gate.  It is reached only after
    // detail::pin_calling_thread returned a pin event for this same Mask,
    // so a proof in force names a pin of the thread that asks.
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
    // User-provided, and not defaulted.  A deleted copy with a defaulted
    // move still leaves the class trivially copyable, and then
    // std::bit_cast<CpuPinned<...>>(0) builds a pin with no syscall.  A
    // user-provided move makes the class neither trivially copyable nor
    // an implicit-lifetime type.  The move takes the event from the
    // source, so the source is not in force after the move.
    constexpr CpuPinned(CpuPinned&& other) noexcept(std::is_nothrow_move_constructible_v<Unit>)
        : value_{std::move(other.value_)}, pin_event_{std::exchange(other.pin_event_, detail::released_pin_event)} {}
    constexpr CpuPinned& operator=(CpuPinned&& other) noexcept(std::is_nothrow_move_assignable_v<Unit>) {
        value_ = std::move(other.value_);
        pin_event_ = std::exchange(other.pin_event_, detail::released_pin_event);
        return *this;
    }
    ~CpuPinned() = default;

    // True when the pin event of this proof is the one in force on the
    // calling thread.  A proof of another thread, a proof whose thread
    // pinned again and a proof that a move emptied give false.  The cost
    // is one load of a thread-local value and one comparison.
    [[nodiscard]] bool is_in_force() const noexcept { return pin_event_ == detail::tls_pin_event; }

    [[nodiscard]] constexpr Unit const& peek() const& noexcept { return value_; }
    [[nodiscard]] constexpr Unit& peek_mut() & noexcept { return value_; }
    [[nodiscard]] constexpr Unit consume() && noexcept(std::is_nothrow_move_constructible_v<Unit>) {
        return std::move(value_);
    }

    template <PinningPosture Required>
    static constexpr bool meets_posture = static_cast<std::uint8_t>(Posture) >= static_cast<std::uint8_t>(Required);
};

static_assert(sizeof(CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>)
                  == 2 * sizeof(std::uint64_t),
              "a pin proof holds its unit payload and its pin event, and nothing more");
static_assert(sizeof(CpuPinned<AffinityMask::single(7), PinningPosture::PinnedExplicit, unsigned long long>)
              == sizeof(unsigned long long) + sizeof(std::uint64_t));
static_assert(!std::is_copy_constructible_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>,
              "CpuPinned MUST be move-only — a pin proof cannot be duplicated.");
static_assert(std::is_move_constructible_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>);
static_assert(!std::is_trivially_copyable_v<CpuPinned<AffinityMask::single(0), PinningPosture::PinnedExplicit, int>>
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
using InitOnlyCtx = ::foundation::effects::ExecCtx<::foundation::effects::Init,
                                                   ::foundation::effects::Row<::foundation::effects::Effect::Init>>;
using ForegroundCtx = ::foundation::effects::ExecCtx<>;

static_assert(CtxFitsAffinityMint<InitOnlyCtx, PinningPosture::PinnedExplicit>);
static_assert(!CtxFitsAffinityMint<InitOnlyCtx, PinningPosture::NotPinned>,
              "there is no proof of NOT being pinned, so the mint must refuse the NotPinned posture.");
static_assert(!CtxFitsAffinityMint<ForegroundCtx, PinningPosture::PinnedExplicit>,
              "the foreground hot path owns neither Bg nor Init, so it must not be able to pin a thread.");

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
