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
//     that is not valid.  No thread of the call runs then.  A start that fails
//     after other threads of the call started ends the process.  A started
//     task can wait for a task that did not start, so a join of the started
//     threads could wait forever, and the door has no correct state to give
//     back.  A join that fails ends the process too, because the door started
//     and joins each thread one time.
//
// THE ALTERNATE STACK
//     Each thread that the door starts holds a ThreadSignalStack of
//     fixy/os/Signal.h while its task runs.  The handler of a fatal signal
//     then runs on that stack, also when the task overflows its own stack.
//     The thread puts back its earlier alternate stack and unmaps the stack
//     of the door when its task returns.  A thread that the base does not
//     start gets no alternate stack from the door.
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
// THE GATE
//     run_joined takes a context that owns Bg or Init, the authority to start
//     threads.  The list form and the owned form in detail have no gate of
//     their own.  Their callers in the base hold the gate: the fork of
//     fixy/concurrent/PermissionFork.h, the parallel loop of
//     fixy/concurrent/Spawn.h, the pipeline of fixy/concurrent/Pipeline.h and
//     the mint of fixy/os/OwnedThread.h.  run_in_fork_child changes the process
//     for each fork after it, so it takes a context that owns Init.

#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
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
// context is Shareable (THE CONTEXT OF A BODY).
template <typename Ctx, typename Body>
inline constexpr bool takes_context_ =
    std::is_nothrow_invocable_v<Body&, Ctx const&> && ::foundation::transfer::Shareable<Ctx>;

// The frame of one body of run_joined: the body and the context of the call.
// It lives in the frame of run_joined, which waits for each thread (THE
// CONTEXT OF A BODY).
struct joined_body_ {
    void* body = nullptr;
    void const* ctx = nullptr;
};

template <typename Ctx, typename Body>
void run_joined_body_(void* frame) noexcept {
    joined_body_ const& joined = *static_cast<joined_body_ const*>(frame);
    Body& body = *static_cast<Body*>(joined.body);
    if constexpr (takes_context_<Ctx, Body>) {
        body(*static_cast<Ctx const*>(joined.ctx));
    } else {
        body();
    }
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
// const, and that runs with no argument or with the context (THE CONTEXT OF A
// BODY).
template <typename Body, typename Ctx>
concept JoinedBody =
    !std::is_const_v<Body> && (std::is_nothrow_invocable_v<Body&> || detail::takes_context_<Ctx, Body>);

// Runs each body on a thread of its own with the options, and joins each
// thread before it returns.  Each body is an object of the caller, and the
// call ends before the object does.  The bodies run at the same time, so
// two bodies that share state need a synchronization of their own.
template <typename Ctx, typename... Bodies>
    requires CtxStartsThreads<Ctx> && (sizeof...(Bodies) > 0) && (JoinedBody<Bodies, Ctx> && ...)
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_joined(Ctx const& ctx, ThreadOptions const& options, Bodies&... bodies) noexcept {
    detail::joined_body_ joined[sizeof...(Bodies)]{detail::joined_body_{&bodies, &ctx}...};
    // A braced list runs its parts in order, so each task takes the frame of
    // its own body.
    std::size_t index = 0;
    detail::thread_task tasks[sizeof...(Bodies)]{
        detail::thread_task{&joined[index++], &detail::run_joined_body_<Ctx, Bodies>}...};
    return detail::run_thread_tasks(::foundation::core::detail::view_over_(tasks, sizeof...(Bodies)), options);
}

}  // namespace fixy::thread
