// The door of fixy/os/ThreadTasks.h: the one owner of <pthread.h>.  POSIX
// makes <pthread.h> give the symbols of <sched.h>, so the set of CPUs needs
// no include of its own.  A call of <pthread.h> gives its error number as
// its result, and the asm-generic numbers of foundation::os::Errno are the
// numbers of the C library on x86_64 and aarch64.

#include <fixy/os/ThreadTasks.h>

#include <fixy/os/CpuPinned.h>
#include <foundation/core/Report.h>
#include <foundation/os/Errno.h>

#include <pthread.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fixy::thread::detail {

namespace {

static_assert(std::is_same_v<::pthread_t, std::uint64_t>,
              "the door keeps the handle of a thread in a std::uint64_t of the task list");

// The error of a call of <pthread.h> that did not give 0.
[[nodiscard]] ::foundation::os::Errno error_of(int code) noexcept { return static_cast<::foundation::os::Errno>(code); }

// The function of each new thread.  The thread takes its name first, so its
// task reads the name of the call.
void* start_task(void* argument) noexcept {
    thread_task const& task = *static_cast<thread_task const*>(argument);
    if (task.name != nullptr) {
        name_calling_thread(task.name);
    }
    task.run(task.frame);
    return nullptr;
}

// The attributes of each thread of one call, or the code of the call that
// refused an option.  The destructor ends the attributes in each case.
class StartAttributes {
public:
    explicit StartAttributes(ThreadOptions const& options) noexcept {
        code_ = ::pthread_attr_init(&attributes_);
        if (code_ != 0) return;
        is_initialized_ = true;
        code_ = ::pthread_attr_setstacksize(&attributes_, options.stack_bytes);
        if (code_ == 0) code_ = ::pthread_attr_setguardsize(&attributes_, options.guard_bytes);
        if (code_ == 0 && options.affinity != ::foundation::algebra::lattices::AffinityMask{}) {
            ::cpu_set_t set{};
            ::fixy::detail::fill_cpu_set(options.affinity, set);
            code_ = ::pthread_attr_setaffinity_np(&attributes_, sizeof(set), &set);
        }
    }

    StartAttributes(StartAttributes const&) = delete("the attributes of one call end one time");
    StartAttributes& operator=(StartAttributes const&) = delete("the attributes of one call end one time");

    ~StartAttributes() {
        if (is_initialized_) static_cast<void>(::pthread_attr_destroy(&attributes_));
    }

    [[nodiscard]] int code() const noexcept { return code_; }
    [[nodiscard]] ::pthread_attr_t const* attributes() const noexcept { return &attributes_; }

private:
    ::pthread_attr_t attributes_{};
    int code_ = 0;
    bool is_initialized_ = false;
};

}  // namespace

char const* name_text_(ThreadName const& name) noexcept { return name.text_; }

void name_calling_thread(char const* name) noexcept { static_cast<void>(::pthread_setname_np(::pthread_self(), name)); }

::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_thread_tasks(::foundation::core::View<thread_task> tasks, ThreadOptions const& options) noexcept {
    if (options.guard_bytes == 0) return ::foundation::core::err(::foundation::os::Errno::einval);
    {
        StartAttributes const attributes{options};
        if (attributes.code() != 0) return ::foundation::core::err(error_of(attributes.code()));
        char const* const name = name_text_(options.name);
        std::size_t started = 0;
        for (thread_task& task : tasks) {
            task.name = name;
            ::pthread_t handle{};
            int const code = ::pthread_create(&handle, attributes.attributes(), &start_task, &task);
            if (code != 0) [[unlikely]] {
                if (started == 0) return ::foundation::core::err(error_of(code));
                ::foundation::core::fatal(
                    "run_thread_tasks: a thread did not start after other threads of the call started, with the code "
                    "{}.  A started task can wait for a task that did not start, so a join can wait forever, and "
                    "the process ends",
                    code);
            }
            task.handle = handle;
            ++started;
        }
    }
    for (thread_task& task : tasks) {
        int const code = ::pthread_join(task.handle, nullptr);
        if (code != 0) [[unlikely]] {
            ::foundation::core::fatal("run_thread_tasks: the join of a thread that the door started failed with the "
                                      "code {}",
                                      code);
        }
        task.handle = 0;
    }
    return ::foundation::core::Unit{};
}

}  // namespace fixy::thread::detail
