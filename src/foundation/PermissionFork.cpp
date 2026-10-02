#include <foundation/permissions/PermissionFork.h>

#include <cstddef>
#include <cstdlib>
#include <span>
#include <thread>
#include <vector>

namespace foundation::permissions::detail {

// The threads of the spawning arm of the fork, and of the door of
// fixy/os/ThreadTasks.h.  This file is the one place that compiles <thread>
// for the two, so a translation unit that includes PermissionFork.h or
// ThreadTasks.h does not compile it.
void run_fork_tasks(std::span<const fork_task> tasks) noexcept {
    // A jthread constructor is not noexcept, because the thread creation
    // under it can fail on resource exhaustion.  Without the catch, that
    // failure reaches the noexcept boundary of this function and
    // terminates when the build has exceptions on, but aborts when it does
    // not.  The catch makes both builds abort, which is what a resource
    // failure does everywhere else here.
#if defined(__cpp_exceptions)
    try {
#endif
        // The destructor of each std::jthread joins its thread, so every
        // body has finished when this block ends.
        std::vector<std::jthread> threads(tasks.size());
        for (std::size_t index = 0; index < tasks.size(); ++index) {
            const fork_task task = tasks[index];
            threads[index] = std::jthread{[task]() noexcept { task.run(task.frame); }};
        }
#if defined(__cpp_exceptions)
    } catch (...) {
        // The catch is deliberately untyped.  The contract is that any
        // failure to construct a thread aborts, and the exception type a
        // given standard library reports it with is not fixed.
        std::abort();
    }
#endif
}

}  // namespace foundation::permissions::detail
