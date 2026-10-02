#pragma once

// The task list of a fork that starts threads, and the one function that runs
// it.  A task is the address of a frame and the function that runs the task
// from that frame.  run_fork_tasks starts one thread for each task, runs each
// task on its own thread, and joins every thread before it returns.  A failure
// to start a thread ends the process.
//
// The body is in src/foundation/PermissionFork.cpp, so a translation unit that
// includes this header does not compile <thread>.  The two doors that start
// threads call it: the spawning arm of foundation/permissions/PermissionFork.h,
// and fixy/os/ThreadTasks.h.  The function has no gate of its own, and each
// door holds the gate.

#include <span>

namespace foundation::permissions::detail {

// One task: the address of its frame, and the function that runs the task
// from that frame.  The frame lives in the frame of the caller, which waits
// in run_fork_tasks until every thread has joined.
struct fork_task {
    void* frame = nullptr;
    void (*run)(void* frame) noexcept = nullptr;
};

void run_fork_tasks(std::span<const fork_task> tasks) noexcept;

}  // namespace foundation::permissions::detail
