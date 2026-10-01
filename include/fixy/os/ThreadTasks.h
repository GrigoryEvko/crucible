#pragma once

// The door that starts the threads of a fixy surface and joins them.
//
// A task is the address of a frame and the function that runs the task from
// that frame.  run_thread_tasks starts one thread for each task, runs each
// task on its own thread, and joins every thread before it returns.  The
// body is in src/fixy/os/ThreadTasks.cpp, so a translation unit that includes
// this header does not compile <thread>.
//
// The door has no gate of its own.  Its caller holds the gate: the pipeline
// of fixy/concurrent/Pipeline.h calls it only for a context that owns Bg or
// Init, the authority to start threads.

#include <foundation/Platform.h>

#include <span>

namespace fixy::spawn::detail {

// One task: the address of its frame, and the function that runs the task
// from that frame.  The frame lives in the frame of the caller, which waits
// in run_thread_tasks until every thread has joined.
struct thread_task {
    void* frame = nullptr;
    void (*run)(void* frame) noexcept = nullptr;
};

// Starts one thread for each task, runs the task on its thread, and joins
// every thread before it returns.  A failure to start a thread ends the
// process.
void run_thread_tasks(std::span<const thread_task> tasks) noexcept;

}  // namespace fixy::spawn::detail
