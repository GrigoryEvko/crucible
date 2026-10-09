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
// THE GATE
//     run_joined takes a context that owns Bg or Init, the authority to start
//     threads.  The list form in detail has no gate of its own.  Its callers
//     in the base hold the gate: the fork of fixy/concurrent/PermissionFork.h,
//     the parallel loop of fixy/concurrent/Spawn.h and the pipeline of
//     fixy/concurrent/Pipeline.h.

#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fixy::thread {

// The stack of a thread under the default RLIMIT_STACK of Linux.
inline constexpr std::size_t default_stack_bytes = std::size_t{8} << 20;

// One page of 4 KiB.  glibc rounds a guard up to the page size of the host.
inline constexpr std::size_t default_guard_bytes = std::size_t{4} << 10;

class ThreadName;

namespace detail {

struct thread_task;

[[nodiscard]] char const* name_text_(ThreadName const& name) noexcept;

}  // namespace detail

// The name of a thread: a literal of 1 to 15 characters, the bound of the
// name of a task of the Linux kernel.  A name comes only from a string
// literal, so its text has static storage and outlives each thread.  A
// default name is empty, and a thread with no name keeps the name of the
// thread that starts it.
class ThreadName {
public:
    constexpr ThreadName() noexcept = default;

    template <std::size_t Length>
        requires(Length >= 2 && Length <= 16)
    consteval ThreadName(char const (&literal)[Length]) noexcept : text_{literal} {
        // A zero byte before the end would cut the name, and a literal
        // always ends with one.  A read past a constant array, or a zero
        // byte at the wrong place, stops the constant evaluation.
        for (std::size_t index = 0; index + 1 < Length; ++index) {
            if (literal[index] == '\0') name_holds_a_zero_byte_();
        }
        if (literal[Length - 1] != '\0') name_holds_a_zero_byte_();
    }

    [[nodiscard]] constexpr bool is_named() const noexcept { return text_ != nullptr; }

private:
    // Not constexpr, so a call of it stops the constant evaluation of the
    // constructor, and the diagnostic names it.
    static void name_holds_a_zero_byte_() noexcept;

    friend char const* detail::name_text_(ThreadName const& name) noexcept;

    char const* text_ = nullptr;
};

// The options of each thread of one call (THE OPTIONS).
struct ThreadOptions {
    std::size_t stack_bytes = default_stack_bytes;
    std::size_t guard_bytes = default_guard_bytes;
    ThreadName name{};
    ::foundation::algebra::lattices::AffinityMask affinity{};
};

// A context that may start threads: one that owns Bg or Init.
template <typename Ctx>
concept CtxStartsThreads =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Init>;

namespace detail {

// One task.  The caller writes the frame and the function, and the door
// writes the name and the handle.  The frame lives in the frame of the
// caller, which waits in the door until each thread has joined.
struct thread_task {
    void* frame = nullptr;
    void (*run)(void* frame) noexcept = nullptr;
    char const* name = nullptr;
    std::uint64_t handle = 0;
};

// Starts one thread for each task with the options, runs each task on its
// thread, and joins each thread before it returns (THE ERRORS).
// Complexity: one start and one join for each task.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_thread_tasks(::foundation::core::View<thread_task> tasks, ThreadOptions const& options) noexcept;

// Gives the calling thread a name: a text of at most 15 characters with a
// zero byte at its end.  The caller holds the bound in its type, as
// mint_thread_name of fixy/os/ThreadName.h does.  ERANGE is the only failure
// of the call, and the bound excludes it, so the door reads no error.
void name_calling_thread(char const* name) noexcept;

template <typename Body>
void run_body_(void* frame) noexcept {
    (*static_cast<Body*>(frame))();
}

}  // namespace detail

// Runs each body on a thread of its own with the options, and joins each
// thread before it returns.  Each body is an object of the caller, and the
// call ends before the object does.  The bodies run at the same time, so
// two bodies that share state need a synchronization of their own.
template <typename Ctx, typename... Bodies>
    requires CtxStartsThreads<Ctx> && (sizeof...(Bodies) > 0)
          && ((!std::is_const_v<Bodies> && std::is_nothrow_invocable_v<Bodies&>) && ...)
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_joined(Ctx const& /*ctx*/, ThreadOptions const& options, Bodies&... bodies) noexcept {
    detail::thread_task tasks[sizeof...(Bodies)]{detail::thread_task{&bodies, &detail::run_body_<Bodies>}...};
    return detail::run_thread_tasks(::foundation::core::detail::view_over_(tasks, sizeof...(Bodies)), options);
}

}  // namespace fixy::thread
