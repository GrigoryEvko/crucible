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
#include <foundation/Transfer.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
    // The second CPU of the mask, or the first one when the mask holds one.
    std::uint64_t second_cpu = 0;
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
        std::size_t found = 0;
        for (std::size_t cpu = 0; cpu < std::size_t{CPU_SETSIZE} && found < 2; ++cpu) {
            if (!CPU_ISSET(cpu, &set)) continue;
            if (found == 0) facts.first_cpu = cpu;
            facts.second_cpu = cpu;
            ++found;
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

// A value of one owner thread, which does not move to another thread.
struct[[= ::foundation::transfer::thread_bound{}]] OwnerThreadValue {
    int value = 0;
};

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

// A body takes the context of the call as its parameter, beside a body that
// takes none, so no body captures a context.  Each runs on a thread of its
// own.
void test_a_body_takes_the_context_of_the_call() {
    WorkerCtx const ctx{eff::testing::bg()};
    Atomic<bool> is_context_body_off_main{false};
    Atomic<bool> is_plain_body_off_main{false};
    auto with_context = [&is_context_body_off_main](WorkerCtx const& /*thread_ctx*/) noexcept {
        is_context_body_off_main.store_release(!is_main_thread);
    };
    auto plain = [&is_plain_body_off_main]() noexcept { is_plain_body_off_main.store_release(!is_main_thread); };
    CRUCIBLE_FATAL_INVARIANT(::fixy::thread::run_joined(ctx, ThreadOptions{}, with_context, plain).is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_context_body_off_main.load_acquire() && is_plain_body_off_main.load_acquire());
}

// Each body gives its value back, in the order of the bodies, and a body that
// gives nothing has Unit there.  A value of one owner thread cannot cross to
// the caller, and a view is a borrow, so the door refuses a body that gives
// either.
void test_each_body_gives_its_value() {
    WorkerCtx const ctx{eff::testing::bg()};
    auto seven = []() noexcept { return std::uint32_t{7}; };
    auto nothing = []() noexcept {};
    auto is_off_main = [](WorkerCtx const& /*thread_ctx*/) noexcept { return !is_main_thread; };
    auto [value, unit, was_off_main] = ::fixy::thread::run_joined(ctx, ThreadOptions{}, seven, nothing, is_off_main)
                                           .expect("the door starts three threads with the default options");
    static_assert(std::is_same_v<decltype(unit), ::foundation::core::Unit>);
    CRUCIBLE_FATAL_INVARIANT(value == 7 && was_off_main);

    auto none_give = []() noexcept {};
    static_assert(std::is_same_v<::fixy::thread::joined_values_t<WorkerCtx, decltype(none_give), decltype(nothing)>,
                                 ::foundation::core::Unit>);
    auto gives_an_owner_thread_value = []() noexcept { return OwnerThreadValue{}; };
    auto gives_a_view = []() noexcept { return ::foundation::core::View<int const>{}; };
    static_assert(::fixy::thread::JoinedBody<decltype(seven), WorkerCtx>);
    static_assert(!::fixy::thread::JoinedBody<decltype(gives_an_owner_thread_value), WorkerCtx>);
    static_assert(!::fixy::thread::JoinedBody<decltype(gives_a_view), WorkerCtx>);
}

// The body runs on a thread of its own beside the continuation, which runs on
// the calling thread at the same time.  Each waits for the other, so the call
// ends only if the two run at the same time.  The values of the two come
// back.  A refused option is the error of the call, and then neither runs.
void test_a_body_runs_beside_the_continuation() {
    WorkerCtx const ctx{eff::testing::bg()};
    Atomic<bool> has_body_arrived{false};
    Atomic<bool> has_continuation_arrived{false};
    auto body = [&has_body_arrived, &has_continuation_arrived]() noexcept {
        has_body_arrived.store_release(true);
        while (!has_continuation_arrived.load_acquire())
            CRUCIBLE_SPIN_PAUSE;
        return !is_main_thread;
    };
    auto continuation = [&has_body_arrived, &has_continuation_arrived]() noexcept {
        has_continuation_arrived.store_release(true);
        while (!has_body_arrived.load_acquire())
            CRUCIBLE_SPIN_PAUSE;
        return is_main_thread;
    };
    auto [was_body_off_main, was_continuation_on_main] =
        ::fixy::thread::run_beside(ctx, ThreadOptions{}, body, continuation)
            .expect("the door starts a thread beside the caller with the default options");
    CRUCIBLE_FATAL_INVARIANT(was_body_off_main && was_continuation_on_main);

    Atomic<bool> has_run{false};
    auto marks = [&has_run]() noexcept { has_run.store_release(true); };
    auto also_marks = [&has_run]() noexcept { has_run.store_release(true); };
    static_assert(std::is_same_v<::fixy::thread::beside_values_t<WorkerCtx, decltype(marks), decltype(also_marks)>,
                                 ::foundation::core::Unit>);
    auto const refused = ::fixy::thread::run_beside(ctx, ThreadOptions{.guard_bytes = 0}, marks, also_marks);
    CRUCIBLE_FATAL_INVARIANT(refused.err().value_or(Errno::eperm) == Errno::einval);
    CRUCIBLE_FATAL_INVARIANT(!has_run.load_acquire());
}

// The counted form starts one thread for each index, and each calls the one
// body with its index.  The continuation waits until each body arrived, so
// the call ends only if the bodies run beside it.  A count of zero runs the
// continuation alone, and a refused option runs nothing.
void test_each_index_runs_beside_the_continuation() {
    WorkerCtx const ctx{eff::testing::bg()};
    Atomic<std::uint32_t> arrived{0};
    Atomic<std::uint32_t> index_sum{0};
    Atomic<bool> ran_on_main{false};
    auto body = [&arrived, &index_sum, &ran_on_main](::fixy::thread::BesideIndex index) noexcept {
        static_cast<void>(index_sum.fetch_add_acq_rel(index.raw()));
        if (is_main_thread) ran_on_main.store_release(true);
        static_cast<void>(arrived.fetch_add_acq_rel(1));
    };
    auto waits_for_three = [&arrived]() noexcept {
        while (arrived.load_acquire() < 3)
            CRUCIBLE_SPIN_PAUSE;
        return is_main_thread;
    };
    using Count = ::fixy::thread::BesideCount<4>;
    bool const was_continuation_on_main =
        ::fixy::thread::run_beside_each(ctx, ThreadOptions{}, Count::check(3).expect("3 is at most 4"), body,
                                        waits_for_three)
            .expect("the door starts three threads beside the caller with the default options");
    CRUCIBLE_FATAL_INVARIANT(was_continuation_on_main);
    CRUCIBLE_FATAL_INVARIANT(index_sum.load_acquire() == 0 + 1 + 2);
    CRUCIBLE_FATAL_INVARIANT(!ran_on_main.load_acquire());

    Atomic<bool> has_continued{false};
    auto marks = [&has_continued]() noexcept { has_continued.store_release(true); };
    ::fixy::thread::run_beside_each(ctx, ThreadOptions{}, Count::check(0).expect("0 is at most 4"), body, marks)
        .expect("a count of zero starts no thread");
    CRUCIBLE_FATAL_INVARIANT(has_continued.load_acquire() && arrived.load_acquire() == 3);

    Atomic<bool> has_refused_run{false};
    auto refused_mark = [&has_refused_run]() noexcept { has_refused_run.store_release(true); };
    auto const refused = ::fixy::thread::run_beside_each(ctx, ThreadOptions{.guard_bytes = 0},
                                                         Count::check(2).expect("2 is at most 4"), body, refused_mark);
    CRUCIBLE_FATAL_INVARIANT(refused.err().value_or(Errno::eperm) == Errno::einval);
    CRUCIBLE_FATAL_INVARIANT(!has_refused_run.load_acquire() && arrived.load_acquire() == 3);
}

// The default options: a stack of 8 MiB, a guard of at least 4 KiB, the
// name and the CPUs of the thread that starts the thread.
void test_default_options() {
    ThreadFacts const main_facts = read_calling_thread_facts();
    auto worker = []() noexcept { return read_calling_thread_facts(); };
    auto [worker_facts] = ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, ThreadOptions{}, worker)
                              .expect("the door starts a thread with the default options");
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
    auto first = []() noexcept { return read_calling_thread_facts(); };
    auto second = []() noexcept { return read_calling_thread_facts(); };
    auto [first_facts, second_facts] = ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, options, first, second)
                                           .expect("the door starts two threads with the probe options");
    expect_probe_options(first_facts, options, main_facts);
    expect_probe_options(second_facts, options, main_facts);
}

// Under OneCpuEach the thread of the task k takes only the CPU k of the mask,
// modulo the count of its CPUs.  Three tasks on a mask of two CPUs take the
// first CPU, the second CPU and the first CPU again, each as a mask of one CPU.
// On isolated CPUs, threads with a mask of several CPUs all stay on the CPU of
// the thread that starts them, so this placement is the way to run them on
// more than one CPU.
void test_one_cpu_each() {
    ThreadFacts const main_facts = read_calling_thread_facts();
    if (main_facts.cpu_count < 2) return;
    auto const first = static_cast<std::uint16_t>(main_facts.first_cpu);
    auto const second = static_cast<std::uint16_t>(main_facts.second_cpu);
    ThreadOptions const options{
        .affinity = ::foundation::algebra::lattices::AffinityLattice::join(AffinityMask::single(first),
                                                                           AffinityMask::single(second)),
        .placement = ::fixy::thread::ThreadPlacement::OneCpuEach,
    };
    auto first_task = []() noexcept { return read_calling_thread_facts(); };
    auto second_task = []() noexcept { return read_calling_thread_facts(); };
    auto third_task = []() noexcept { return read_calling_thread_facts(); };
    auto [first_facts, second_facts, third_facts] =
        ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, options, first_task, second_task, third_task)
            .expect("the door starts three threads, one on each CPU");
    CRUCIBLE_FATAL_INVARIANT(first_facts.cpu_count == 1 && second_facts.cpu_count == 1 && third_facts.cpu_count == 1);
    CRUCIBLE_FATAL_INVARIANT(first_facts.first_cpu == first);
    CRUCIBLE_FATAL_INVARIANT(second_facts.first_cpu == second);
    CRUCIBLE_FATAL_INVARIANT(third_facts.first_cpu == first);

    // SharedMask gives each thread both CPUs.
    ThreadOptions const shared{.affinity = options.affinity};
    auto shared_task = []() noexcept { return read_calling_thread_facts(); };
    auto [shared_facts] = ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, shared, shared_task)
                              .expect("the door starts a thread on the shared mask");
    CRUCIBLE_FATAL_INVARIANT(shared_facts.cpu_count == 2);
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
    test_a_body_takes_the_context_of_the_call();
    test_each_body_gives_its_value();
    test_a_body_runs_beside_the_continuation();
    test_each_index_runs_beside_the_continuation();
    test_default_options();
    test_each_option_reaches_each_thread();
    test_one_cpu_each();
    test_refused_option_runs_no_thread();
    return 0;
}
