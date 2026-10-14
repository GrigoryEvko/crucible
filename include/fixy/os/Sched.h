#pragma once

// Setting a real-time policy, a deadline budget, or a lower nice value needs
// CAP_SYS_NICE and fails for an unprivileged thread. A proof that claims the
// thread is pinned, real-time or niced must not exist when the syscall failed,
// so every mint here returns the error number of the kernel and lets the
// caller decide.

#include <fixy/Ctx.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/SchedClass.h>
#include <fixy/os/ThreadName.h>
#include <foundation/Lifetime.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/core/Choice.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>
#include <foundation/os/SchedWords.h>

#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

namespace fixy::sched {

// sf names ::fixy, where the pin proof and the scheduling-class proof
// live.
namespace sf = ::fixy;
namespace eff = ::foundation::effects;
namespace ml = ::foundation::algebra::lattices;

using sf::PinningPosture;
using sf::SchedulerPolicy_v;
using ml::AffinityMask;

// The payload of a scheduling proof carries nothing. The authority is the
// type, so a unit value is enough.
using ProofUnit = int;

// The nice value is a flat integer over [-20, 19] rather than a lattice, so
// no graded wrapper fits and this surface owns the witness.
//
// The witness is a proof that setpriority succeeded for the calling
// thread.  So it has the shape of the scheduling-class proof in
// fixy/os/SchedClass.h.  Its one constructor takes the key of
// SchedProofDoor, and only the door builds that key, after the syscall of
// mint_priority returned.  The witness is neither copyable nor movable,
// because the nice value belongs to the thread that set it, and a copy or
// a move could carry the claim to another thread.
template <int Nice>
    requires(Nice >= -20 && Nice <= 19)
class [[nodiscard]] SchedPriority final {
public:
    static constexpr int nice = Nice;
    using row_discipline = SchedPriority;
    using row_payload = ::foundation::diag::row_payloads<>;

    // User-provided, so the type is not an aggregate and not an
    // implicit-lifetime type.
    explicit constexpr SchedPriority(::foundation::algebra::grade_key<SchedProofDoor> const&) noexcept {}

    SchedPriority(const SchedPriority&) = delete("a nice value belongs to the thread that set it, and a copy could "
                                                 "reach another thread");
    SchedPriority(SchedPriority&&) = delete("a nice value belongs to the thread that set it, and a move could carry "
                                            "the claim to another thread");
    SchedPriority& operator=(const SchedPriority&) = delete("a priority proof is not assignable");
    SchedPriority& operator=(SchedPriority&&) = delete("a priority proof is not assignable");
    ~SchedPriority() = default;

private:
    // No byte route builds a proof, so no proof claims a nice value that no
    // syscall set.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

namespace detail {

// The policy number of the kernel for each policy, from
// foundation/os/SchedWords.h, or -1 for a value outside the enumeration.
[[nodiscard]] consteval int sched_policy_constant(SchedulerPolicy_v policy) noexcept {
    switch (policy) {
        case SchedulerPolicy_v::Idle:
            return ::foundation::os::kSchedIdle;
        case SchedulerPolicy_v::Batch:
            return ::foundation::os::kSchedBatch;
        case SchedulerPolicy_v::Other:
            return ::foundation::os::kSchedNormal;
        case SchedulerPolicy_v::RoundRobin:
            return ::foundation::os::kSchedRoundRobin;
        case SchedulerPolicy_v::Fifo:
            return ::foundation::os::kSchedFifo;
        case SchedulerPolicy_v::Deadline:
            return ::foundation::os::kSchedDeadline;
        default:
            return -1;
    }
}

// THE SCHEDULING DOOR.  Each function issues its one system call for the
// calling thread through foundation::os::raw_syscall, and gives the error of
// the kernel.  The bodies are in src/fixy/os/Sched.cpp, which includes no
// header of the kernel and no header of the C library.  A function that
// issues one system call NAME out of line has the name NAME_sys (THE
// AFFINITY DOOR of fixy/os/CpuPinned.h).

// Sets the policy of the calling thread, with the real-time priority that
// SCHED_FIFO and SCHED_RR read and that each other policy needs as zero.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sched_setscheduler_sys(int policy, int rt_priority) noexcept;

// Sets the policy SCHED_DEADLINE of the calling thread, with its budget in
// nanoseconds.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sched_setattr_sys(std::uint64_t runtime_ns, std::uint64_t deadline_ns, std::uint64_t period_ns) noexcept;

// Sets the nice value of the calling thread.  On Linux, the target of the
// call is the thread, and not each thread of the process.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
setpriority_sys(int nice) noexcept;

template <SchedulerPolicy_v Policy>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
apply_scheduler_policy(int rt_priority, std::uint64_t runtime_ns, std::uint64_t deadline_ns,
                       std::uint64_t period_ns) noexcept {
    if constexpr (Policy == SchedulerPolicy_v::Deadline) {
        return sched_setattr_sys(runtime_ns, deadline_ns, period_ns);
    } else {
        return sched_setscheduler_sys(sched_policy_constant(Policy), rt_priority);
    }
}

}  // namespace detail

// CtxFitsAffinityMint is in fixy/os/CpuPinned.h with mint_affinity's
// declaration, because the proof names that mint as its sole friend and
// a friend must already have been declared.  The
// definition below names the gate as ::fixy::CtxFitsAffinityMint, the
// spelling of the declaration.

// The second conjunct is not a restatement of the enum's own range,
// though it reads like one.  It is the
// only compile-time rejection of a policy outside the enum: SchedClass
// accepts any SchedulerPolicy_v value, including a cast one, and renders
// it as the At<?> sentinel.  Without this conjunct,
// mint_scheduler_policy<static_cast<SchedulerPolicy_v>(200)> compiles,
// calls sched_setscheduler with a policy of -1, and turns a compile
// error into a runtime EINVAL.  Two pins in the check file of this
// header witness both arms.
//
// The first conjunct is the scheduling-authority gate every door here
// shares.  The rationale is written once, on the concept, in
// fixy/os/CpuPinned.h.
template <typename Ctx, SchedulerPolicy_v Policy>
concept CtxFitsSchedPolicyMint = CtxFitsRuntimeAffinity<Ctx> && (detail::sched_policy_constant(Policy) >= 0);

// The [-20, 19] bound here reads like a restatement of the one on
// SchedPriority<Nice>.  It is not.  The mint names SchedPriority<Nice>
// in its return type, so without this conjunct an out-of-range nice
// reaches that return type during substitution, and a constraint
// failure on a CLASS template is not in the immediate context of the
// function template.  GCC 16 reports it as a hard error rather than
// discarding the candidate, so the call site cannot recover and no
// overload set can absorb it.  Measured, not reasoned: the deletion was
// written, compiled, and reverted on the diagnostic.
//
// The first conjunct is the shared scheduling-authority gate, as above.
template <typename Ctx, int Nice>
concept CtxFitsPriorityMint = CtxFitsRuntimeAffinity<Ctx> && (Nice >= -20 && Nice <= 19);

// The definition of the mint declared in fixy/os/CpuPinned.h.  It lives
// here, beside the other scheduling mints, and it is the sole friend of
// CpuPinned's only constructor.  The default template argument belongs
// to the declaration and must not be repeated, and the requires-clause
// is spelled exactly as it is there so the two match.
//
// This is the one door that earns a CpuPinned.  The proof comes back
// only after sched_setaffinity returned 0 for this same mask, and no
// other path to one exists.
template <AffinityMask Mask, PinningPosture Posture, eff::IsExecCtx Ctx>
    requires ::fixy::CtxFitsAffinityMint<Ctx, Posture>
[[nodiscard]] std::expected<sf::CpuPinned<Mask, Posture, sf::PinProofUnit>, int> mint_affinity(Ctx const&) noexcept {
    ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno> pin_event =
        sf::detail::sched_setaffinity_sys(Mask);
    for (::foundation::os::Errno const error : pin_event.err()) {
        return std::unexpected(::foundation::os::error_number(error));
    }
    return sf::CpuPinned<Mask, Posture, sf::PinProofUnit>{0, std::move(pin_event).expect("the pin succeeded above")};
}

// Deadline admission requires runtime < deadline <= period. The SchedClass
// return type asserts that ordering, so a budget that violates it is a
// compile error inside the mint and needs no separate concept.
//
// The two mints below are declared here and defined after SchedProofDoor,
// which names each one as a friend.  The default arguments belong to this
// declaration.
template <SchedulerPolicy_v Policy, std::uint64_t RuntimeNs = 0, std::uint64_t DeadlineNs = 0,
          std::uint64_t PeriodNs = 0, eff::IsExecCtx Ctx>
    requires CtxFitsSchedPolicyMint<Ctx, Policy>
[[nodiscard]] std::expected<sf::SchedClass<Policy, ProofUnit, RuntimeNs, DeadlineNs, PeriodNs>, int>
mint_scheduler_policy(Ctx const&, int rt_priority = 0) noexcept;

template <int Nice, eff::IsExecCtx Ctx>
    requires CtxFitsPriorityMint<Ctx, Nice>
[[nodiscard]] std::expected<SchedPriority<Nice>, int> mint_priority(Ctx const&) noexcept;

// The door to the two scheduling proofs.  No object of it exists.  It is
// the authority of the key that the constructor of SchedClass and of
// SchedPriority takes, so only its members build that key.  Its members
// are private, and its two friends are the mints above, which call a
// member only after their syscall returned.  Each member builds its proof
// in place inside the result, because a proof neither copies nor moves.
//
// The trailing return types are necessary: fixy/os/CpuPinned.h gives the
// parse reason.
class SchedProofDoor final : ::foundation::NoObject<SchedProofDoor> {
    template <SchedulerPolicy_v FriendPolicy, std::uint64_t FriendRuntimeNs, std::uint64_t FriendDeadlineNs,
              std::uint64_t FriendPeriodNs, eff::IsExecCtx FriendCtx>
        requires CtxFitsSchedPolicyMint<FriendCtx, FriendPolicy>
    friend auto mint_scheduler_policy(FriendCtx const&, int) noexcept
        -> std::expected<sf::SchedClass<FriendPolicy, ProofUnit, FriendRuntimeNs, FriendDeadlineNs, FriendPeriodNs>,
                         int>;

    template <int FriendNice, eff::IsExecCtx FriendCtx>
        requires CtxFitsPriorityMint<FriendCtx, FriendNice>
    friend auto mint_priority(FriendCtx const&) noexcept -> std::expected<SchedPriority<FriendNice>, int>;

    using key_ = ::foundation::algebra::grade_key<SchedProofDoor>;

    template <SchedulerPolicy_v Policy, std::uint64_t RuntimeNs, std::uint64_t DeadlineNs, std::uint64_t PeriodNs>
    [[nodiscard]] static std::expected<sf::SchedClass<Policy, ProofUnit, RuntimeNs, DeadlineNs, PeriodNs>, int>
    policy_proof_(int rt_priority) noexcept {
        const key_ key{};
        return std::expected<sf::SchedClass<Policy, ProofUnit, RuntimeNs, DeadlineNs, PeriodNs>, int>{std::in_place,
                                                                                                      key, rt_priority};
    }

    template <int Nice>
    [[nodiscard]] static std::expected<SchedPriority<Nice>, int> priority_proof_() noexcept {
        const key_ key{};
        return std::expected<SchedPriority<Nice>, int>{std::in_place, key};
    }
};

template <SchedulerPolicy_v Policy, std::uint64_t RuntimeNs, std::uint64_t DeadlineNs, std::uint64_t PeriodNs,
          eff::IsExecCtx Ctx>
    requires CtxFitsSchedPolicyMint<Ctx, Policy>
[[nodiscard]] std::expected<sf::SchedClass<Policy, ProofUnit, RuntimeNs, DeadlineNs, PeriodNs>, int>
mint_scheduler_policy(Ctx const&, int rt_priority) noexcept {
    if (int const error = ::foundation::os::error_number_of(
            detail::apply_scheduler_policy<Policy>(rt_priority, RuntimeNs, DeadlineNs, PeriodNs));
        error != 0) {
        return std::unexpected(error);
    }
    return SchedProofDoor::policy_proof_<Policy, RuntimeNs, DeadlineNs, PeriodNs>(rt_priority);
}

// The nice value is a value of the calling thread, not of the whole process
// (setpriority_sys).
template <int Nice, eff::IsExecCtx Ctx>
    requires CtxFitsPriorityMint<Ctx, Nice>
[[nodiscard]] std::expected<SchedPriority<Nice>, int> mint_priority(Ctx const&) noexcept {
    if (int const error = ::foundation::os::error_number_of(detail::setpriority_sys(Nice)); error != 0) {
        return std::unexpected(error);
    }
    return SchedProofDoor::priority_proof_<Nice>();
}

using ::fixy::mint_thread_name;

// The affinity mask of the calling thread, or the error of the kernel.  The
// read changes nothing, and it records no pin event.  It is not a mint,
// because it gives a fact of the kernel and no authority.  Its gate is the
// gate of the other affinity doors, CtxFitsRuntimeAffinity in
// fixy/os/CpuPinned.h.
template <eff::IsExecCtx Ctx>
    requires CtxFitsRuntimeAffinity<Ctx>
[[nodiscard]] ::foundation::core::Result<AffinityMask, ::foundation::os::Errno> read_affinity(Ctx const&) noexcept {
    return sf::detail::sched_getaffinity_sys();
}

class PriorAffinity;

template <eff::IsExecCtx Ctx>
    requires CtxFitsRuntimeAffinity<Ctx>
[[nodiscard]] auto apply_affinity_to_mask(Ctx const&, AffinityMask mask) noexcept -> std::expected<PriorAffinity, int>;

// The affinity mask that one pin of apply_affinity_to_mask replaced, and the
// authority to put it back.  Only that door builds one, from the mask that
// it read before its pin, so the authority to restore is the authority of
// the pin.  restore() goes through the recording door of fixy/os/CpuPinned.h,
// so a proof of the pin stops being in force.
//
// The mask belongs to the thread that pinned.  restore() on another thread
// changes nothing and returns EPERM, because the recording door sets the
// mask of the calling thread only.  A drop keeps the pin.  A move takes the
// mask from the source, so one pin is restored at most one time.
class [[nodiscard]] PriorAffinity final {
public:
    PriorAffinity(const PriorAffinity&) = delete("a copy would restore one prior mask two times");
    PriorAffinity& operator=(const PriorAffinity&) = delete("a copy would restore one prior mask two times");
    PriorAffinity(PriorAffinity&& other) noexcept
        : mask_{other.mask_}, owner_{other.owner_}, is_held_{std::exchange(other.is_held_, false)} {}
    PriorAffinity& operator=(PriorAffinity&& other) noexcept {
        mask_ = other.mask_;
        owner_ = other.owner_;
        is_held_ = std::exchange(other.is_held_, false);
        return *this;
    }
    ~PriorAffinity() = default;

    // False for a call that asked for no pin, and after a move or a restore.
    [[nodiscard]] bool is_held() const noexcept { return is_held_; }

    [[nodiscard]] std::expected<void, int> restore() && noexcept {
        if (int const error = ::foundation::os::error_number_of(restore_mask_()); error != 0) {
            return std::unexpected(error);
        }
        return {};
    }

private:
    // The body of restore().  It is in src/fixy/os/Sched.cpp, so a caller
    // pays one call and holds no copy of the mask or of the door.
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
    restore_mask_() noexcept;

    // Holds no mask: the call asked for no pin.
    PriorAffinity() noexcept = default;
    explicit PriorAffinity(AffinityMask const& mask) noexcept
        : mask_{mask},
          owner_{::foundation::effects::detail::producer_claim::calling_thread_identity()},
          is_held_{true} {}

    template <eff::IsExecCtx FriendCtx>
        requires CtxFitsRuntimeAffinity<FriendCtx>
    friend auto apply_affinity_to_mask(FriendCtx const&, AffinityMask mask) noexcept
        -> std::expected<PriorAffinity, int>;

    AffinityMask mask_{};
    // The identity of the thread that pinned, from foundation/effects/Ctx.h.
    // Zero names no thread.
    std::uintptr_t owner_ = 0;
    bool is_held_ = false;
};

// The mask arrives at runtime, so no compile-time pinning proof can be
// produced. This is not a mint for that reason. Its gate is the one the
// three mints above read, CtxFitsRuntimeAffinity in fixy/os/CpuPinned.h.
//
// An empty mask asks for no pin, and the call changes nothing.  A pin
// goes through the same helper as mint_affinity, so it records a new pin
// event, and a proof of an earlier pin on this thread stops being in
// force.  The result holds the mask of the thread before the pin.
//
// The two door calls stand in the body and not in a lambda, because the walk
// of the rule syscall-grant-coverage does not read the body of a lambda, and
// the grant of crucible/warden/Hardening.h names the two calls of this door.
template <eff::IsExecCtx Ctx>
    requires CtxFitsRuntimeAffinity<Ctx>
[[nodiscard]] auto apply_affinity_to_mask(Ctx const&, AffinityMask mask) noexcept -> std::expected<PriorAffinity, int> {
    if (mask == AffinityMask{}) return PriorAffinity{};
    ::foundation::core::Result<AffinityMask, ::foundation::os::Errno> prior = sf::detail::sched_getaffinity_sys();
    if (int const error = ::foundation::os::error_number_of(prior); error != 0) {
        return std::unexpected(error);
    }
    if (int const error = ::foundation::os::error_number_of(sf::detail::sched_setaffinity_sys(mask)); error != 0) {
        return std::unexpected(error);
    }
    return PriorAffinity{std::move(prior).expect("the read of the prior mask succeeded above")};
}

// A pin to one CPU, through apply_affinity_to_mask.  A negative index asks
// for no pin, and an index past the kernel set is EINVAL.
template <eff::IsExecCtx Ctx>
    requires CtxFitsRuntimeAffinity<Ctx>
[[nodiscard]] auto apply_affinity_to_cpu(Ctx const& ctx, int cpu) noexcept -> std::expected<PriorAffinity, int> {
    if (cpu < 0) return apply_affinity_to_mask(ctx, AffinityMask{});
    if (static_cast<unsigned>(cpu) > AffinityMask::kMaxCore) [[unlikely]] {
        return std::unexpected(::foundation::os::error_number(::foundation::os::Errno::einval));
    }
    return apply_affinity_to_mask(ctx, AffinityMask::single(static_cast<std::uint16_t>(cpu)));
}

}  // namespace fixy::sched
