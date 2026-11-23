#pragma once

// Setting a real-time policy, a deadline budget, or a lower nice value needs
// CAP_SYS_NICE and fails for an unprivileged thread. A proof that claims the
// thread is pinned, real-time or niced must not exist when the syscall failed,
// so every mint here returns the error of the kernel in a Result and lets the
// caller decide.
//
// Scheduling frames
//   A scheduling proof cannot copy and cannot move, because the policy or
//   the nice value belongs to the thread that set it.  So
//   mint_scheduler_policy and mint_priority build the proof in their own
//   frame after the system call, and they lend it to a body as a Ref with
//   the brand of the frame.  The proof exists only inside the frame.  The
//   mint gives the result of the body, or Unit for a body that gives
//   nothing.  That result leaves the frame, so it holds no borrow and names
//   no brand of the frame (FrameResult of foundation/core/Choice.h).  The
//   kernel keeps the policy or the nice value after the frame ends: the end
//   of the proof changes no setting of the thread.

#include <fixy/Ctx.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/SchedClass.h>
#include <fixy/os/ThreadName.h>
#include <foundation/Brand.h>
#include <foundation/Lifetime.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Id.h>
#include <foundation/core/Ref.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>
#include <foundation/os/SchedWords.h>

#include <cstdint>
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

// The index of a CPU, as the kernel numbers the CPUs from zero.  An index is
// no integer, and its number comes out only through raw().
struct CpuIndexTag final {};
using CpuIndex = ::foundation::core::Id<CpuIndexTag, std::uint32_t>;

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

// Scheduling door.  Each function issues its one system call for the
// calling thread through foundation::os::raw_syscall, and gives the error of
// the kernel.  The bodies are in src/fixy/os/Sched.cpp, which includes no
// header of the kernel and no header of the C library.  A function that
// issues one system call name out of line has the name name_sys (the
// affinity door of fixy/os/CpuPinned.h).

// Sets the policy of the calling thread, with the real-time priority that
// SCHED_FIFO and SCHED_RR read and that each other policy needs as zero.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sched_setscheduler_sys(int policy, int rt_priority) noexcept;

// Sets the scheduling attributes of the calling thread.  The door writes the
// field size itself, so the kernel reads the first published struct
// sched_attr.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sched_setattr_sys(::foundation::os::KernelSchedAttr const& attributes) noexcept;

// The scheduling attributes of the calling thread, in the layout of the
// first published struct sched_attr.
[[nodiscard]] ::foundation::core::Result<::foundation::os::KernelSchedAttr, ::foundation::os::Errno>
sched_getattr_sys() noexcept;

// The attributes of the policy SCHED_DEADLINE, with its budget in
// nanoseconds.
[[nodiscard]] constexpr ::foundation::os::KernelSchedAttr
deadline_attributes(std::uint64_t runtime_ns, std::uint64_t deadline_ns, std::uint64_t period_ns) noexcept {
    ::foundation::os::KernelSchedAttr attributes{};
    attributes.policy = static_cast<std::uint32_t>(::foundation::os::kSchedDeadline);
    attributes.runtime_ns = runtime_ns;
    attributes.deadline_ns = deadline_ns;
    attributes.period_ns = period_ns;
    return attributes;
}

// Sets the nice value of the calling thread.  On Linux, the target of the
// call is the thread, and not each thread of the process.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
setpriority_sys(int nice) noexcept;

// The CPU on which the calling thread runs, through sched_getcpu of the C
// library, or none when the kernel gives no CPU.  The C library reads the CPU
// from the area of restartable sequences that it registers for each thread,
// with no system call, so a read costs some nanoseconds.  The body is in
// src/fixy/os/ThreadTasks.cpp, the one owner of <pthread.h>, which gives the
// symbols of <sched.h>.
[[nodiscard]] ::foundation::core::Option<CpuIndex> sched_getcpu_lib() noexcept;

template <SchedulerPolicy_v Policy>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
apply_scheduler_policy(int rt_priority, std::uint64_t runtime_ns, std::uint64_t deadline_ns,
                       std::uint64_t period_ns) noexcept {
    if constexpr (Policy == SchedulerPolicy_v::Deadline) {
        return sched_setattr_sys(deadline_attributes(runtime_ns, deadline_ns, period_ns));
    } else {
        return sched_setscheduler_sys(sched_policy_constant(Policy), rt_priority);
    }
}

// The borrow of a proof that the body of a scheduling frame takes (the
// scheduling frames).  The brand is the brand of the frame.
template <class Proof, class Brand>
using ProofRef = ::foundation::core::Ref<Proof const, Brand>;

// The result of a body for the borrow of a proof.
template <class Body, class Proof, class Brand>
using frame_result_t = std::invoke_result_t<Body&&, ProofRef<Proof, Brand>>;

// The value that a frame gives: the result of the body, or Unit for a body
// that gives void.
template <class Given>
using frame_value_t = std::conditional_t<std::is_void_v<Given>, ::foundation::core::Unit, std::remove_cv_t<Given>>;

// A body of a frame: it takes the borrow of the proof, it does not throw,
// and it holds no linear value by value (HoldsNoLinearValue of
// foundation/Linear.h).
template <class Body, class Proof, class Brand>
concept SchedFrameBody =
    std::is_nothrow_invocable_v<Body&&, ProofRef<Proof, Brand>> && ::foundation::linear::HoldsNoLinearValue<Body>;

// A body of a frame that holds a linear value by value: the linear twin of
// each frame mint binds to it.
template <class Body, class Proof, class Brand>
concept SchedLinearFrameBody =
    (!::foundation::linear::HoldsNoLinearValue<Body>) && std::is_invocable_v<Body&&, ProofRef<Proof, Brand>>;

// A body whose result leaves the frame: void, or a value that holds no
// borrow, names no brand of the frame, and a Result holds.
template <class Body, class Proof, class Brand>
concept SchedFrameLeaves =
    SchedFrameBody<Body, Proof, Brand> && ::foundation::core::FrameResult<frame_result_t<Body, Proof, Brand>, Brand>
    && ::foundation::core::ChoicePayload<frame_value_t<frame_result_t<Body, Proof, Brand>>>;

// Gives the borrow of the proof to body, and gives the value of the frame.
// The frame door of SchedProofDoor makes the borrow with the brand of the
// frame.
template <class Brand, class Proof, class Body>
[[nodiscard]] frame_value_t<frame_result_t<Body, Proof, Brand>> run_frame_(ProofRef<Proof, Brand> const lent,
                                                                           Body&& body) noexcept {
    if constexpr (std::is_void_v<frame_result_t<Body, Proof, Brand>>) {
        static_cast<Body&&>(body)(lent);
        return ::foundation::core::Unit{};
    } else {
        return static_cast<Body&&>(body)(lent);
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
// failure on a class template is not in the immediate context of the
// function template.  GCC 16 reports it as a hard error rather than
// discarding the candidate, so the call site cannot recover and no
// overload set can absorb it.  Measured, not reasoned: the deletion was
// written, compiled, and reverted on the diagnostic.
//
// The first conjunct is the shared scheduling-authority gate, as above.
template <typename Ctx, int Nice>
concept CtxFitsPriorityMint = CtxFitsRuntimeAffinity<Ctx> && (Nice >= -20 && Nice <= 19);

// The real-time priority that a policy takes.  SCHED_FIFO and SCHED_RR take
// a priority from 1 to 99, the bounds that sched_get_priority_min and
// sched_get_priority_max give on Linux.  Each other policy takes zero.  So a
// priority that the policy does not take is a compile error, and not the
// EINVAL of the kernel.
template <SchedulerPolicy_v Policy, std::uint8_t RtPriority>
concept RealTimePriorityFits = ((Policy == SchedulerPolicy_v::Fifo || Policy == SchedulerPolicy_v::RoundRobin)
                                    ? (RtPriority >= 1 && RtPriority <= 99)
                                    : RtPriority == 0);

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
[[nodiscard]] ::foundation::core::Result<sf::CpuPinned<Mask, Posture, sf::PinProofUnit>, ::foundation::os::Errno>
mint_affinity(Ctx const&) noexcept {
    ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno> pin_event =
        sf::detail::sched_setaffinity_sys(Mask);
    for (::foundation::os::Errno const error : pin_event.err()) {
        return ::foundation::core::err(error);
    }
    return sf::CpuPinned<Mask, Posture, sf::PinProofUnit>{0, std::move(pin_event).expect("the pin succeeded above")};
}

// The proof of a policy.  The proof holds the real-time priority as its
// value, which peek() gives.
template <SchedulerPolicy_v Policy, std::uint64_t RuntimeNs, std::uint64_t DeadlineNs, std::uint64_t PeriodNs>
using PolicyProof = sf::SchedClass<Policy, ProofUnit, RuntimeNs, DeadlineNs, PeriodNs>;

// The value of a frame whose body takes the borrow of Proof.
template <class Body, class Proof, class Brand>
using frame_value_of_t = detail::frame_value_t<detail::frame_result_t<Body, Proof, Brand>>;

// Deadline admission requires runtime < deadline <= period. The SchedClass
// proof asserts that ordering, so a budget that violates it is a compile
// error inside the mint and needs no separate concept.
//
// The two frame mints below set the policy or the nice value of the calling
// thread, and lend the proof to body (the scheduling frames).  The proof
// exists only inside the frame.  The policy and the nice value stay in force
// after the frame ends.  The pack Refused takes each explicit template
// argument after the named ones and refuses it, so no caller names the brand
// of the frame.
//
// They are declared here and defined after SchedProofDoor, which names each
// one as a friend.  The default arguments belong to this declaration.
template <SchedulerPolicy_v Policy, std::uint8_t RtPriority = 0, std::uint64_t RuntimeNs = 0,
          std::uint64_t DeadlineNs = 0, std::uint64_t PeriodNs = 0, class... Refused, eff::IsExecCtx Ctx, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires CtxFitsSchedPolicyMint<Ctx, Policy> && (sizeof...(Refused) == 0)
          && RealTimePriorityFits<Policy, RtPriority>
          && detail::SchedFrameLeaves<Body, PolicyProof<Policy, RuntimeNs, DeadlineNs, PeriodNs>, Fresh>
[[nodiscard]] ::foundation::core::Result<
    frame_value_of_t<Body, PolicyProof<Policy, RuntimeNs, DeadlineNs, PeriodNs>, Fresh>, ::foundation::os::Errno>
mint_scheduler_policy(Ctx const&, Body&& body) noexcept;

template <int Nice, class... Refused, eff::IsExecCtx Ctx, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires CtxFitsPriorityMint<Ctx, Nice> && (sizeof...(Refused) == 0)
          && detail::SchedFrameLeaves<Body, SchedPriority<Nice>, Fresh>
[[nodiscard]] ::foundation::core::Result<frame_value_of_t<Body, SchedPriority<Nice>, Fresh>, ::foundation::os::Errno>
mint_priority(Ctx const&, Body&& body) noexcept;

// A body whose result holds a borrow, an address or the brand of the frame
// binds to these two overloads, so the refusal gives its reason.  The proof
// ends with the frame, so such a result names an object that no longer
// exists.
template <SchedulerPolicy_v Policy, std::uint8_t RtPriority = 0, std::uint64_t RuntimeNs = 0,
          std::uint64_t DeadlineNs = 0, std::uint64_t PeriodNs = 0, class... Refused, eff::IsExecCtx Ctx, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires CtxFitsSchedPolicyMint<Ctx, Policy> && (sizeof...(Refused) == 0)
              && RealTimePriorityFits<Policy, RtPriority>
              && detail::SchedFrameBody<Body, PolicyProof<Policy, RuntimeNs, DeadlineNs, PeriodNs>, Fresh>
              && (!detail::SchedFrameLeaves<Body, PolicyProof<Policy, RuntimeNs, DeadlineNs, PeriodNs>, Fresh>)
void mint_scheduler_policy(Ctx const&, Body&& body) = delete(
    "the result of a scheduling frame leaves the frame, so it holds no reference and no address, and it names no "
    "brand of the frame: the proof ends with the frame, so give a plain value");

template <int Nice, class... Refused, eff::IsExecCtx Ctx, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires CtxFitsPriorityMint<Ctx, Nice> && (sizeof...(Refused) == 0)
              && detail::SchedFrameBody<Body, SchedPriority<Nice>, Fresh>
              && (!detail::SchedFrameLeaves<Body, SchedPriority<Nice>, Fresh>)
void mint_priority(Ctx const&, Body&& body) = delete(
    "the result of a scheduling frame leaves the frame, so it holds no reference and no address, and it names no "
    "brand of the frame: the proof ends with the frame, so give a plain value");

// A body that holds a linear value by value binds to these overloads, so the
// refusal gives its reason.
template <SchedulerPolicy_v Policy, std::uint8_t RtPriority = 0, std::uint64_t RuntimeNs = 0,
          std::uint64_t DeadlineNs = 0, std::uint64_t PeriodNs = 0, class... Refused, eff::IsExecCtx Ctx, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires CtxFitsSchedPolicyMint<Ctx, Policy> && (sizeof...(Refused) == 0)
              && detail::SchedLinearFrameBody<Body, PolicyProof<Policy, RuntimeNs, DeadlineNs, PeriodNs>, Fresh>
void mint_scheduler_policy(Ctx const&, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

template <int Nice, class... Refused, eff::IsExecCtx Ctx, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires CtxFitsPriorityMint<Ctx, Nice> && (sizeof...(Refused) == 0)
              && detail::SchedLinearFrameBody<Body, SchedPriority<Nice>, Fresh>
void mint_priority(Ctx const&, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

}  // namespace fixy::sched

namespace foundation::core {

// The door of the frames of a scheduling proof to the door of a Ref (the doors
// of the owners above foundation/core/Ref.h).  Its one member is private, and
// SchedProofDoor of fixy/os/Sched.h is its one friend.  The invariant: the
// frame door builds a proof in its own frame after the syscall returned, and
// lends it to the body with the scope brand of the call of the frame mint.
// So the Ref names a live proof for the call of the body, and the brand does
// not leave the frame.  A home owner row of utils/rule-table.txt keeps this
// definition in this header.
class SchedProofRefDoor final : ::foundation::NoObject<SchedProofRefDoor> {
    friend class ::fixy::sched::SchedProofDoor;

    // The proof of a frame, as a Ref of the brand of the frame.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return detail::RefDoor::lend<Brand>(object);
    }
};

}  // namespace foundation::core

namespace fixy::sched {

// The door to the two scheduling proofs.  No object of it exists.  It is
// the authority of the key that the constructor of SchedClass and of
// SchedPriority takes, so only its members build that key.  Its members
// are private, and its two friends are the frame mints above, which call a
// member only after their syscall returned.  Each member builds its proof
// in its own frame and lends it to the body, because a proof neither copies
// nor moves.
//
// The trailing return types are necessary: fixy/os/CpuPinned.h gives the
// parse reason.
class SchedProofDoor final : ::foundation::NoObject<SchedProofDoor> {
    template <SchedulerPolicy_v FriendPolicy, std::uint8_t FriendRtPriority, std::uint64_t FriendRuntimeNs,
              std::uint64_t FriendDeadlineNs, std::uint64_t FriendPeriodNs, class... FriendRefused,
              eff::IsExecCtx FriendCtx, class FriendBody, ::foundation::brand::IsBrand FriendFresh>
        requires CtxFitsSchedPolicyMint<FriendCtx, FriendPolicy> && (sizeof...(FriendRefused) == 0)
              && RealTimePriorityFits<FriendPolicy, FriendRtPriority>
              && detail::SchedFrameLeaves<FriendBody,
                                          PolicyProof<FriendPolicy, FriendRuntimeNs, FriendDeadlineNs, FriendPeriodNs>,
                                          FriendFresh>
    friend auto mint_scheduler_policy(FriendCtx const&, FriendBody&&) noexcept -> ::foundation::core::Result<
        frame_value_of_t<FriendBody, PolicyProof<FriendPolicy, FriendRuntimeNs, FriendDeadlineNs, FriendPeriodNs>,
                         FriendFresh>,
        ::foundation::os::Errno>;

    template <int FriendNice, class... FriendRefused, eff::IsExecCtx FriendCtx, class FriendBody,
              ::foundation::brand::IsBrand FriendFresh>
        requires CtxFitsPriorityMint<FriendCtx, FriendNice> && (sizeof...(FriendRefused) == 0)
              && detail::SchedFrameLeaves<FriendBody, SchedPriority<FriendNice>, FriendFresh>
    friend auto mint_priority(FriendCtx const&, FriendBody&&) noexcept
        -> ::foundation::core::Result<frame_value_of_t<FriendBody, SchedPriority<FriendNice>, FriendFresh>,
                                      ::foundation::os::Errno>;

    using key_ = ::foundation::algebra::grade_key<SchedProofDoor>;

    template <class Proof, class Brand, class Body, class... Arguments>
    [[nodiscard]] static frame_value_of_t<Body, Proof, Brand> frame_(Body&& body, Arguments... arguments) noexcept {
        const key_ key{};
        Proof const proof{key, arguments...};
        return detail::run_frame_<Brand>(::foundation::core::SchedProofRefDoor::lend_<Brand>(proof),
                                         static_cast<Body&&>(body));
    }
};

template <SchedulerPolicy_v Policy, std::uint8_t RtPriority, std::uint64_t RuntimeNs, std::uint64_t DeadlineNs,
          std::uint64_t PeriodNs, class... Refused, eff::IsExecCtx Ctx, class Body, ::foundation::brand::IsBrand Fresh>
    requires CtxFitsSchedPolicyMint<Ctx, Policy> && (sizeof...(Refused) == 0)
          && RealTimePriorityFits<Policy, RtPriority>
          && detail::SchedFrameLeaves<Body, PolicyProof<Policy, RuntimeNs, DeadlineNs, PeriodNs>, Fresh>
[[nodiscard]] ::foundation::core::Result<
    frame_value_of_t<Body, PolicyProof<Policy, RuntimeNs, DeadlineNs, PeriodNs>, Fresh>, ::foundation::os::Errno>
mint_scheduler_policy(Ctx const&, Body&& body) noexcept {
    CRUCIBLE_TRY(_, detail::apply_scheduler_policy<Policy>(RtPriority, RuntimeNs, DeadlineNs, PeriodNs));
    return SchedProofDoor::frame_<PolicyProof<Policy, RuntimeNs, DeadlineNs, PeriodNs>, Fresh>(
        static_cast<Body&&>(body), ProofUnit{RtPriority});
}

// The nice value is a value of the calling thread, not of the whole process
// (setpriority_sys).
template <int Nice, class... Refused, eff::IsExecCtx Ctx, class Body, ::foundation::brand::IsBrand Fresh>
    requires CtxFitsPriorityMint<Ctx, Nice> && (sizeof...(Refused) == 0)
          && detail::SchedFrameLeaves<Body, SchedPriority<Nice>, Fresh>
[[nodiscard]] ::foundation::core::Result<frame_value_of_t<Body, SchedPriority<Nice>, Fresh>, ::foundation::os::Errno>
mint_priority(Ctx const&, Body&& body) noexcept {
    CRUCIBLE_TRY(_, detail::setpriority_sys(Nice));
    return SchedProofDoor::frame_<SchedPriority<Nice>, Fresh>(static_cast<Body&&>(body));
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

// The gate of the read of the current CPU: a context that owns Nondet.  The
// scheduler can move the thread to another CPU at each instruction, so two
// reads can give two CPUs with no change of the program.
template <typename Ctx>
concept CtxFitsCurrentCpuRead = eff::IsExecCtx<Ctx> && eff::CtxOwnsCapability<Ctx, eff::Effect::Nondet>;

// The CPU on which the calling thread runs at the read, or none when the
// kernel gives no CPU.  The thread can run on another CPU right after the
// read, unless a pin holds it to that one CPU.  The read changes nothing, and
// it records no pin event.  It is not a mint, because it gives a fact of the
// kernel and no authority.  Complexity: one read of the area of restartable
// sequences of the thread (detail::sched_getcpu_lib).
template <eff::IsExecCtx Ctx>
    requires CtxFitsCurrentCpuRead<Ctx>
[[nodiscard]] ::foundation::core::Option<CpuIndex> read_current_cpu(Ctx const&) noexcept {
    return detail::sched_getcpu_lib();
}

// The scheduling attributes of the calling thread: the policy, the flags, the
// nice value, the real-time priority and the deadline budget.  The read
// changes nothing.  An older kernel than 3.14 gives enosys.  The gate is the
// gate of the other scheduling doors.
template <eff::IsExecCtx Ctx>
    requires CtxFitsRuntimeAffinity<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::os::KernelSchedAttr, ::foundation::os::Errno>
read_sched_attributes(Ctx const&) noexcept {
    return detail::sched_getattr_sys();
}

// Sets the scheduling attributes of the calling thread.  A real-time policy,
// the deadline policy and a lower nice value need CAP_SYS_NICE, and an
// unprivileged thread gets eperm.  Attributes that read_sched_attributes gave
// put the thread back in the state of that read.  This is not a mint: no
// proof claims the attributes after the call.
template <eff::IsExecCtx Ctx>
    requires CtxFitsRuntimeAffinity<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
apply_sched_attributes(Ctx const&, ::foundation::os::KernelSchedAttr const& attributes) noexcept {
    return detail::sched_setattr_sys(attributes);
}

class PriorAffinityDoor;

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

    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> restore() && noexcept {
        return restore_mask_();
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

    // The door below is the one friend.  A friend function with a result of
    // this class would name the class before it is complete, and the Result
    // of the base needs a complete payload.
    friend class PriorAffinityDoor;

    AffinityMask mask_{};
    // The identity of the thread that pinned, from foundation/effects/Ctx.h.
    // Zero names no thread.
    std::uintptr_t owner_ = 0;
    bool is_held_ = false;
};

template <eff::IsExecCtx Ctx>
    requires CtxFitsRuntimeAffinity<Ctx>
[[nodiscard]] auto apply_affinity_to_mask(Ctx const&, AffinityMask mask) noexcept
    -> ::foundation::core::Result<PriorAffinity, ::foundation::os::Errno>;

// The door of a prior mask.  No object of it exists.  Its members are
// private, and its one friend is apply_affinity_to_mask, so only that door
// builds a PriorAffinity.
class PriorAffinityDoor final : ::foundation::NoObject<PriorAffinityDoor> {
    template <eff::IsExecCtx FriendCtx>
        requires CtxFitsRuntimeAffinity<FriendCtx>
    friend auto apply_affinity_to_mask(FriendCtx const&, AffinityMask mask) noexcept
        -> ::foundation::core::Result<PriorAffinity, ::foundation::os::Errno>;

    // The result of a call that asked for no pin.
    [[nodiscard]] static PriorAffinity none_() noexcept { return PriorAffinity{}; }

    // The mask that a pin of the calling thread replaced.
    [[nodiscard]] static PriorAffinity held_(AffinityMask const& mask) noexcept { return PriorAffinity{mask}; }
};

// The mask arrives at runtime, so no compile-time pinning proof can be
// produced. This is not a mint for that reason. Its gate is the one the
// three mints above read, CtxFitsRuntimeAffinity in fixy/os/CpuPinned.h.
//
// An empty mask asks for no pin, and the call changes nothing.  A pin
// goes through the same helper as mint_affinity, so it records a new pin
// event, and a proof of an earlier pin on this thread stops being in
// force.  The result holds the mask of the thread before the pin, or the
// error of the kernel.
//
// The two door calls stand in the body and not in a lambda, because the walk
// of the rule syscalls does not read the body of a lambda, and
// the grant of crucible/warden/Hardening.h names the two calls of this door.
template <eff::IsExecCtx Ctx>
    requires CtxFitsRuntimeAffinity<Ctx>
[[nodiscard]] auto apply_affinity_to_mask(Ctx const&, AffinityMask mask) noexcept
    -> ::foundation::core::Result<PriorAffinity, ::foundation::os::Errno> {
    if (mask == AffinityMask{}) return PriorAffinityDoor::none_();
    ::foundation::core::Result<AffinityMask, ::foundation::os::Errno> prior = sf::detail::sched_getaffinity_sys();
    for (::foundation::os::Errno const error : prior.err()) {
        return ::foundation::core::err(error);
    }
    ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno> const pin_event =
        sf::detail::sched_setaffinity_sys(mask);
    for (::foundation::os::Errno const error : pin_event.err()) {
        return ::foundation::core::err(error);
    }
    return PriorAffinityDoor::held_(std::move(prior).expect("the read of the prior mask succeeded above"));
}

// A pin to one CPU, through apply_affinity_to_mask.  A negative index asks
// for no pin, and an index past the kernel set is einval.
template <eff::IsExecCtx Ctx>
    requires CtxFitsRuntimeAffinity<Ctx>
[[nodiscard]] auto apply_affinity_to_cpu(Ctx const& ctx, int cpu) noexcept
    -> ::foundation::core::Result<PriorAffinity, ::foundation::os::Errno> {
    if (cpu < 0) return apply_affinity_to_mask(ctx, AffinityMask{});
    if (static_cast<unsigned>(cpu) > AffinityMask::kMaxCore) [[unlikely]] {
        return ::foundation::core::err(::foundation::os::Errno::einval);
    }
    return apply_affinity_to_mask(ctx, AffinityMask::single(static_cast<std::uint16_t>(cpu)));
}

}  // namespace fixy::sched
