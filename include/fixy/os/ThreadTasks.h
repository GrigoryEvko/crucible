#pragma once

// The door that starts the threads of the base and joins them.
//
// A task is the address of a frame and the function that runs the task from
// that frame.  run_thread_tasks starts one thread for each task, runs the task
// on its thread, and joins each thread before it returns.  The door calls
// pthread_create and pthread_join directly, in src/fixy/os/ThreadTasks.cpp,
// which is the one owner of <pthread.h>.  It uses no std::thread, no mutex, no
// condition variable, no cancellation, no detach and no thread key.  The list
// of the caller holds the handle of each thread, so the door allocates
// nothing.
//
// THE OPTIONS
//     Each thread of one call gets the options of the call: the size of its
//     stack, the size of the guard area below the stack, its name and its
//     CPUs.  The defaults do not read the environment.  The stack has 8 MiB,
//     which is the stack of a thread under the default RLIMIT_STACK of Linux,
//     and the guard has one page of 4 KiB.  glibc rounds the guard up to the
//     page size of the host.  A guard of zero bytes is refused, because a
//     stack with no guard lets an overflow write over other memory with no
//     signal.  An empty name keeps the name of the thread that starts the new
//     thread, and an empty mask keeps its CPUs.  A new thread takes its name
//     before its task runs, and it takes its CPUs when it starts.  The door
//     records no pin event for those CPUs, so no CpuPinned proof of
//     fixy/os/CpuPinned.h is in force on the thread until it pins through
//     fixy/os/Sched.h.
//
//     The placement tells how the threads share the mask.  SharedMask gives
//     each thread each CPU of the mask, and the scheduler places them.
//     OneCpuEach gives the thread of the task k only the CPU k of the mask,
//     in ascending order of the CPUs, modulo the count of the CPUs.  So no
//     two of the first tasks, up to that count, share a CPU.  On isolated
//     CPUs, such as the cores of a bench, no load balancer moves a thread, and
//     a thread with a mask of several CPUs stays on the CPU of the thread that
//     starts it.  There, OneCpuEach is the only way to run the threads of one
//     call on more than one CPU.  The placement of an empty mask changes
//     nothing.
//
// THE ERRORS
//     A start that fails before the first thread runs is the error of the
//     Result, with the error number of the call that failed: eagain when the
//     process or the host has no resources for one more thread, eperm when
//     the caller has no permission for an option, and einval for an option
//     that is not valid.  No thread of the call runs then.  When a body or a
//     continuation of the call holds a linear value, the error is a
//     RefusedStart: the error number and each part of the call, which no door
//     ran, so no door drops a linear value in silence.  The caller starts a
//     part again, or ends it through end_unrun_body of foundation/Linear.h
//     with a reason.  The error of each other call is the error number
//     (start_error_t).  A start that fails after other threads of the call
//     started ends the process.  A started
//     task can wait for a task that did not start, so a join of the started
//     threads could wait forever, and the door has no correct state to give
//     back.  A join that fails ends the process too, because the door started
//     and joins each thread one time.
//
// THE WAIT OF THE CALLER
//     The door has no spin.  A start calls pthread_create, and a join calls
//     pthread_join, which parks the caller until the thread ends.  run_joined
//     holds the caller from the first start to the last join.  So the caller
//     cannot open a gate of its bodies between the starts and the joins, and
//     bodies that wait for each other need a wait of their own.  run_beside
//     and run_beside_each run a continuation on the caller between the starts
//     and the joins.
//
//     A spin of a body is correct only when each body has a CPU of its own,
//     such as through OneCpuEach over one CPU for each body.  On a CPU that
//     two bodies share, a spin holds the CPU until the scheduler ends its time
//     slice, some milliseconds, while the body that it waits for cannot run.
//     Bodies that can share a CPU wait with SpinThenPark of
//     fixy/concurrent/EventCount.h.
//
// THE ALTERNATE STACK
//     Each thread that the door starts holds a ThreadSignalStack of
//     fixy/os/Signal.h while its task runs.  The handler of a fatal signal
//     then runs on that stack, also when the task overflows its own stack.
//     The thread puts back its earlier alternate stack and unmaps the stack
//     of the door when its task returns.  A thread that the base does not
//     start gets no alternate stack from the door.  The stack costs each
//     thread six system calls: mmap, mprotect, three calls of sigaltstack and
//     munmap.
//
// THE OWNED THREAD
//     start_thread_task starts one thread and gives its handle, and
//     join_thread_task joins it later.  The two are the door of the
//     OwnedThread of fixy/os/OwnedThread.h, which holds the task, the name
//     and the body in a frame of its own.  The errors of a start and of a
//     join are the errors above.
//
// THE FORK HOOK
//     run_in_fork_child registers a function that the child of a fork runs
//     before fork returns in the child, through pthread_atfork.  A child has
//     only the thread that called fork, so a hook can drop the state of the
//     other threads of the parent.
//
// THE CONTEXT OF A BODY
//     A body of run_joined takes no argument, or the context of the call as
//     its one parameter.  A body that needs a context takes it there and
//     captures none, so no context of one thread reaches the body through a
//     capture (foundation/Closure.h).  The context owns Bg or Init, which
//     names no thread, so each thread of the call can use it.  The door
//     gives one context to each thread at the same time, so a body takes the
//     context only when the context is Shareable (foundation/Transfer.h).  A
//     context that holds the branded source of one thread is not Shareable,
//     and the door refuses a body that takes it.  The caller waits until each
//     thread has joined, so the context outlives each body.
//
// THE KEYS OF A BODY
//     A body of run_joined or of run_beside can name keys of named brands as
//     its trailing parameters, LoanKey<B>&, after no argument or after the
//     context: body(keys...) or body(ctx, keys...).  The door is a root door
//     (THE BORROW OF A ROOT DOOR of foundation/core/Frame.h): on the thread of
//     the body, it mints one key of each brand for the call, and it refuses a
//     body that reaches a key of such a brand, or an owner of such a brand
//     through a borrow (foundation/core/RootKeys.h).  The bodies of
//     run_beside_each take the index of the thread only, so a body that names a
//     key does not compile there.
//
// THE BODIES BY MOVE
//     run_joined, run_joined_raw_probe and run_beside take each body as an
//     rvalue, and they move each body into a parameter by value of the frame of
//     the door before the first start.  So two bodies are two objects, also
//     when the caller moves one object two times, and no two threads call one
//     object.  A body that the caller names as an lvalue meets a deleted
//     overload with the reason.  Without it, one keyed body given two times
//     would mint a key on each thread and open two frames of one owner that it
//     holds by value, and one mutable body given two times would write its own
//     captures from two threads.  Each body then moves into an Option of the
//     frame.  The thread of a body takes it out of its Option and gives it to
//     the once door call_once of foundation/Linear.h, which owns it, calls it
//     one time and ends it.  A continuation goes through the once door too.
//     After the join, each Option is empty, and expect_none checks it: the
//     door drops no body.  The body owns each value that it captures by value,
//     and it consumes each linear one on each path (the kind
//     capture-unconsumed of the rule must-consume).  A body that the caller
//     reads after the call gives that value back as its result (THE RESULTS).
//
// THE RESULTS
//     A body of run_joined gives a value, or gives nothing.  Each thread
//     writes the value of its body into a slot of its own in the frame of
//     the call, and the join orders that write before the read of the
//     caller.  The call then gives a Record of the value of each body, in the
//     order of the bodies, with Unit for a body that gives nothing.  When no
//     body gives a value, the call gives Unit.  So a body gives its result
//     back and writes no state of its caller.  A value crosses from the
//     thread of its body to the caller, so it is Sendable, and it holds no
//     borrow.  The slot takes the value by an assignment of an Option, so a
//     linear value is refused.
//
// THE CLOSURE OF A BODY
//     run_joined checks each body with check_closure of foundation/Closure.h,
//     and a refusal is a static_assert whose message is the verdict.  One
//     body crosses to one thread while the caller waits, so its policy is
//     joined_sole_policy (SoleThread).  Two or more bodies run at the same
//     time, so each has joined_concurrent_policy (ConcurrentThreads).  The
//     check reads each body alone, so two bodies that reach one object
//     through two borrows are each refused where a borrow is not
//     Synchronized: the check fails closed.
//
// THE GATE
//     run_joined takes a context that owns Bg or Init, the authority to start
//     threads.  The list form and the owned form in detail have no gate of
//     their own.  Their callers in the base hold the gate: the fork of
//     fixy/concurrent/PermissionFork.h, the parallel loop of
//     fixy/concurrent/Spawn.h, the pipeline of fixy/concurrent/Pipeline.h and
//     the mint of fixy/os/OwnedThread.h.  run_in_fork_child changes the process
//     for each fork after it, so it takes a context that owns Init.
//
// THE RAW PROBE
//     run_joined_raw_probe is run_joined for a test that probes raw behavior,
//     such as the memory orders of an atomic.  Its bodies share plain state,
//     and the order of the probe publishes it.  No type can state that, so the
//     door does not ask the transfer facts of the state that its bodies share.
//     It still refuses a captured context and a borrow that outlives the call
//     (foundation/Closure.h).  The second context owns Test, which only the
//     testing door of foundation/effects/Effect.h gives, so code that ships
//     cannot call it.  The reason is a string literal, which names the probe
//     at the call.
//
// THE THREAD BESIDE THE CALLER
//     run_beside starts one thread that runs the body, runs the continuation
//     on the calling thread at the same time, and joins the thread when the
//     continuation returns.  So the body ends before the call returns, and it
//     can borrow the locals of the caller.  The continuation gets no owner of
//     the thread, so no thread leaves the call.  The body moves into the frame
//     of the door (THE BODIES BY MOVE), and the door takes the continuation by
//     value, so the continuation cannot reach the object of the body while
//     the thread calls it.  The body takes no argument
//     or the context of the call, as a body of run_joined does, and the
//     values of the body and of the continuation come back as THE RESULTS
//     tell.
//
//     The policy of the body is BesideCaller (foundation/Closure.h, rule 8),
//     which is stricter than the policy of run_joined.  In run_joined the
//     caller waits, so a body can read a Shareable object through a reference
//     to const.  Beside the caller, the continuation runs at the same time
//     and can write each object of the frame, also one that the body reads
//     through a reference to const, and the check cannot see that alias.  So
//     each borrow of the body is Synchronized, such as an Atomic, and each
//     value of the body is SendableBeside (foundation/Transfer.h): a value
//     that refers to an object of the frame, such as a View, refers only to a
//     Synchronized object.  The end of a channel moves into the body, and the
//     body ends it.  The door checks its body with check_closure under
//     beside_caller_policy, and the verdict is the message of a refusal.
//
//     run_beside_each is the counted form.  It starts count threads, and each
//     thread calls one shared body with its index, from 0 to count less one.
//     The continuation runs on the calling thread at the same time, and the
//     door joins each thread when the continuation returns.  The threads call
//     one object at the same time, so the policy beside_each_policy also asks
//     each value of the body to be Shareable, and a linear body is refused
//     because the door calls it more than one time.  A body that owns a
//     linear value, such as the one consumer of a channel, goes to run_beside,
//     and the continuation of that call can run the counted form.  The body
//     gives no value: a thread writes its result into a Synchronized object.
//     The bound of the count is a template argument, and the frame of the door
//     holds a task and a frame for each thread up to that bound, so the door
//     allocates nothing.  The options of the call are the options of each
//     thread.  With OneCpuEach, the thread of index k takes the CPU k of the
//     mask in ascending order of the CPUs (THE OPTIONS), and not the CPU of
//     some other order of the caller.  So a caller with threads that have one
//     role each gives each role a call of its own.  The errors are THE
//     ERRORS: a first start that fails is the error of the call, and then the
//     continuation does not run.

#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/Closure.h>
#include <foundation/Transfer.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Convert.h>
#include <foundation/core/Id.h>
#include <foundation/core/Index.h>
#include <foundation/core/Record.h>
#include <foundation/core/Region.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::thread {

// The stack of a thread under the default RLIMIT_STACK of Linux.
inline constexpr std::size_t default_stack_bytes = std::size_t{8} << 20;

// One page of 4 KiB.  glibc rounds a guard up to the page size of the host.
inline constexpr std::size_t default_guard_bytes = std::size_t{4} << 10;

namespace detail {

struct thread_task;

}  // namespace detail

// The name of a thread: a literal of 1 to 15 characters, the bound of the
// name of a task of the Linux kernel, which cuts a longer name with no
// error.  A name comes only from a string literal, and it holds its own
// copy of the characters, with zero bytes after them.  A default name is
// empty, and a thread with no name keeps the name of the thread that starts
// it.
//
// The class is structural, so a name is also a template argument:
// ThreadNamed<"crucible-bg"> of fixy/os/ThreadName.h names its thread in
// its type.  A structural class has public members, so the characters are
// const, and no code changes a name after its constructor.
class ThreadName {
public:
    // The bytes of a name: 15 characters and the zero after them.
    static constexpr std::size_t capacity = 16;

    char const text[capacity];

    constexpr ThreadName() noexcept : text{} {}

    template <std::size_t Length>
        requires(Length >= 2 && Length <= capacity)
    consteval ThreadName(char const (&literal)[Length]) noexcept
        : ThreadName{literal, std::make_index_sequence<capacity>{}} {
        // A zero byte before the end would cut the name, and a literal
        // always ends with one.  A read past a constant array, or a zero
        // byte at the wrong place, stops the constant evaluation.
        for (std::size_t index = 0; index + 1 < Length; ++index) {
            if (literal[index] == '\0') name_holds_a_zero_byte_();
        }
        if (literal[Length - 1] != '\0') name_holds_a_zero_byte_();
    }

    [[nodiscard]] constexpr bool is_named() const noexcept { return text[0] != '\0'; }

    // The characters with the zero after them.
    [[nodiscard]] constexpr char const* c_str() const noexcept { return text; }

    // The count of the characters, without the zero.
    [[nodiscard]] constexpr std::size_t visible_length() const noexcept {
        std::size_t length = 0;
        while (length < capacity && text[length] != '\0') {
            ++length;
        }
        return length;
    }

private:
    // Each byte of the name: a character of the literal, or a zero after
    // the literal.
    template <std::size_t Length, std::size_t... Index>
    consteval ThreadName(char const (&literal)[Length], std::index_sequence<Index...>) noexcept
        : text{(Index < Length ? literal[Index] : '\0')...} {}

    // Not constexpr, so a call of it stops the constant evaluation of the
    // constructor, and the diagnostic names it.
    static void name_holds_a_zero_byte_() noexcept;
};

// How the threads of one call share the CPUs of the mask of the options (THE
// OPTIONS).
enum class ThreadPlacement : std::uint8_t {
    // Each thread takes each CPU of the mask.
    SharedMask,
    // The thread of the task k takes the CPU k of the mask, modulo the count
    // of the CPUs of the mask.
    OneCpuEach,
};

// The options of each thread of one call (THE OPTIONS).
struct ThreadOptions {
    std::size_t stack_bytes = default_stack_bytes;
    std::size_t guard_bytes = default_guard_bytes;
    ThreadName name{};
    ::foundation::algebra::lattices::AffinityMask affinity{};
    ThreadPlacement placement = ThreadPlacement::SharedMask;
};

// A context that may start threads: one that owns Bg or Init.
template <typename Ctx>
concept CtxStartsThreads =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Init>;

namespace detail {

// One task.  The caller writes the frame and the function, and the door
// writes the name and the handle.  The frame and the options of the name
// live in the frame of the caller, which waits in the door until each
// thread has joined.
struct thread_task {
    void* frame = nullptr;
    void (*run)(void* frame) noexcept = nullptr;
    ThreadName const* name = nullptr;
    std::uint64_t handle = 0;
};

// Starts one thread for each task with the options, runs each task on its
// thread, and joins each thread before it returns (THE ERRORS).
// Complexity: one start and one join for each task.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_thread_tasks(::foundation::core::View<thread_task> tasks, ThreadOptions const& options) noexcept;

// Starts one thread for each task with the options, and keeps the handle of
// each thread in its task (THE ERRORS).  The tasks live until
// join_thread_tasks returns for them.  Complexity: one start for each task.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
start_thread_tasks(::foundation::core::View<thread_task> tasks, ThreadOptions const& options) noexcept;

// Joins the thread of each task that start_thread_tasks started, in the
// order of the tasks.  A join that fails ends the process.  Complexity: one
// join for each task.
void join_thread_tasks(::foundation::core::View<thread_task> tasks) noexcept;

// Starts one thread that runs the task with the options, and gives the
// handle of the thread (THE OWNED THREAD).  The caller writes the frame, the
// function and the name of the task, and the task lives until
// join_thread_task returns for the handle.  A refused option or a start that
// fails is the error, with the error number of the call, and then no thread
// runs.  Complexity: one start.
[[nodiscard]] ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno>
start_thread_task(thread_task& task, ThreadOptions const& options) noexcept;

// Joins the thread of a handle that start_thread_task gave.  A join that
// fails ends the process, as a join of run_thread_tasks does.
void join_thread_task(std::uint64_t handle) noexcept;

// Gives the calling thread a name.  The type holds the bound of the name,
// so ERANGE, the only failure of the call, cannot occur, and the door reads
// no error.
void name_calling_thread(ThreadName const& name) noexcept;

// True when a body of run_joined takes the context of the call, and the
// context is Shareable (THE CONTEXT OF A BODY).  The door calls each body as
// an rvalue (THE BODIES BY MOVE).
template <typename Ctx, typename Body>
inline constexpr bool takes_context_ =
    std::is_nothrow_invocable_v<Body, Ctx const&> && ::foundation::transfer::Shareable<Ctx>;

// The forms of a body of run_joined and run_beside: no argument, the context,
// the keys of a root door, or the context and the keys (THE KEYS OF A BODY).
enum class JoinedForm : std::uint8_t {
    none,
    bare,
    context,
    keys,
    context_and_keys,
};

// The form of a body with the context Ctx, or none when no form fits.  A body
// with keys takes them as its trailing parameters.
template <typename Ctx, typename Body>
consteval JoinedForm joined_form_() noexcept {
    if constexpr (::foundation::core::NamesRootKeys<Body, 0>) {
        return JoinedForm::keys;
    } else if constexpr (::foundation::core::NamesRootKeys<Body, 1> && ::foundation::transfer::Shareable<Ctx>) {
        if constexpr (requires {
                          typename ::foundation::core::detail::root_body_result_<
                              Body, ::foundation::core::root_key_brands_t<Body, 1>, Ctx const&>::type;
                      }) {
            return JoinedForm::context_and_keys;
        } else {
            return JoinedForm::none;
        }
    } else if constexpr (takes_context_<Ctx, Body>) {
        return JoinedForm::context;
    } else if constexpr (std::is_nothrow_invocable_v<Body>) {
        return JoinedForm::bare;
    } else {
        return JoinedForm::none;
    }
}

// The type that a call of a body gives, void included (THE RESULTS).  Only the
// trait of the form of the body is read.
template <typename Ctx, typename Body, JoinedForm Form = joined_form_<Ctx, Body>()>
struct joined_call_result_ {};
template <typename Ctx, typename Body>
struct joined_call_result_<Ctx, Body, JoinedForm::bare> : std::invoke_result<Body> {};
template <typename Ctx, typename Body>
struct joined_call_result_<Ctx, Body, JoinedForm::context> : std::invoke_result<Body, Ctx const&> {};
template <typename Ctx, typename Body>
struct joined_call_result_<Ctx, Body, JoinedForm::keys>
    : ::foundation::core::detail::root_body_result_<Body, ::foundation::core::root_key_brands_t<Body, 0>> {};
template <typename Ctx, typename Body>
struct joined_call_result_<Ctx, Body, JoinedForm::context_and_keys>
    : ::foundation::core::detail::root_body_result_<Body, ::foundation::core::root_key_brands_t<Body, 1>, Ctx const&> {
};

template <typename Ctx, typename Body>
using raw_joined_result_t_ = typename joined_call_result_<Ctx, Body>::type;

// The value of a body in the result of run_joined: what the body gives, or
// Unit for a body that gives nothing (THE RESULTS).
template <typename Ctx, typename Body>
using joined_result_t_ = std::conditional_t<std::is_void_v<raw_joined_result_t_<Ctx, Body>>, ::foundation::core::Unit,
                                            raw_joined_result_t_<Ctx, Body>>;

// True when the value of a body can cross to the thread of the caller and
// sit in the result of run_joined (THE RESULTS).
template <typename Value>
concept JoinedValue = ::foundation::transfer::Sendable<Value> && ::foundation::core::DroppablePayload<Value>
                   && ::foundation::core::HoldsNoBorrow<Value>;

// The frame of one body of run_joined: the Option that holds the body, the
// context of the call and the slot of the value of the body.  The Option and
// the slot live in the frame of the door, which waits for each thread (THE
// CONTEXT OF A BODY, THE BODIES BY MOVE, THE RESULTS).
struct joined_body_ {
    void* body = nullptr;
    void const* ctx = nullptr;
    void* value = nullptr;
};

// True when a part of a call, a body or a continuation, holds a linear value
// (THE ERRORS).
template <typename... Parts>
inline constexpr bool holds_linear_part_ = (::foundation::linear::is_linear_v<Parts> || ...);

}  // namespace detail

// The refusal of a start of a call with a part that holds a linear value (THE
// ERRORS): the error number of the call that failed, and each part that no
// thread ran, in the order of the call.  The caller binds the parts, and it
// starts each one again or ends it, such as through end_unrun_body of
// foundation/Linear.h.
template <typename... Parts>
struct RefusedStart {
    ::foundation::os::Errno error{};
    ::foundation::core::Record<Parts...> parts{};
};

// The error of a start of a call with these parts (THE ERRORS): a
// RefusedStart when a part holds a linear value, and the error number for
// each other call.
template <typename... Parts>
using start_error_t =
    std::conditional_t<detail::holds_linear_part_<Parts...>, RefusedStart<Parts...>, ::foundation::os::Errno>;

namespace detail {

// Calls the body one time with the context that it takes, and gives its value.
// The body moves from the frame of the door into the once door call_once of
// foundation/Linear.h on the thread of the body, which calls it one time and
// ends it, or into the entry of the root doors for a body with keys.
template <typename Ctx, typename Body>
[[nodiscard]] joined_result_t_<Ctx, Body> call_joined_body_(Body&& body, Ctx const& ctx) noexcept {
    constexpr JoinedForm form = joined_form_<Ctx, Body>();
    if constexpr (form == JoinedForm::keys) {
        // The keys are minted here, on the thread of the body (THE KEYS OF A BODY).
        if constexpr (std::is_void_v<raw_joined_result_t_<Ctx, Body>>) {
            ::foundation::core::detail::run_root_body_<0>(static_cast<Body&&>(body));
            return ::foundation::core::Unit{};
        } else {
            return ::foundation::core::detail::run_root_body_<0>(static_cast<Body&&>(body));
        }
    } else if constexpr (form == JoinedForm::context_and_keys) {
        if constexpr (std::is_void_v<raw_joined_result_t_<Ctx, Body>>) {
            ::foundation::core::detail::run_root_body_<1>(static_cast<Body&&>(body), ctx);
            return ::foundation::core::Unit{};
        } else {
            return ::foundation::core::detail::run_root_body_<1>(static_cast<Body&&>(body), ctx);
        }
    } else if constexpr (form == JoinedForm::context) {
        if constexpr (std::is_void_v<raw_joined_result_t_<Ctx, Body>>) {
            ::foundation::linear::call_once(static_cast<Body&&>(body), ctx);
            return ::foundation::core::Unit{};
        } else {
            return ::foundation::linear::call_once(static_cast<Body&&>(body), ctx);
        }
    } else if constexpr (std::is_void_v<raw_joined_result_t_<Ctx, Body>>) {
        ::foundation::linear::call_once(static_cast<Body&&>(body));
        return ::foundation::core::Unit{};
    } else {
        return ::foundation::linear::call_once(static_cast<Body&&>(body));
    }
}

// Takes the body out of its Option in the frame of the door, calls it one
// time, and writes its value into its slot.  The Option is empty after the
// take, so the door reads that the thread took the body (THE BODIES BY MOVE).
template <typename Ctx, typename Body>
void run_joined_body_(void* frame) noexcept {
    using Value = joined_result_t_<Ctx, Body>;
    joined_body_ const& joined = *static_cast<joined_body_ const*>(frame);
    *static_cast<::foundation::core::Option<Value>*>(joined.value) = ::foundation::core::Option<Value>::some(
        call_joined_body_<Ctx, Body>(static_cast<::foundation::core::Option<Body>*>(joined.body)
                                         ->take()
                                         .expect("the frame of the door gives each body to one thread"),
                                     *static_cast<Ctx const*>(joined.ctx)));
}

// Registers HOOK for the child of each fork (THE FORK HOOK).  The error is
// the error number of pthread_atfork: enomem when the C library has no room
// for one more hook.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_in_fork_child_(void (*hook)() noexcept) noexcept;

}  // namespace detail

// A context that can change the process for each fork: one that owns Init.
template <typename Ctx>
concept CtxRegistersForkHooks = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

// Registers HOOK for the child of each fork after the call (THE FORK HOOK).
template <typename Ctx>
    requires CtxRegistersForkHooks<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_in_fork_child(Ctx const& /*init*/, void (*hook)() noexcept) noexcept {
    return detail::run_in_fork_child_(hook);
}

// A body of run_joined for a call with the context Ctx: an object that is not
// a reference and not const, that runs as an rvalue with no argument or with
// the context (THE CONTEXT OF A BODY, THE BODIES BY MOVE), and whose value can
// cross to the caller (THE RESULTS).
template <typename Body, typename Ctx>
concept JoinedBody = !std::is_reference_v<Body> && !std::is_const_v<Body>
                  && (detail::joined_form_<Ctx, Body>() != detail::JoinedForm::none)
                  && detail::JoinedValue<detail::joined_result_t_<Ctx, Body>>;

// True when a call names a body as an lvalue, which a root door refuses (THE
// BODIES BY MOVE).
template <typename... Bodies>
concept NamesABodyAsAnLvalue = (std::is_lvalue_reference_v<Bodies> || ...);

// The value of a call of run_joined (THE RESULTS): Unit when no body gives a
// value, else a Record of the value of each body, in the order of the bodies.
template <typename Ctx, typename... Bodies>
using joined_values_t =
    std::conditional_t<(std::is_void_v<detail::raw_joined_result_t_<Ctx, Bodies>> && ...), ::foundation::core::Unit,
                       ::foundation::core::Record<detail::joined_result_t_<Ctx, Bodies>...>>;

namespace detail {

// Each body is a parameter by value, so each body is an object of this frame,
// also when the caller moves one object two times.  Each body then moves into
// an Option of the frame, and its thread takes it out (THE BODIES BY MOVE).
template <typename Ctx, std::size_t... Is, typename... Bodies>
[[nodiscard]] ::foundation::core::Result<joined_values_t<Ctx, Bodies...>, start_error_t<Bodies...>>
run_joined_(Ctx const& ctx, ThreadOptions const& options, std::index_sequence<Is...> /*order*/,
            Bodies... bodies) noexcept {
    ::foundation::core::Record<::foundation::core::Option<Bodies>...> owned{
        ::foundation::core::Option<Bodies>::some(static_cast<Bodies&&>(bodies))...};
    ::foundation::core::Record<::foundation::core::Option<joined_result_t_<Ctx, Bodies>>...> slots{};
    joined_body_ joined[sizeof...(Bodies)]{
        joined_body_{&owned.at(::foundation::core::index<Is>()), &ctx, &slots.at(::foundation::core::index<Is>())}...};
    thread_task tasks[sizeof...(Bodies)]{thread_task{&joined[Is], &run_joined_body_<Ctx, Bodies>}...};
    using Values = joined_values_t<Ctx, Bodies...>;
    using Joined = ::foundation::core::Result<Values, start_error_t<Bodies...>>;
    ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> started =
        run_thread_tasks(::foundation::core::detail::view_of_array_(tasks), options);
    // Each path checks that each Option is empty after its last use of the
    // frame.
    if (started.is_err()) {
        ::foundation::os::Errno const error = std::move(started).err().expect("a refused start holds its error");
        // No thread ran, so each Option still holds its body (THE ERRORS).
        if constexpr (holds_linear_part_<Bodies...>) {
            Joined refusal{::foundation::core::err(RefusedStart<Bodies...>{
                error,
                ::foundation::core::Record<Bodies...>{
                    owned.at(::foundation::core::index<Is>()).take().expect("no thread ran a refused body")...}})};
            (std::move(owned.at(::foundation::core::index<Is>())).expect_none("the refusal holds each body"), ...);
            return refusal;
        } else {
            return Joined{::foundation::core::err(error)};
        }
    }
    if constexpr (std::same_as<Values, ::foundation::core::Unit>) {
        Joined joined_values{::foundation::core::Unit{}};
        (std::move(owned.at(::foundation::core::index<Is>())).expect_none("the thread of each body took it"), ...);
        return joined_values;
    } else {
        Joined joined_values{Values{
            std::move(slots.at(::foundation::core::index<Is>())).expect("each body of the call ran one time")...}};
        (std::move(owned.at(::foundation::core::index<Is>())).expect_none("the thread of each body took it"), ...);
        return joined_values;
    }
}

}  // namespace detail

// The policies of a body of run_joined (THE CLOSURE OF A BODY).  One body
// runs on one thread while the caller waits, and two or more bodies run at
// the same time.
inline constexpr ::foundation::closure::DoorPolicy joined_sole_policy{::foundation::closure::Crossing::SoleThread,
                                                                      ::foundation::closure::QttGrade::One, false};
inline constexpr ::foundation::closure::DoorPolicy joined_concurrent_policy{
    ::foundation::closure::Crossing::ConcurrentThreads, ::foundation::closure::QttGrade::One, false};

// The policy of a body of run_joined_raw_probe (THE RAW PROBE): the policy
// of sibling bodies, with no transfer fact of a field.
inline constexpr ::foundation::closure::DoorPolicy raw_probe_policy{::foundation::closure::Crossing::ConcurrentThreads,
                                                                    ::foundation::closure::QttGrade::One, false, false};

namespace detail {

// The check of one body of run_joined under the policy, as a static_assert
// whose message is the verdict.
template <::foundation::closure::DoorPolicy Policy, typename Body>
consteval void check_joined_body_() noexcept {
    constexpr ::foundation::closure::ClosureVerdict verdict =
        ::foundation::closure::check_closure(^^Body, Policy, "run_joined");
    static_assert(verdict.is_admitted(), verdict);
}

// The check of one body of run_joined_raw_probe.
template <typename Body>
consteval void check_raw_probe_body_() noexcept {
    constexpr ::foundation::closure::ClosureVerdict verdict =
        ::foundation::closure::check_closure(^^Body, raw_probe_policy, "run_joined_raw_probe");
    static_assert(verdict.is_admitted(), verdict);
}

}  // namespace detail

// Runs each body on a thread of its own with the options, and joins each
// thread before it returns.  Each body moves into the frame of the door, and
// the call ends before the frame does (THE BODIES BY MOVE).  The bodies run at
// the same time, so two bodies that share state need a synchronization of
// their own.  The value of the call holds the value of each body (THE
// RESULTS).
template <typename Ctx, typename... Bodies>
    requires(!NamesABodyAsAnLvalue<Bodies...>) && CtxStartsThreads<Ctx> && (sizeof...(Bodies) > 0)
         && (JoinedBody<Bodies, Ctx> && ...)
[[nodiscard]] ::foundation::core::Result<joined_values_t<Ctx, Bodies...>, start_error_t<Bodies...>>
run_joined(Ctx const& ctx, ThreadOptions const& options, Bodies&&... bodies) noexcept {
    constexpr ::foundation::closure::DoorPolicy policy =
        sizeof...(Bodies) == 1 ? joined_sole_policy : joined_concurrent_policy;
    (detail::check_joined_body_<policy, Bodies>(), ...);
    return detail::run_joined_(ctx, options, std::index_sequence_for<Bodies...>{}, static_cast<Bodies&&>(bodies)...);
}

// A body that the caller names as an lvalue stays an object of the caller, so
// one object given two times would run on two threads (THE BODIES BY MOVE).
template <typename Ctx, typename... Bodies>
    requires NamesABodyAsAnLvalue<Bodies...>
void run_joined(Ctx const& /*ctx*/, ThreadOptions const& /*options*/, Bodies&&... /*bodies*/) =
    delete("run_joined takes each body by move, so that two bodies are two objects: write the lambda in the call, or "
           "pass std::move(body)");

namespace detail {

// The value of a continuation: what it gives, or Unit when it gives nothing.
template <typename Continuation>
using beside_result_t_ = std::conditional_t<std::is_void_v<std::invoke_result_t<Continuation>>,
                                            ::foundation::core::Unit, std::invoke_result_t<Continuation>>;

}  // namespace detail

// A continuation of run_beside: an object that is not const, that runs one
// time as an rvalue with no argument on the calling thread, and whose value
// can sit in the result (THE THREAD BESIDE THE CALLER).
template <typename Continuation>
concept BesideContinuation = !std::is_const_v<Continuation> && std::is_nothrow_invocable_v<Continuation>
                          && ::foundation::core::RecordElement<detail::beside_result_t_<Continuation>>;

// The value of a call of run_beside (THE RESULTS): Unit when neither the body
// nor the continuation gives a value, else a Record of the value of the body
// and the value of the continuation.
template <typename Ctx, typename Body, typename Continuation>
using beside_values_t = std::conditional_t<
    std::is_void_v<detail::raw_joined_result_t_<Ctx, Body>> && std::is_void_v<std::invoke_result_t<Continuation>>,
    ::foundation::core::Unit,
    ::foundation::core::Record<detail::joined_result_t_<Ctx, Body>, detail::beside_result_t_<Continuation>>>;

// The policy of a body of run_beside (THE THREAD BESIDE THE CALLER).
inline constexpr ::foundation::closure::DoorPolicy beside_caller_policy{::foundation::closure::Crossing::BesideCaller,
                                                                        ::foundation::closure::QttGrade::One, false};

namespace detail {

// run_beside with the body and the continuation by value, so each is an
// object of this frame.  The body then moves into an Option of the frame,
// and its thread takes it out (THE BODIES BY MOVE).
template <typename Ctx, typename Body, typename Continuation>
[[nodiscard]] ::foundation::core::Result<beside_values_t<Ctx, Body, Continuation>, start_error_t<Body, Continuation>>
run_beside_(Ctx const& ctx, ThreadOptions const& options, Body body, Continuation continuation) noexcept {
    using Values = beside_values_t<Ctx, Body, Continuation>;
    using Beside = ::foundation::core::Result<Values, start_error_t<Body, Continuation>>;
    constexpr ::foundation::core::StaticIndex<0> only = ::foundation::core::index<0>();
    ::foundation::core::Record<::foundation::core::Option<Body>> owned{
        ::foundation::core::Option<Body>::some(static_cast<Body&&>(body))};
    ::foundation::core::Option<joined_result_t_<Ctx, Body>> slot{};
    joined_body_ joined{&owned.at(only), &ctx, &slot};
    thread_task task{&joined, &run_joined_body_<Ctx, Body>};
    ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno> started = start_thread_task(task, options);
    // Each path checks that the Option is empty after its last use of the
    // frame.
    if (started.is_err()) {
        ::foundation::os::Errno const error = std::move(started).err().expect("a refused start holds its error");
        // No thread ran, so the Option still holds the body (THE ERRORS).
        if constexpr (holds_linear_part_<Body, Continuation>) {
            Beside refusal{::foundation::core::err(RefusedStart<Body, Continuation>{
                error, ::foundation::core::Record<Body, Continuation>{
                           owned.at(only).take().expect("no thread ran a refused body"),
                           static_cast<Continuation&&>(continuation)}})};
            std::move(owned.at(only)).expect_none("the refusal holds the body");
            return refusal;
        } else {
            return Beside{::foundation::core::err(error)};
        }
    }
    std::uint64_t const handle = std::move(started).expect("a start that is no error holds its handle");
    // The once door owns the continuation, calls it one time and ends it.
    if constexpr (std::is_void_v<std::invoke_result_t<Continuation>>) {
        ::foundation::linear::call_once(static_cast<Continuation&&>(continuation));
        join_thread_task(handle);
        if constexpr (std::same_as<Values, ::foundation::core::Unit>) {
            Beside joined_values{::foundation::core::Unit{}};
            std::move(owned.at(only)).expect_none("the thread of the body took it");
            return joined_values;
        } else {
            Beside joined_values{
                Values{std::move(slot).expect("the body of the thread ran one time"), ::foundation::core::Unit{}}};
            std::move(owned.at(only)).expect_none("the thread of the body took it");
            return joined_values;
        }
    } else {
        beside_result_t_<Continuation> continued =
            ::foundation::linear::call_once(static_cast<Continuation&&>(continuation));
        join_thread_task(handle);
        Beside joined_values{
            Values{std::move(slot).expect("the body of the thread ran one time"), std::move(continued)}};
        std::move(owned.at(only)).expect_none("the thread of the body took it");
        return joined_values;
    }
}

}  // namespace detail

// Runs the body on a thread of its own with the options, runs the
// continuation on the calling thread at the same time, and joins the thread
// when the continuation returns (THE THREAD BESIDE THE CALLER).  A start that
// fails is the error of the call, and the continuation does not run then
// (THE ERRORS).
template <typename Ctx, typename Body, typename Continuation>
    requires(!NamesABodyAsAnLvalue<Body>)
         && CtxStartsThreads<Ctx> && JoinedBody<Body, Ctx> && BesideContinuation<Continuation>
[[nodiscard]] ::foundation::core::Result<beside_values_t<Ctx, Body, Continuation>, start_error_t<Body, Continuation>>
run_beside(Ctx const& ctx, ThreadOptions const& options, Body&& body, Continuation continuation) noexcept {
    constexpr ::foundation::closure::ClosureVerdict verdict =
        ::foundation::closure::check_closure(^^Body, beside_caller_policy, "run_beside");
    static_assert(verdict.is_admitted(), verdict);
    return detail::run_beside_(ctx, options, static_cast<Body&&>(body), static_cast<Continuation&&>(continuation));
}

// A body that the caller names as an lvalue stays an object of the caller
// (THE BODIES BY MOVE).
template <typename Ctx, typename Body, typename Continuation>
    requires NamesABodyAsAnLvalue<Body>
void run_beside(Ctx const& /*ctx*/, ThreadOptions const& /*options*/, Body&& /*body*/,
                Continuation /*continuation*/) = delete("run_beside takes its body by move: write the lambda in the "
                                                        "call, or pass std::move(body)");

// The index of the thread of a body of run_beside_each, from 0 to the count
// less one (THE THREAD BESIDE THE CALLER).
using BesideIndex = ::foundation::core::Id<struct BesideIndexTag, std::uint32_t>;

// A count of the threads of run_beside_each, from 0 to Max.
template <std::size_t Max>
using BesideCount = ::foundation::core::Index<Max + 1>;

// The policy of a body of run_beside_each: beside the caller, more than one
// call, and one object that many threads call at the same time (THE THREAD
// BESIDE THE CALLER).
inline constexpr ::foundation::closure::DoorPolicy beside_each_policy{::foundation::closure::Crossing::BesideCaller,
                                                                      ::foundation::closure::QttGrade::Omega, true};

// A body of run_beside_each: each thread calls it through a reference to
// const with the index of the thread, and it gives nothing.
template <typename Body>
concept BesideEachBody = std::is_nothrow_invocable_v<Body const&, BesideIndex>
                      && std::is_void_v<std::invoke_result_t<Body const&, BesideIndex>>;

namespace detail {

// What the thread of one index reads: the shared body and the index.  The
// frames live in the frame of run_beside_each, which joins each thread before
// it returns.
template <typename Body>
struct beside_each_frame_ {
    Body const* body = nullptr;
    BesideIndex index{0};
};

template <typename Body>
void run_beside_each_body_(void* frame) noexcept {
    beside_each_frame_<Body> const& source = *static_cast<beside_each_frame_<Body> const*>(frame);
    (*source.body)(source.index);
}

}  // namespace detail

// Starts count threads with the options, and each calls the body with its
// index.  The continuation runs on the calling thread at the same time, and
// the door joins each thread when the continuation returns (THE THREAD BESIDE
// THE CALLER).  The call gives the value of the continuation, or Unit when it
// gives nothing.  A first start that fails is the error of the call, and then
// no body and no continuation run (THE ERRORS).  Complexity: one start and one
// join for each thread, and a frame that holds Bound tasks.
template <typename Ctx, std::size_t Bound, typename Body, typename Continuation>
    requires CtxStartsThreads<Ctx> && BesideEachBody<Body> && BesideContinuation<Continuation>
[[nodiscard]] ::foundation::core::Result<detail::beside_result_t_<Continuation>, start_error_t<Continuation>>
run_beside_each(Ctx const& /*ctx*/, ThreadOptions const& options, ::foundation::core::Index<Bound> count, Body body,
                Continuation continuation) noexcept {
    constexpr ::foundation::closure::ClosureVerdict verdict =
        ::foundation::closure::check_closure(^^Body, beside_each_policy, "run_beside_each");
    static_assert(verdict.is_admitted(), verdict);
    using Continued = ::foundation::core::Result<detail::beside_result_t_<Continuation>, start_error_t<Continuation>>;
    using Slot = ::foundation::core::Index<Bound>;
    ::foundation::core::FixedArray<detail::beside_each_frame_<Body>, Bound> frames{};
    ::foundation::core::FixedArray<detail::thread_task, Bound> tasks{};
    // A task holds the address of its frame for the start door of a thread,
    // so the two arrays give their runs through the keyed run of a frame.
    ::foundation::core::View<detail::beside_each_frame_<Body>, Bound> const frame_run =
        frame_run_(::foundation::core::detail::FrameDoor::key(), frames);
    ::foundation::core::View<detail::thread_task, Bound> const task_run =
        frame_run_(::foundation::core::detail::FrameDoor::key(), tasks);
    for (Slot const slot : ::foundation::core::IndexRange<Bound>{}) {
        if (slot.raw() >= count.raw()) break;
        BesideIndex const index{::foundation::core::narrow<std::uint32_t>(slot.raw())
                                    .expect("run_beside_each: the index of a thread fits in 32 bits")};
        frame_run.at(slot) = detail::beside_each_frame_<Body>{&body, index};
        task_run.at(slot) = detail::thread_task{&frame_run.at(slot), &detail::run_beside_each_body_<Body>};
    }
    // The first count tasks.  An index of the bound is below the bound, so the
    // window always fits.
    ::foundation::core::View<detail::thread_task> const started_tasks =
        task_run.window(0, count.raw()).expect("an index of the bound is below the count of the frame");
    ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> started =
        detail::start_thread_tasks(started_tasks, options);
    if (started.is_err()) {
        ::foundation::os::Errno const error = std::move(started).err().expect("a refused start holds its error");
        // The body holds no linear value (beside_each_policy), so only the
        // continuation can come back (THE ERRORS).
        if constexpr (detail::holds_linear_part_<Continuation>) {
            return Continued{::foundation::core::err(RefusedStart<Continuation>{
                error, ::foundation::core::Record<Continuation>{static_cast<Continuation&&>(continuation)}})};
        } else {
            return Continued{::foundation::core::err(error)};
        }
    }
    if constexpr (std::is_void_v<std::invoke_result_t<Continuation>>) {
        ::foundation::linear::call_once(static_cast<Continuation&&>(continuation));
        detail::join_thread_tasks(started_tasks);
        return Continued{::foundation::core::Unit{}};
    } else {
        detail::beside_result_t_<Continuation> continued =
            ::foundation::linear::call_once(static_cast<Continuation&&>(continuation));
        detail::join_thread_tasks(started_tasks);
        return Continued{std::move(continued)};
    }
}

// The reason of a raw probe (THE RAW PROBE).  Only a string literal that is
// not empty gives one, so the reason names the probe at the call.
class RawProbeReason {
public:
    template <std::size_t Length>
        requires(Length > 1)
    consteval RawProbeReason(char const (&literal)[Length]) noexcept : text_{literal} {}

    [[nodiscard]] constexpr ::foundation::core::TextView text() const noexcept { return text_; }

private:
    ::foundation::core::TextView text_{};
};

// A context that is the authority of a raw probe: one that owns Test (THE
// RAW PROBE).
template <typename TestCtx>
concept CtxRunsRawProbes = ::foundation::effects::CtxOwnsCapability<TestCtx, ::foundation::effects::Effect::Test>;

// run_joined for a test that probes raw behavior, with the reason of the
// probe (THE RAW PROBE).  The bodies, the values and the errors are those of
// run_joined.
template <typename Ctx, typename TestCtx, typename... Bodies>
    requires(!NamesABodyAsAnLvalue<Bodies...>) && CtxStartsThreads<Ctx> && CtxRunsRawProbes<TestCtx>
         && (sizeof...(Bodies) > 0) && (JoinedBody<Bodies, Ctx> && ...)
[[nodiscard]] ::foundation::core::Result<joined_values_t<Ctx, Bodies...>, start_error_t<Bodies...>>
run_joined_raw_probe(Ctx const& ctx, TestCtx const& /*test*/, RawProbeReason /*reason*/, ThreadOptions const& options,
                     Bodies&&... bodies) noexcept {
    (detail::check_raw_probe_body_<Bodies>(), ...);
    return detail::run_joined_(ctx, options, std::index_sequence_for<Bodies...>{}, static_cast<Bodies&&>(bodies)...);
}

// A body that the caller names as an lvalue stays an object of the caller
// (THE BODIES BY MOVE).
template <typename Ctx, typename TestCtx, typename... Bodies>
    requires NamesABodyAsAnLvalue<Bodies...>
void run_joined_raw_probe(Ctx const& /*ctx*/, TestCtx const& /*test*/, RawProbeReason /*reason*/,
                          ThreadOptions const& /*options*/, Bodies&&... /*bodies*/) =
    delete("run_joined_raw_probe takes each body by move, so that two bodies are two objects: write the lambda in "
           "the call, or pass std::move(body)");

}  // namespace fixy::thread
