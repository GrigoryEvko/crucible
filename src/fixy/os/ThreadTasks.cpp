// The door of fixy/os/ThreadTasks.h: the one owner of <pthread.h>.  POSIX
// makes <pthread.h> give the symbols of <sched.h>, so the set of CPUs needs
// no include of its own.  A call of <pthread.h> gives its error number as
// its result, and the asm-generic numbers of foundation::os::Errno are the
// numbers of the C library on x86_64 and aarch64.

#include <fixy/os/ThreadTasks.h>

#include <fixy/os/Signal.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/core/Id.h>
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

// THE SET OF CPUS: a set of CPUs of the C library is an array of words of 64
// bits, with the bit of CPU N in the word N / 64 at the place N % 64, on
// x86_64 and aarch64.  An affinity mask has the same layout and the same
// size, so the bytes of the mask are the bytes of the set.
static_assert(::foundation::algebra::lattices::AffinityMask::kBits == CPU_SETSIZE,
              "an affinity mask must hold each CPU that a set of CPUs of the C library can name, and no more");
static_assert(sizeof(::foundation::algebra::lattices::AffinityMask) == sizeof(::cpu_set_t),
              "an affinity mask must have the bytes of a set of CPUs of the C library, and no more");
static_assert(std::is_trivially_copyable_v<::foundation::algebra::lattices::AffinityMask>
                  && std::is_trivially_copyable_v<::cpu_set_t>,
              "the door copies the bytes of an affinity mask into a set of CPUs");

// The error of a call of <pthread.h> that did not give 0.
[[nodiscard]] ::foundation::os::Errno error_of(int code) noexcept { return static_cast<::foundation::os::Errno>(code); }

// The set of CPUs of the C library that holds each CPU of the mask (THE SET
// OF CPUS).
void fill_cpu_set(::foundation::algebra::lattices::AffinityMask const& mask, ::cpu_set_t& set) noexcept {
    __builtin_memcpy(&set, &mask, sizeof(set));
}

struct TaskPositionTag final {};

// The position of a task in the list of one call, from 0.
using TaskPosition = ::foundation::core::Id<TaskPositionTag, std::uint64_t>;

// The set of the one CPU of the task at a position under OneCpuEach: the CPU
// at that position in the ascending CPUs of the mask, modulo their count.  An
// empty mask gives the empty set.  The walk reads the set of the C library
// with its macros, so it runs no contract check of the mask.  Complexity:
// linear in the CPUs of a set.
[[nodiscard]] ::cpu_set_t cpu_set_of_task(::foundation::algebra::lattices::AffinityMask const& mask,
                                          TaskPosition position) noexcept {
    ::cpu_set_t one{};
    std::uint64_t const count = mask.popcount();
    if (count == 0) return one;
    ::cpu_set_t all{};
    fill_cpu_set(mask, all);
    std::uint64_t remaining = position.raw() % count;
    for (std::size_t cpu = 0; cpu < std::size_t{CPU_SETSIZE}; ++cpu) {
        if (!CPU_ISSET(cpu, &all)) continue;
        if (remaining == 0) {
            CPU_SET(cpu, &one);
            return one;
        }
        --remaining;
    }
    return one;
}

// The function of each new thread.  The thread sets its alternate signal
// stack, and it takes its name, so its task reads the name of the call.  The
// stack object ends after the task, and it puts back the alternate stack
// that the thread had before (THE ALTERNATE STACK of fixy/os/ThreadTasks.h).
void* start_task(void* argument) noexcept {
    [[maybe_unused]] ::fixy::signal::ThreadSignalStack const signal_stack =
        ::fixy::signal::ThreadSignalStack::install_on_calling_thread();
    thread_task const& task = *static_cast<thread_task const*>(argument);
    if (task.name != nullptr && task.name->is_named()) {
        name_calling_thread(*task.name);
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
            fill_cpu_set(options.affinity, set);
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

    // Gives the next thread of the call the CPU of its task under
    // OneCpuEach, and gives the code of the C library.  The attributes keep
    // the mask of the options under SharedMask, and for an empty mask.
    [[nodiscard]] int place_task(ThreadOptions const& options, TaskPosition position) noexcept {
        if (options.placement != ThreadPlacement::OneCpuEach) return 0;
        if (options.affinity == ::foundation::algebra::lattices::AffinityMask{}) return 0;
        ::cpu_set_t const one = cpu_set_of_task(options.affinity, position);
        return ::pthread_attr_setaffinity_np(&attributes_, sizeof(one), &one);
    }

private:
    ::pthread_attr_t attributes_{};
    int code_ = 0;
    bool is_initialized_ = false;
};

}  // namespace

void name_calling_thread(ThreadName const& name) noexcept {
    static_cast<void>(::pthread_setname_np(::pthread_self(), name.c_str()));
}

::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_thread_tasks(::foundation::core::View<thread_task> tasks, ThreadOptions const& options) noexcept {
    if (options.guard_bytes == 0) return ::foundation::core::err(::foundation::os::Errno::einval);
    {
        StartAttributes attributes{options};
        if (attributes.code() != 0) return ::foundation::core::err(error_of(attributes.code()));
        std::size_t started = 0;
        for (thread_task& task : tasks) {
            task.name = &options.name;
            ::pthread_t handle{};
            int code = attributes.place_task(options, TaskPosition{started});
            if (code == 0) code = ::pthread_create(&handle, attributes.attributes(), &start_task, &task);
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
        join_thread_task(task.handle);
        task.handle = 0;
    }
    return ::foundation::core::Unit{};
}

::foundation::core::Result<std::uint64_t, ::foundation::os::Errno>
start_thread_task(thread_task& task, ThreadOptions const& options) noexcept {
    if (options.guard_bytes == 0) return ::foundation::core::err(::foundation::os::Errno::einval);
    StartAttributes attributes{options};
    if (attributes.code() != 0) return ::foundation::core::err(error_of(attributes.code()));
    ::pthread_t handle{};
    int code = attributes.place_task(options, TaskPosition{0});
    if (code == 0) code = ::pthread_create(&handle, attributes.attributes(), &start_task, &task);
    if (code != 0) return ::foundation::core::err(error_of(code));
    return handle;
}

void join_thread_task(std::uint64_t handle) noexcept {
    int const code = ::pthread_join(handle, nullptr);
    if (code != 0) [[unlikely]] {
        ::foundation::core::fatal("the join of a thread that the thread door started failed with the code {}", code);
    }
}

::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_in_fork_child_(void (*hook)() noexcept) noexcept {
    int const code = ::pthread_atfork(nullptr, nullptr, hook);
    if (code != 0) return ::foundation::core::err(error_of(code));
    return ::foundation::core::Unit{};
}

}  // namespace fixy::thread::detail
