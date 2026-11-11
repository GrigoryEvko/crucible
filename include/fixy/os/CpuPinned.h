#pragma once

// Proof that the calling thread was pinned to a set of cores.
// A timestamp-counter read is only meaningful while the thread cannot
// migrate, so a value carrying such a read carries this proof with it.
//
// The proof is unforgeable.  Every constructor that builds one is
// private, and the sole friend is fixy::sched::mint_affinity, which
// hands one back only after sched_setaffinity succeeded for the same
// mask.  A caller that never pinned has no path to a CpuPinned, so the
// singleton-and-posture gate on the TSC reader stands on the pin rather
// than on the shape of a token anyone could build.
//
// A pin holds for one thread, and only until that thread pins again.  So
// the proof names one pin event.  Each successful sched_setaffinity of the
// affinity door takes the next value of one count for the process, and
// records it as the pin event in force on the calling thread.  The
// proof keeps the value, and is_in_force() compares it with the value in
// force on the thread that asks.  A proof moved to another thread, or
// kept after its thread pinned again, is not in force, and a consumer
// that reads the counter of one core refuses it.  The proof can move, and
// a move takes the event from the source, so one claim is never held
// two times.  It is neither default-constructible nor constructible from
// a value, because each of those doors claims a pin that nobody
// performed.
//
// THE AFFINITY DOOR
//   detail::sched_setaffinity_sys sets the affinity mask of the calling
//   thread and records the pin event of the change, and
//   detail::sched_getaffinity_sys reads the mask.  Their bodies are in
//   src/fixy/os/CpuPinned.cpp, so a caller pays one call and holds no copy
//   of the door.  Each one issues its one system call through
//   foundation::os::raw_syscall, and the kernel reads or writes the words of
//   an AffinityMask.  So the door includes no header of the kernel and no
//   header of the C library.  A door function that issues one system call
//   NAME out of line has the name NAME_sys, so the rule
//   syscalls counts a call of it as a call of NAME.
//
// A pin that does not go through the door is not recorded.  A
// sched_setaffinity call around the door, a call from another thread that
// names this thread, and a taskset from outside the process move the
// thread with no record, and a proof then stays in force for a mask the
// thread no longer has.  The rule syscalls of the quarantine
// plugin refuses the first two in each file that a build compiles: a
// sched_setaffinity of the C library or of the raw primitive, or a
// pthread_setaffinity_np, outside detail::sched_setaffinity_sys is a
// finding that no ledger row admits.  Nothing refuses the third.
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
// No example forges a pin.  test/fixy/test_os_time.cpp earns its pin
// from mint_affinity, and skips when the cpuset refuses.

#include <foundation/Platform.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>

#include <concepts>
#include <cstdint>
#include <expected>
#include <meta>
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

// The pin event in force on a thread that never pinned through the
// affinity door, and the event of a proof that a move emptied.  The count
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
// other.  Only sched_setaffinity_sys changes it.
CRUCIBLE_PROCESS_WIDE inline constinit ::foundation::core::Atomic<std::uint64_t> pin_event_count{no_pin_event};

// The pin event in force on the calling thread.  is_in_force() reads it in
// the header, so a read of the TSC pays one load of it and no call.  Only
// sched_setaffinity_sys changes it.  The initial-exec model makes each access
// one load or store relative to the thread pointer, also in a library, so no
// access calls the dynamic loader.  The value takes 8 bytes of the static TLS
// of the process.
CRUCIBLE_PROCESS_WIDE [[gnu::tls_model("initial-exec")]] inline constinit thread_local std::uint64_t tls_pin_event =
    no_pin_event;

// Sets the affinity mask of the calling thread to the mask, and records a
// new pin event (THE AFFINITY DOOR).  Returns the event, or the error of the
// kernel.  A failed call leaves the affinity as it was, so the event in force
// stays.  An empty mask is EINVAL of the kernel.
//
// Each change of affinity comes through this function, so a proof of an
// earlier pin on this thread stops being in force.  The function mints
// nothing.  The proof is built by mint_affinity, on its success, and
// nowhere else.  The gated callers are mint_affinity, apply_affinity_to_mask
// and PriorAffinity::restore in fixy/os/Sched.h.  apply_affinity_to_cpu
// calls apply_affinity_to_mask.
[[nodiscard]] ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno>
sched_setaffinity_sys(AffinityMask const& mask) noexcept;

// The affinity mask of the calling thread (THE AFFINITY DOOR), or the error
// of the kernel.  The kernel writes the bits of each CPU that it knows, and
// each other bit of the mask stays zero.  A kernel with more CPUs than the
// mask holds gives EINVAL.  The gated callers are read_affinity and
// apply_affinity_to_mask in fixy/os/Sched.h.
[[nodiscard]] ::foundation::core::Result<AffinityMask, ::foundation::os::Errno> sched_getaffinity_sys() noexcept;

}  // namespace detail

namespace sched {

// The gate on each change to where and how a thread runs.  Five doors in
// fixy/os/Sched.h read it: mint_affinity, mint_scheduler_policy,
// mint_priority, and the two runtime doors apply_affinity_to_mask and
// apply_affinity_to_cpu, which give back no proof.
//
// A context passes when it owns Bg or Init.  A background worker pins
// itself, sets its policy and sets its nice value at startup, and that is
// the primary use of the five doors.  An init context prepares the
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
concept CtxFitsRuntimeAffinity =
    ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Init>;

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
    // event is the one that detail::sched_setaffinity_sys returned for this
    // same mask.
    constexpr CpuPinned(Unit value, std::uint64_t pin_event) noexcept(std::is_nothrow_move_constructible_v<Unit>)
        : value_{std::move(value)}, pin_event_{pin_event} {}

    // The sole friend, and the whole gate.  It is reached only after
    // detail::sched_setaffinity_sys returned a pin event for this same Mask,
    // so a proof in force names a pin of the thread that asks.
    //
    // Keep this friend a function that PERFORMS AND CHECKS the syscall.
    // A friend that merely forwards an argument proves nothing.
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
};

// True when pin reflects a CpuPinned whose posture is at least required.
// The posture comes from the template arguments of the pin, so no member
// of the class plays a part.  The answer is a function at namespace scope
// that is not a template, so no translation unit can specialize it.
[[nodiscard]] consteval bool pin_meets_posture(std::meta::info pin, PinningPosture required) {
    const std::meta::info type = std::meta::dealias(std::meta::remove_cvref(pin));
    if (!std::meta::has_template_arguments(type) || std::meta::template_of(type) != ^^CpuPinned) return false;
    const PinningPosture posture = std::meta::extract<PinningPosture>(std::meta::template_arguments_of(type)[1]);
    return static_cast<std::uint8_t>(posture) >= static_cast<std::uint8_t>(required);
}

}  // namespace fixy
