// The signal door of fixy (fixy/os/Signal.h).  It is the one includer of
// <signal.h>, and it keeps the calls of sigaction, sigaltstack and raise in
// the C library (decision D12 of misc/01_10_2026_quarantine.md).  The C
// library keeps the dispositions and the masks of its threads, so the base
// takes signals only through it.

#include <fixy/os/Signal.h>

#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Report.h>
#include <foundation/os/Auxiliary.h>

#include <signal.h>

#include <cstddef>
#include <cstdint>

namespace fixy::signal::detail {

// The signals that end a process with no exit hook.
inline constexpr std::size_t kFatalSignalCount = 5;
inline constexpr int kFatalSignals[kFatalSignalCount] = {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE};

// The action of each fatal signal before the door installed its handler, the
// report of the caller, and the mark of the install.  One for each process,
// whatever shared library installs the handlers.
CRUCIBLE_PROCESS_WIDE inline constinit struct ::sigaction g_previous_actions[kFatalSignalCount]{};
CRUCIBLE_PROCESS_WIDE inline constinit ::foundation::core::Atomic<FatalReport> g_fatal_report{nullptr};
CRUCIBLE_PROCESS_WIDE inline constinit ::foundation::core::Atomic<std::uint32_t> g_is_installed{0};

// The size of an alternate stack, or zero before the first read of the
// auxiliary vector.  Two threads can read the vector at one time, and each
// stores the same value.
CRUCIBLE_PROCESS_WIDE inline constinit ::foundation::core::Atomic<std::size_t> g_alternate_stack_bytes{0};

namespace {

// The floor of an alternate stack, and the floor of the smallest stack of a
// signal, which is MINSIGSTKSZ of the C library on x86_64.
constexpr std::size_t kStackFloorBytes = std::size_t{64} * 1024;
constexpr std::uint64_t kMinimumFloorBytes = 2048;

// The largest value of AT_MINSIGSTKSZ that the door takes as it is.  A
// larger value is no stack of a signal on a host of 2026, and it would make
// the product overflow.
constexpr std::uint64_t kMinimumCeilingBytes = std::uint64_t{1} << 30;

// The bytes of an alternate stack (THE ALTERNATE STACK of fixy/os/Signal.h).
[[nodiscard]] std::size_t alternate_stack_bytes() noexcept {
    std::size_t const known = g_alternate_stack_bytes.load_acquire();
    if (known != 0) return known;
    std::uint64_t minimum =
        ::foundation::os::read_auxiliary(::foundation::os::AuxiliaryEntry::minimum_signal_stack).value_or(0);
    if (minimum < kMinimumFloorBytes) minimum = kMinimumFloorBytes;
    if (minimum > kMinimumCeilingBytes) minimum = kMinimumCeilingBytes;
    std::size_t bytes = minimum * 4;
    if (bytes < kStackFloorBytes) bytes = kStackFloorBytes;
    g_alternate_stack_bytes.store_release(bytes);
    return bytes;
}

// The handler of each fatal signal (THE HANDLERS of fixy/os/Signal.h).
void on_fatal_signal(int signal_number, ::siginfo_t* info, void* /*context*/) noexcept {
    FatalReport const report = g_fatal_report.load_acquire();
    if (report != nullptr) report(signal_number);
    for (std::size_t index = 0; index < kFatalSignalCount; ++index) {
        if (kFatalSignals[index] != signal_number) continue;
        static_cast<void>(::sigaction(signal_number, &g_previous_actions[index], nullptr));
    }
    if (info == nullptr || info->si_code <= 0) static_cast<void>(::raise(signal_number));
}

}  // namespace

::foundation::core::Option<::foundation::os::GuardedRegion> install_alternate_stack_(PreviousStack& previous) noexcept {
    std::size_t const needed = alternate_stack_bytes();
    ::stack_t current{};
    if (::sigaltstack(nullptr, &current) != 0) {
        ::foundation::core::fatal("the signal door: sigaltstack did not read the alternate stack of the thread");
    }
    if ((current.ss_flags & SS_DISABLE) == 0 && current.ss_size >= needed) return ::foundation::core::none;
    ::foundation::os::GuardedRegion region = ::foundation::os::map_guarded_region(needed).expect(
        "the signal door: the map of an alternate stack failed, and a failed allocation ends the process");
    ::stack_t next{};
    next.ss_sp = region.data_mut();
    next.ss_size = region.usable_bytes();
    next.ss_flags = 0;
    if (::sigaltstack(&next, nullptr) != 0) {
        ::foundation::core::fatal("the signal door: sigaltstack did not set the alternate stack of the thread");
    }
    previous.first = current.ss_sp;
    previous.bytes = current.ss_size;
    previous.flags = current.ss_flags & SS_DISABLE;
    return ::foundation::core::Option<::foundation::os::GuardedRegion>::some(
        static_cast<::foundation::os::GuardedRegion&&>(region));
}

void restore_alternate_stack_(PreviousStack const& previous) noexcept {
    ::stack_t restored{};
    restored.ss_sp = previous.first;
    restored.ss_size = previous.bytes;
    restored.ss_flags = previous.flags;
    if (::sigaltstack(&restored, nullptr) != 0) {
        ::foundation::core::fatal("the signal door: sigaltstack did not put back the alternate stack of the thread");
    }
}

::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
install_fatal_handlers_(FatalReport report) noexcept {
    if (g_is_installed.exchange_acq_rel(1) != 0) return ::foundation::core::err(::foundation::os::Errno::ebusy);
    g_fatal_report.store_release(report);
    PreviousStack replaced{};
    install_alternate_stack_(replaced).match(
        [](::foundation::os::GuardedRegion region) {
            static_cast<::foundation::os::GuardedRegion&&>(region).keep_until_exit();
        },
        [] {});
    for (std::size_t index = 0; index < kFatalSignalCount; ++index) {
        struct ::sigaction action{};
        action.sa_sigaction = &on_fatal_signal;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        static_cast<void>(::sigemptyset(&action.sa_mask));
        struct ::sigaction& previous = g_previous_actions[index];
        if (::sigaction(kFatalSignals[index], &action, &previous) != 0) continue;
        if ((previous.sa_flags & SA_SIGINFO) == 0 && previous.sa_handler == SIG_IGN) {
            static_cast<void>(::sigaction(kFatalSignals[index], &previous, nullptr));
        }
    }
    return ::foundation::core::Unit{};
}

}  // namespace fixy::signal::detail
