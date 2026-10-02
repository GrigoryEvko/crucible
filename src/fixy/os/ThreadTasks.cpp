#include <fixy/os/ThreadTasks.h>

#include <foundation/permissions/ForkTasks.h>

#include <span>

namespace fixy::spawn::detail {

void run_thread_tasks(std::span<const thread_task> tasks) noexcept {
    ::foundation::permissions::detail::run_fork_tasks(tasks);
}

}  // namespace fixy::spawn::detail
