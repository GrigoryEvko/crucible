// The cold code of the watch (fixy/session/Watch.h): the install of the
// hooks, the reports at exit and at a fatal signal, the report of a
// deadlock and the refusal of a wait.
//
// The file includes no system header.  It installs the hooks through the
// doors of fixy/os: the exit hook door, the fork hook of the thread door and
// the signal door.  The gate of mint_watch_hooks holds the Init context of
// the three doors, so the install calls the door functions of their detail
// namespaces.  Each report writes through the Report family of
// foundation/core, whose sink write issues the raw write.  So each report is
// safe in a signal handler, and the exit report and the signal report share
// one printer of an endpoint.

#include <fixy/session/Watch.h>

#include <fixy/os/ExitHooks.h>
#include <fixy/os/MonotonicClock.h>
#include <fixy/os/Signal.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>

#include <cstddef>
#include <cstdint>

namespace fixy::session::watch::detail {

namespace {

namespace core = ::foundation::core;

// The longest path that the report prints: PATH_MAX of Linux, with its
// terminating zero.  The site of a mint is a path that the compiler wrote.
constexpr std::size_t kLongestPath = 4096;

// The count of the characters of the file name TEXT before its terminating
// zero, and at most kLongestPath.  The bound keeps each read inside the
// text, and GCC does not change the loop into a call of strlen of the C
// library.  Complexity: linear in the length of the text.
[[nodiscard]] std::size_t length_of(char const* text) noexcept {
    std::size_t length = 0;
    while (length < kLongestPath && text[length] != '\0')
        ++length;
    return length;
}

// Prints one endpoint: its protocol at the start and the site of its mint.
// The protocol text and the file name come from the records, so their bytes
// go to the sink write as they are.
void print_endpoint(core::TextView label, std::uint32_t index) noexcept {
    endpoint_record const& record = record_at_(index);
    char const* const text = record.protocol_text.load_acquire();
    std::uint32_t const size = record.protocol_size.load_acquire();
    char const* const file = record.site_file.load_acquire();
    core::report(core::Sink::Err, "  {} endpoint {}: ", label, index);
    if (text != nullptr) core::detail::write_sink_(core::Sink::Err, text, size);
    core::report(core::Sink::Err, "\n    minted at ");
    if (file != nullptr && file[0] != '\0') {
        core::detail::write_sink_(core::Sink::Err, file, length_of(file));
        core::report(core::Sink::Err, ":{}\n", record.site_line.load_acquire());
    } else {
        core::report(core::Sink::Err, "<unknown site>\n");
    }
}

// Prints each live record.  Complexity: linear in the records ever claimed.
void print_live_records() noexcept {
    std::uint32_t const bound = claimed_bound();
    for (std::uint32_t index = 1; index <= bound; ++index) {
        if (record_at_(index).is_live.load_acquire() != 0) print_endpoint("live", index);
    }
}

// The hook that std::exit and std::quick_exit run.  A live record is a
// protocol that nothing will finish: print each one, then end the process.
// Complexity: linear in the records ever claimed.
void report_live_at_exit() noexcept {
    std::uint32_t const live = count_live();
    if (live == 0) return;
    g_registry.has_reported_live.store_release(1);
    core::report(core::Sink::Err,
                 "\n"
                 "fixy::session: LIVE PROTOCOL AT EXIT\n"
                 "  {} session(s) still owe a message, and no handle will finish them.  A handle was\n"
                 "  never destroyed: heap storage that was never freed, a released owner, a coroutine\n"
                 "  frame that was never destroyed, or a static object that quick_exit skips.\n",
                 live);
    print_live_records();
    core::fatal("fixy::session: a live protocol at exit ends the process");
}

// The report that the handler of a fatal signal calls first.  It writes
// each live record, once for the process.  Complexity: linear in the
// records ever claimed.
void report_live_on_signal(int signal_number) noexcept {
    if (g_registry.has_reported_live.exchange_acq_rel(1) != 0) return;
    std::uint32_t const live = count_live();
    if (live == 0) return;
    core::report(core::Sink::Err,
                 "\n"
                 "fixy::session: LIVE PROTOCOL AT A FATAL SIGNAL\n"
                 "  signal {} ends the process, and {} session(s) still owe a message that no handle will send.\n",
                 signal_number, live);
    print_live_records();
}

// Forgets every record in a child after fork.  The records belong to
// threads of the parent, which the child does not have.
void forget_records_in_child() noexcept {
    std::uint32_t const bound = claimed_bound();
    for (std::uint32_t index = 1; index <= bound; ++index) {
        record_at_(index).is_live.store_release(0);
    }
}

}  // namespace

void report_deadlock(wait_chain const& chain, std::uint64_t self) noexcept {
    if (g_registry.is_reporting.exchange_acq_rel(1) != 0) {
        // Another thread on the cycle reports it and ends the process.
        for (;;)
            CRUCIBLE_SPIN_PAUSE;
    }
    core::report(core::Sink::Err,
                 "\n"
                 "fixy::session: DEADLOCK ACROSS SESSIONS\n"
                 "  {} thread(s) wait in a cycle, and the cycle held for two checks {} ms apart.\n"
                 "  Each thread waits on an endpoint whose peer the next thread holds.  An endpoint\n"
                 "  escaped the fork that made its channel, so threads and channels no longer form\n"
                 "  a forest.\n",
                 chain.count, confirm_after_ns / 1000000);
    for (std::size_t index = 0; index < chain.count; ++index) {
        chain_link const& link = chain.links[index];
        std::uint64_t const waiter = index == 0 ? self : chain.links[index - 1].holder;
        core::report(core::Sink::Err, "  thread slot {} waits on:\n", index_of(waiter));
        print_endpoint("waited", link.endpoint);
        core::report(core::Sink::Err, "  and thread slot {} holds its peer:\n", index_of(link.holder));
        print_endpoint("peer", link.peer);
    }
    core::fatal("fixy::session: a deadlock across sessions ends the process");
}

void refuse_wait(wait_refusal refusal, std::uint32_t waited, std::uint32_t held) noexcept {
    core::TextView const tag = refusal == wait_refusal::held_peer ? core::TextView{"Wait_On_Held_Peer"}
                                                                  : core::TextView{"Wait_Breaks_Priority_Order"};
    core::report(core::Sink::Err,
                 "\n"
                 "fixy::session: diagnostic [{}]\n"
                 "  A thread opened a wait that could close a cycle of waits across sessions, so the watch\n"
                 "  refused it before the thread waited.  When a thread waits on an endpoint of priority p,\n"
                 "  each other endpoint it holds must have a priority below p, and it must not hold the peer\n"
                 "  of the endpoint it waits on.  A Resource states the priority of its session with a\n"
                 "  static member session_priority, and a session with none has priority 0, the lowest.\n",
                 tag);
    core::report(core::Sink::Err, "  the thread waits on:\n");
    print_endpoint("waited", waited);
    core::report(core::Sink::Err, "  priority {}\n  and holds:\n", record_at_(waited).priority.load_acquire());
    print_endpoint("held", held);
    core::report(core::Sink::Err, "  priority {}\n", record_at_(held).priority.load_acquire());
    core::fatal("fixy::session: a wait that could close a cycle of waits ends the process");
}

std::uint64_t monotonic_ns() noexcept { return ::fixy::time::detail::monotonic_nanos_(); }

WatchHooks install_hooks_() noexcept {
    if (g_registry.hooks_installed.exchange_acq_rel(1) == 0) {
        ::fixy::exit::detail::run_at_exit_(&report_live_at_exit)
            .expect("fixy::session: the C library refused the exit hook of the watch");
        ::fixy::thread::detail::run_in_fork_child_(&forget_records_in_child)
            .expect("fixy::session: the C library refused the fork hook of the watch");
        ::fixy::signal::detail::install_fatal_handlers_(&report_live_on_signal)
            .expect("fixy::session: the signal door refused the handlers, because a different caller installed "
                    "them first");
    }
    return WatchHooks{};
}

}  // namespace fixy::session::watch::detail
