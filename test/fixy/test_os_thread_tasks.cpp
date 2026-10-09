// Tests of the thread door of fixy/os/ThreadTasks.h.  Each test starts
// threads through run_joined and reads back, on the started thread, what
// the C library reports for that thread: the stack, the guard area, the
// name and the CPUs.  The tsan preset runs the same binary.

#include <fixy/os/ThreadTasks.h>

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>

namespace {

// The facts of one thread, as the C library reports them.  The name is
// its 16 bytes in two words, so two names compare as two pairs of words.
struct ThreadFacts {
    std::size_t stack_bytes = 0;
    std::size_t guard_bytes = 0;
    std::uint64_t name_low = 0;
    std::uint64_t name_high = 0;
    std::uint64_t cpu_count = 0;
    std::uint64_t first_cpu = 0;
};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the C library reports the attributes of the calling thread")
#include <pthread.h>

namespace {

ThreadFacts read_calling_thread_facts() noexcept {
    ThreadFacts facts{};
    ::pthread_attr_t attributes{};
    if (::pthread_getattr_np(::pthread_self(), &attributes) == 0) {
        static_cast<void>(::pthread_attr_getstacksize(&attributes, &facts.stack_bytes));
        static_cast<void>(::pthread_attr_getguardsize(&attributes, &facts.guard_bytes));
        static_cast<void>(::pthread_attr_destroy(&attributes));
    }
    char name[16]{};
    static_cast<void>(::pthread_getname_np(::pthread_self(), name, sizeof(name)));
    __builtin_memcpy(&facts.name_low, name, 8);
    __builtin_memcpy(&facts.name_high, name + 8, 8);
    ::cpu_set_t set{};
    if (::pthread_getaffinity_np(::pthread_self(), sizeof(set), &set) == 0) {
        facts.cpu_count = static_cast<std::uint64_t>(CPU_COUNT(&set));
        for (std::size_t cpu = 0; cpu < std::size_t{CPU_SETSIZE}; ++cpu) {
            if (CPU_ISSET(cpu, &set)) {
                facts.first_cpu = cpu;
                break;
            }
        }
    }
    return facts;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

namespace eff = ::foundation::effects;
using ::foundation::algebra::lattices::AffinityMask;
using ::foundation::core::Atomic;
using ::foundation::os::Errno;
using ::fixy::thread::ThreadOptions;

using WorkerCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;

constinit thread_local bool is_main_thread = false;

[[nodiscard]] bool have_same_name(ThreadFacts const& left, ThreadFacts const& right) noexcept {
    return left.name_low == right.name_low && left.name_high == right.name_high;
}

// Two bodies that wait on each other end only if they run at the same
// time.  Each body also reads that it does not run on the main thread.
void test_each_body_runs_on_a_thread_of_its_own() {
    Atomic<bool> left_arrived{false};
    Atomic<bool> right_arrived{false};
    Atomic<bool> left_on_main{true};
    Atomic<bool> right_on_main{true};
    auto left = [&]() noexcept {
        left_on_main.store_release(is_main_thread);
        left_arrived.store_release(true);
        while (!right_arrived.load_acquire())
            CRUCIBLE_SPIN_PAUSE;
    };
    auto right = [&]() noexcept {
        right_on_main.store_release(is_main_thread);
        right_arrived.store_release(true);
        while (!left_arrived.load_acquire())
            CRUCIBLE_SPIN_PAUSE;
    };
    auto const started = ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, ThreadOptions{}, left, right);
    CRUCIBLE_FATAL_INVARIANT(started.is_ok());
    CRUCIBLE_FATAL_INVARIANT(!left_on_main.load_acquire() && !right_on_main.load_acquire());
}

// The default options: a stack of 8 MiB, a guard of at least 4 KiB, the
// name and the CPUs of the thread that starts the thread.
void test_default_options() {
    ThreadFacts const main_facts = read_calling_thread_facts();
    ThreadFacts worker_facts{};
    auto worker = [&]() noexcept { worker_facts = read_calling_thread_facts(); };
    auto const started = ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, ThreadOptions{}, worker);
    CRUCIBLE_FATAL_INVARIANT(started.is_ok());
    CRUCIBLE_FATAL_INVARIANT(worker_facts.stack_bytes == ::fixy::thread::default_stack_bytes);
    CRUCIBLE_FATAL_INVARIANT(worker_facts.guard_bytes >= ::fixy::thread::default_guard_bytes);
    CRUCIBLE_FATAL_INVARIANT(have_same_name(worker_facts, main_facts));
    CRUCIBLE_FATAL_INVARIANT(worker_facts.cpu_count == main_facts.cpu_count);
    CRUCIBLE_FATAL_INVARIANT(worker_facts.first_cpu == main_facts.first_cpu);
}

// The facts of a thread that the options below started.
void expect_probe_options(ThreadFacts const& facts, ThreadOptions const& options, ThreadFacts const& main_facts) {
    ThreadFacts expected_name{};
    expected_name.name_low = 0x6f772d65626f7270ULL;  // "probe-wo"
    expected_name.name_high = 0x0000000072656b72ULL;  // "rker"
    CRUCIBLE_FATAL_INVARIANT(facts.stack_bytes == options.stack_bytes);
    CRUCIBLE_FATAL_INVARIANT(facts.guard_bytes == options.guard_bytes);
    CRUCIBLE_FATAL_INVARIANT(have_same_name(facts, expected_name));
    CRUCIBLE_FATAL_INVARIANT(facts.cpu_count == 1);
    CRUCIBLE_FATAL_INVARIANT(facts.first_cpu == main_facts.first_cpu);
}

// Each option reaches each thread of the call.
void test_each_option_reaches_each_thread() {
    ThreadFacts const main_facts = read_calling_thread_facts();
    CRUCIBLE_FATAL_INVARIANT(main_facts.cpu_count > 0);
    ThreadOptions const options{
        .stack_bytes = std::size_t{1} << 20,
        .guard_bytes = std::size_t{64} << 10,
        .name = "probe-worker",
        .affinity = AffinityMask::single(static_cast<std::uint16_t>(main_facts.first_cpu)),
    };
    ThreadFacts first_facts{};
    ThreadFacts second_facts{};
    auto first = [&]() noexcept { first_facts = read_calling_thread_facts(); };
    auto second = [&]() noexcept { second_facts = read_calling_thread_facts(); };
    auto const started = ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, options, first, second);
    CRUCIBLE_FATAL_INVARIANT(started.is_ok());
    expect_probe_options(first_facts, options, main_facts);
    expect_probe_options(second_facts, options, main_facts);
}

// An option that is not valid is the error of the call, and no thread of
// the call runs.
void test_refused_option_runs_no_thread() {
    Atomic<bool> has_run{false};
    auto body = [&]() noexcept { has_run.store_release(true); };

    ThreadOptions const no_guard{.guard_bytes = 0};
    auto const refused_guard = ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, no_guard, body);
    CRUCIBLE_FATAL_INVARIANT(refused_guard.is_err());
    CRUCIBLE_FATAL_INVARIANT(refused_guard.err().value_or(Errno::eperm) == Errno::einval);

    ThreadOptions const small_stack{.stack_bytes = 1};
    auto const refused_stack = ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, small_stack, body);
    CRUCIBLE_FATAL_INVARIANT(refused_stack.err().value_or(Errno::eperm) == Errno::einval);

    // The last CPU of a mask is past each CPU of the host, so the kernel
    // refuses the CPUs of the new thread.
    ThreadOptions const no_usable_cpu{.affinity = AffinityMask::single(AffinityMask::kMaxCore)};
    auto const refused_cpus = ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, no_usable_cpu, body);
    CRUCIBLE_FATAL_INVARIANT(refused_cpus.err().value_or(Errno::eperm) == Errno::einval);

    CRUCIBLE_FATAL_INVARIANT(!has_run.load_acquire());
}

}  // namespace

int main() {
    is_main_thread = true;
    test_each_body_runs_on_a_thread_of_its_own();
    test_default_options();
    test_each_option_reaches_each_thread();
    test_refused_option_runs_no_thread();
    return 0;
}
