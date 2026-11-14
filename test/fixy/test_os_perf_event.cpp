// The perf event door and the identity door, run against the kernel.
//
// The header of the perf event door holds the words and the record of the
// attributes of <linux/perf_event.h> with no include of it.  This file
// compares each word and the bytes of the record with the kernel header.  It
// also opens an event of the own process.  A host whose perf_event_paranoid
// is 2 or less opens it with no privilege, and a host with a stricter
// setting refuses it, so the check accepts the refusal of the kernel.  A
// type that no PMU has is refused on each host.  A counter that includes the
// children of the thread counts a thread that starts after the open, and a
// counter of the thread alone does not.

#include <fixy/Core.h>
#include <fixy/os/PerfEvent.h>
#include <fixy/os/Process.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Quarantine.h>

#include <fcntl.h>
#include <linux/perf_event.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include "../test_assert.h"

namespace eff = foundation::effects;
namespace perf = fixy::perf;
namespace process = fixy::process;

namespace {

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
// A context that starts threads, for the check of the children.
using ThreadCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
using Errno = foundation::os::Errno;

// The words of the door are the words of the kernel header.
static_assert(perf::detail::kTypeHardware == PERF_TYPE_HARDWARE);
static_assert(perf::detail::kTypeSoftware == PERF_TYPE_SOFTWARE);
static_assert(perf::detail::kTypeHardwareCache == PERF_TYPE_HW_CACHE);
static_assert(perf::detail::kCacheOperationRead == PERF_COUNT_HW_CACHE_OP_READ);
static_assert(perf::detail::kCacheResultMiss == PERF_COUNT_HW_CACHE_RESULT_MISS);
static_assert(perf::detail::kAttributesSize == PERF_ATTR_SIZE_VER0);
static_assert(perf::detail::kCloseOnExec == PERF_FLAG_FD_CLOEXEC);
static_assert(std::to_underlying(perf::HardwareEvent::instructions) == PERF_COUNT_HW_INSTRUCTIONS);
static_assert(std::to_underlying(perf::HardwareEvent::branch_misses) == PERF_COUNT_HW_BRANCH_MISSES);
static_assert(std::to_underlying(perf::SoftwareEvent::cpu_migrations) == PERF_COUNT_SW_CPU_MIGRATIONS);
static_assert(std::to_underlying(perf::SoftwareEvent::major_page_faults) == PERF_COUNT_SW_PAGE_FAULTS_MAJ);
static_assert(std::to_underlying(perf::SoftwareEvent::alignment_faults) == PERF_COUNT_SW_ALIGNMENT_FAULTS);
static_assert(std::to_underlying(perf::Cache::last_level) == PERF_COUNT_HW_CACHE_LL);
static_assert(std::to_underlying(perf::Cache::data_tlb) == PERF_COUNT_HW_CACHE_DTLB);
static_assert(perf::cache_read_miss(perf::Cache::data_tlb).config
              == (PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_READ << 8U)
                  | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16U)));

// The record of the attributes has the first published layout of the kernel
// struct.
static_assert(sizeof(perf::detail::EventAttributes) == PERF_ATTR_SIZE_VER0);
static_assert(offsetof(perf::detail::EventAttributes, size) == offsetof(::perf_event_attr, size));
static_assert(offsetof(perf::detail::EventAttributes, config) == offsetof(::perf_event_attr, config));
static_assert(offsetof(perf::detail::EventAttributes, sample_period) == offsetof(::perf_event_attr, sample_period));
static_assert(offsetof(perf::detail::EventAttributes, sample_type) == offsetof(::perf_event_attr, sample_type));
static_assert(offsetof(perf::detail::EventAttributes, read_format) == offsetof(::perf_event_attr, read_format));
static_assert(offsetof(perf::detail::EventAttributes, wakeup_events) == offsetof(::perf_event_attr, wakeup_events));
static_assert(offsetof(perf::detail::EventAttributes, breakpoint_type) == offsetof(::perf_event_attr, bp_type));
static_assert(offsetof(perf::detail::EventAttributes, config1) == offsetof(::perf_event_attr, config1));

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the kernel header and the C library are the models of the perf event door")
// The bytes of the attributes of the door are the first bytes of a kernel
// struct with the same fields.  The compare reads the bit-fields, so a wrong
// bit of inherit, of exclude_kernel or of exclude_hv fails it.
[[nodiscard]] bool attributes_match_the_kernel(perf::Event event, std::uint64_t period,
                                               perf::Children children) noexcept {
    ::perf_event_attr kernel{};
    kernel.type = event.type;
    kernel.size = PERF_ATTR_SIZE_VER0;
    kernel.config = event.config;
    kernel.sample_period = period;
    kernel.inherit = children == perf::Children::included ? 1 : 0;
    kernel.exclude_kernel = 1;
    kernel.exclude_hv = 1;
    perf::detail::EventAttributes const door = perf::detail::user_sample_attributes(event, period, children);
    return std::memcmp(&kernel, &door, PERF_ATTR_SIZE_VER0) == 0;
}

// True when the descriptor does not survive execve.
[[nodiscard]] bool closes_on_exec(fixy::fs::OwnedFd const& handle) noexcept {
    int const descriptor_flags = ::fcntl(handle.get(), F_GETFD);
    return descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) != 0;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

[[nodiscard]] int check_attributes() noexcept {
    constexpr perf::Children alone = perf::Children::excluded;
    constexpr perf::Children with_children = perf::Children::included;
    if (!attributes_match_the_kernel(perf::hardware_event(perf::HardwareEvent::branch_misses), 10000, alone)) return 1;
    if (!attributes_match_the_kernel(perf::software_event(perf::SoftwareEvent::major_page_faults), 1, alone)) return 2;
    if (!attributes_match_the_kernel(perf::cache_read_miss(perf::Cache::last_level), 10000, alone)) return 3;
    if (!attributes_match_the_kernel(perf::Event{.type = 11, .config = 0}, 100000, alone)) return 4;
    if (!attributes_match_the_kernel(perf::hardware_event(perf::HardwareEvent::instructions), 0, with_children)) {
        return 5;
    }
    return 0;
}

// The first thread of a process has the id of the process.
[[nodiscard]] int check_identity() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    process::ProcessId const self = process::current_process(ctx);
    process::ThreadId const thread = process::current_thread(ctx);
    if (std::to_underlying(self) == 0) return 11;
    if (std::to_underlying(self) != std::to_underlying(thread)) return 12;
    return 0;
}

// True for an error that a host gives when its settings or its kernel refuse
// a perf event of an unprivileged process.
[[nodiscard]] bool is_refusal(Errno error) noexcept {
    return error == Errno::eacces || error == Errno::eperm || error == Errno::enoent || error == Errno::enosys
        || error == Errno::eopnotsupp;
}

// The software event of the CPU migrations of the own process.  An open
// descriptor does not survive execve.
[[nodiscard]] int check_open_or_refusal() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    perf::Event const migrations = perf::software_event(perf::SoftwareEvent::cpu_migrations);
    return perf::mint_perf_event(ctx, migrations, 1, process::current_process(ctx))
        .match(
            [](fixy::Linear<fixy::fs::OwnedFd> opened) -> int {
                fixy::fs::OwnedFd const handle = std::move(opened).consume();
                if (!handle.is_open()) return 21;
                if (!closes_on_exec(handle)) return 22;
                return 0;
            },
            [](Errno error) -> int { return is_refusal(error) ? 0 : 23; });
}

// The counter of the instructions of the calling thread in user space.  A
// host whose perf_event_paranoid is 2 or less opens it with no privilege,
// unless it has no hardware PMU, as some virtual machines do.  A loop of user
// instructions then raises the count, and the count never falls.
[[nodiscard]] int check_instruction_counter() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    perf::Event const instructions = perf::hardware_event(perf::HardwareEvent::instructions);
    return perf::mint_perf_event(ctx, instructions, 0, process::current_thread(ctx))
        .match(
            [&ctx](fixy::Linear<fixy::fs::OwnedFd> opened) -> int {
                fixy::fs::OwnedFd const counter = std::move(opened).consume();
                std::uint64_t const before = perf::read_count(ctx, counter).value_or(0);
                std::uint64_t volatile sum = 0;
                for (std::uint64_t step = 0; step < 100000; ++step) {
                    sum = sum + step;
                }
                auto read = perf::read_count(ctx, counter);
                if (read.is_err()) return 41;
                std::uint64_t const after = std::move(read).expect("the read of the counter gave its count");
                if (after <= before) return 42;
                return 0;
            },
            [](Errno error) -> int { return is_refusal(error) ? 0 : 43; });
}

// The steps of the loop of the child thread of the check of the children.
// Each step runs more than one user instruction, so the loop runs more
// instructions than this count.  The parent only starts and joins the child,
// and in each build that runs far fewer instructions.
constexpr std::uint64_t kChildSteps = 10000000;

// Opens a counter of the user instructions of the calling thread with
// children, runs one child thread that loops kChildSteps steps, joins it, and
// gives the count.  The error is the error of the open.  A start or a read
// that fails ends the process.
[[nodiscard]] foundation::core::Result<std::uint64_t, Errno> count_across_a_child(perf::Children children) noexcept {
    using Count = foundation::core::Result<std::uint64_t, Errno>;
    ThreadCtx const ctx{eff::testing::bg()};
    perf::Event const instructions = perf::hardware_event(perf::HardwareEvent::instructions);
    return perf::mint_perf_event(ctx, instructions, 0, perf::Task{process::current_thread(ctx), children})
        .match(
            [&ctx](fixy::Linear<fixy::fs::OwnedFd> opened) -> Count {
                fixy::fs::OwnedFd const counter = std::move(opened).consume();
                auto child = []() noexcept {
                    std::uint64_t volatile sum = 0;
                    for (std::uint64_t step = 0; step < kChildSteps; ++step) {
                        sum = sum + step;
                    }
                };
                fixy::thread::run_joined(ctx, fixy::thread::ThreadOptions{}, child)
                    .expect("the thread door starts the child thread");
                return perf::read_count(ctx, counter).expect("the read of the counter gave its count");
            },
            [](Errno error) -> Count { return foundation::core::err(error); });
}

// A counter with its children counts the instructions of a thread that the
// task starts after the open.  A counter of the task alone does not.
[[nodiscard]] int check_children() noexcept {
    return count_across_a_child(perf::Children::included)
        .match(
            [](std::uint64_t with_children) -> int {
                if (with_children < kChildSteps) return 62;
                return count_across_a_child(perf::Children::excluded)
                    .match([](std::uint64_t alone) -> int { return alone < kChildSteps ? 0 : 63; },
                           [](Errno error) -> int { return is_refusal(error) ? 0 : 61; });
            },
            [](Errno error) -> int { return is_refusal(error) ? 0 : 61; });
}

// A closed handle has no count.
[[nodiscard]] int check_read_of_a_closed_handle() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    fixy::fs::OwnedFd const closed{};
    return perf::read_count(ctx, closed).err().value_or(Errno{}) == Errno::ebadf ? 0 : 51;
}

// No PMU of a host has the largest type, so each host refuses the event:
// enoent where perf_event_paranoid lets the process ask, and eacces or eperm
// where it does not.
[[nodiscard]] int check_refusal_of_an_unknown_type() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    perf::Event const unknown{.type = 0x7FFFFFFFU, .config = 0};
    return perf::mint_perf_event(ctx, unknown, 1, process::current_process(ctx))
        .match(
            [](fixy::Linear<fixy::fs::OwnedFd> opened) -> int {
                drop(std::move(opened));
                return 31;
            },
            [](Errno error) -> int {
                bool const is_expected =
                    error == Errno::enoent || error == Errno::eacces || error == Errno::eperm || error == Errno::enosys;
                return is_expected ? 0 : 32;
            });
}

}  // namespace

int main() {
    int failed = check_attributes();
    if (failed == 0) failed = check_identity();
    if (failed == 0) failed = check_open_or_refusal();
    if (failed == 0) failed = check_instruction_counter();
    if (failed == 0) failed = check_children();
    if (failed == 0) failed = check_read_of_a_closed_handle();
    if (failed == 0) failed = check_refusal_of_an_unknown_type();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_perf_event: check {} failed\n", failed);
        return failed;
    }
    crucible::test::pass("test_os_perf_event: passed\n");
    return 0;
}
