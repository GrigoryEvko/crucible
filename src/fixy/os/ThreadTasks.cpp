#include <fixy/os/ThreadTasks.h>

#include <cstddef>
#include <cstdlib>
#include <span>
#include <thread>
#include <vector>

namespace fixy::spawn::detail {

// The threads of the door.  This file is the one place that compiles
// <thread> for the door, so a translation unit that includes
// fixy/os/ThreadTasks.h does not compile it.
void run_thread_tasks(std::span<const thread_task> tasks) noexcept {
    // A jthread constructor is not noexcept, because the kernel can refuse
    // a new thread when a resource is exhausted.  Without the catch, that
    // failure gets to the noexcept boundary of this function.  The process
    // then ends in std::terminate when the build has exceptions, and in
    // std::abort when it has none.  With the catch, both builds abort, as
    // each other resource failure of the tree does.
#if defined(__cpp_exceptions)
    try {
#endif
        // The destructor of each std::jthread joins its thread, so every
        // task has finished when this block ends.
        std::vector<std::jthread> threads(tasks.size());
        for (std::size_t index = 0; index < tasks.size(); ++index) {
            const thread_task task = tasks[index];
            threads[index] = std::jthread{[task]() noexcept { task.run(task.frame); }};
        }
#if defined(__cpp_exceptions)
    } catch (...) {
        // The catch takes each type of exception.  A failure to start a
        // thread always ends the process, and the standard library does not
        // fix the type of the exception that reports the failure.
        std::abort();
    }
#endif
}

}  // namespace fixy::spawn::detail
