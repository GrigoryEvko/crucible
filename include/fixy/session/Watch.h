#pragma once

// The runtime watch over live session endpoints.
//
// A handle under a checking policy is linear while it lives, but two
// failures stay outside the type system:
//
//   - A handle that is never destroyed leaks its protocol.  Heap storage
//     that is never freed, a released owner, a coroutine frame that is
//     never destroyed and a static object skipped by quick_exit all hold
//     a live endpoint that no destructor reaches.
//   - Endpoints that escape through shared memory can form a cycle of
//     waits across sessions.  The forest condition of LinearActris
//     (Jacobs, Hinrichsen and Krebbers, POPL 2024) then does not hold,
//     and the threads deadlock.
//
// The watch reports the first at exit and at each fatal signal.  It
// prevents the second: a wait that could close a cycle is refused before
// the thread waits, by a priority order on sessions.  A cycle detector
// stays as a second line.
//
// ── The endpoint table ──────────────────────────────────────────────
//
// Each session that a mint starts under a checking policy claims one
// record.  The record holds the protocol at the start, the site of the
// mint, the peer endpoint when the mint made a channel, and the thread
// that holds the handle now.  The session releases the record when it
// reaches End, detaches, or cancels.  A handle carries the index of its
// record in the padding of its policy, so a handle keeps its size.
//
// ── The report at exit ──────────────────────────────────────────────
//
// std::exit and std::quick_exit run a hook that walks the table.  A live
// record is a protocol that nothing will finish, so the hook prints each
// one and aborts.
//
// std::abort and the fatal signals SIGSEGV, SIGBUS, SIGILL and SIGFPE run
// no exit hook, so the watch also installs a handler for each of them
// through sigaction.  The handler writes the live records with write(2),
// which is async-signal-safe, and then gives the signal to the action
// that was installed before it: a signal that a process sent comes again
// after the handler returns, and a fault comes again when the faulting
// instruction runs again.  A signal whose action was to ignore it keeps
// that action.  std::_Exit and SIGKILL run no code at all, so the watch
// sees nothing there.
//
// ── Every wait goes through the watch ───────────────────────────────
//
// A transport of fixy/session/Handle.h never waits in secret.  A read
// either polls, and returns no value while no message is there, or it
// takes a wait_scope that the handle opened before the call.  A write
// either tries, and returns false while it cannot take the value, or it
// takes a wait_scope.  So each wait of a thread on an endpoint opens a
// wait_scope first, and the scope publishes the endpoint that the thread
// waits on.
//
// ── The priority order ──────────────────────────────────────────────
//
// Each session has a priority, which its Resource states as a static
// member session_priority, and which is 0 when the Resource states none.
// The two ends of a channel have one priority.  When a thread opens a
// wait on an endpoint of priority p, every other live endpoint that the
// thread holds must have a priority below p, and the thread must not
// hold the peer of the endpoint it waits on.  A wait that breaks the
// order is refused before the thread waits.
//
// This is the lock order of Kobayashi's deadlock-free processes, which
// Dardha and Gay (Prioritised GV, 2022) and van den Heuvel and Pérez
// (APCP, LMCS 2024) give to sessions.  In a cycle of waits, each thread
// waits on an endpoint and holds the peer of the endpoint that the thread
// before it waits on.  The order would need each priority on the cycle to
// be above the one before it, which no cycle allows.  Priority 0 is the
// lowest, so a thread that waits at priority 0 holds no other endpoint:
// the forest condition of LinearActris, checked when the thread waits.
//
// The check runs when a thread starts to wait, and not at each step.  A
// step that finds its message there, or room for its value, does not
// wait, and a thread that does not wait is on no cycle of waits.  So a
// program that breaks the order is refused on the run where it waits,
// and one thread can drive the two ends of a test channel in an order
// that never waits.
//
// One priority for each session cannot order a thread that waits on
// session A while it holds B, and later waits on B while it holds A.  The
// priorities of Dardha and Gay are on each action and grow along the
// protocol, so they can.  This watch keeps one number for each session,
// and such a thread is refused at its second wait.
//
// ── The deadlock detector ───────────────────────────────────────────
//
// A polling wait also follows the wait-for chain: the peer of the
// endpoint, the thread that holds the peer, the endpoint that thread
// waits on, and so on.  A chain that comes back to the waiting thread is
// a cycle.  When the same cycle, with the same wait of each thread,
// holds at two checks confirm_after apart, the scope prints the cycle
// and aborts.  A step that does not wait never touches the watch.
//
// ── The holder of an endpoint ───────────────────────────────────────
//
// The holder is the thread that last stepped or moved the handle.  A
// channel mint claims the records of its two ends on the thread that
// forks, and names no holder: each end's thread becomes the holder when
// it opens its end.
//
// Limits, stated rather than implied:
//
//   - A handle that reaches a thread without a move or a step on that
//     thread, inside a lambda capture or through a pointer, counts for
//     the thread that moved or stepped it last, until the new thread
//     steps it.  A wait of the new thread in that window is not checked
//     against that endpoint.  No C++ event marks the first moment that a
//     thread can reach an object, so no check can run there.
//   - The table has a fixed capacity.  A session minted when it is full
//     is not tracked, and a thread with no free slot is not either.
//
// Every field is an atomic, so a reader on another thread never races a
// writer.  Every "none" is zero, so the tables are in .bss and cost no
// page until a session touches them.

#include <foundation/Platform.h>

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <source_location>
#include <string_view>
#include <utility>

namespace fixy::session::watch {

// The index of an endpoint record, plus one.  Zero is no record, so a
// handle that no mint tracks carries the zero value.
enum class endpoint_id : std::uint32_t { none = 0 };

// The priority of a session in the order that the watch keeps.  A
// Resource states it as a static constexpr member session_priority of
// this type.  lowest is the priority of a Resource that states none.
enum class priority : std::uint32_t { lowest = 0 };

inline constexpr std::uint32_t endpoint_capacity = 4096;
inline constexpr std::uint32_t thread_capacity = 1024;

// A wait that lasts this long starts to trace its chain.
inline constexpr std::chrono::milliseconds suspect_after{50};

// The same cycle must hold at two traces this far apart.
inline constexpr std::chrono::milliseconds confirm_after{50};

// The longest chain the detector follows.
inline constexpr std::size_t max_chain = 64;

namespace detail {

inline constexpr std::uint64_t low_half = 0xFFFF'FFFFu;

// A thread that found no free slot records this owner token.  The
// detector reads it as an unknown holder.
inline constexpr std::uint64_t untracked_owner = ~std::uint64_t{0};

// Packs a one-based index with a generation.  Zero is none.
[[nodiscard]] constexpr std::uint64_t pack(std::uint32_t index, std::uint32_t generation) noexcept {
    return (std::uint64_t{generation} << 32) | index;
}

[[nodiscard]] constexpr std::uint32_t index_of(std::uint64_t packed) noexcept {
    return static_cast<std::uint32_t>(packed & low_half);
}

[[nodiscard]] constexpr std::uint32_t generation_of(std::uint64_t packed) noexcept {
    return static_cast<std::uint32_t>(packed >> 32);
}

// One live endpoint.  A record changes generation at each claim, so a
// stale reference to an old session never matches a new one.
struct alignas(64) endpoint_record {
    std::atomic<std::uint32_t> is_live{0};
    std::atomic<std::uint32_t> generation{0};
    // The next free record, one-based, while the record is free.
    std::atomic<std::uint32_t> next_free{0};
    std::atomic<std::uint32_t> protocol_size{0};
    // The priority of the session, from its Resource.  0 is the lowest.
    std::atomic<std::uint32_t> priority{0};
    // The peer endpoint, packed with its generation.
    std::atomic<std::uint64_t> peer{0};
    // The thread that holds the handle, packed with its slot generation.
    std::atomic<std::uint64_t> owner{0};
    std::atomic<const char*> protocol_text{nullptr};
    std::atomic<std::source_location> site{};
};

// One thread that has touched the watch.
struct alignas(64) thread_record {
    std::atomic<std::uint32_t> is_taken{0};
    std::atomic<std::uint32_t> generation{0};
    // The endpoint the thread waits on, one-based, or zero.
    std::atomic<std::uint32_t> blocked_on{0};
    // Counts the waits of the thread, so that a detector can tell one
    // wait from the next.
    std::atomic<std::uint64_t> wait_epoch{0};
};

struct registry {
    endpoint_record endpoints[endpoint_capacity]{};
    thread_record threads[thread_capacity]{};
    // The free list of released records: a one-based index packed with a
    // tag that changes at each push and pop, which removes the ABA case.
    std::atomic<std::uint64_t> free_head{0};
    // The records never claimed start here, zero-based.
    std::atomic<std::uint32_t> next_unused{0};
    std::atomic<std::uint32_t> hooks_installed{0};
    // Set by the first thread that reports a deadlock.  Every thread on a
    // cycle detects it, and one report is enough.
    std::atomic<std::uint32_t> is_reporting{0};
    // Set by the first report of the live records, so that the abort that
    // ends the exit report does not print them again.
    std::atomic<std::uint32_t> has_reported_live{0};
};

inline constinit registry g_registry{};

// The signals that end a process with no exit hook, and the action that
// each had before the watch installed its handler.
inline constexpr int fatal_signals[] = {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE};

struct signal_chain {
    struct sigaction previous[std::size(fatal_signals)]{};
};

inline constinit signal_chain g_signal_chain{};

// The owner token of this thread, or zero before its first use.
inline constinit thread_local std::uint64_t tls_owner_token = 0;

// The holder stamp of this thread: its one-based slot index, with the
// low bits of the slot generation above it.  Zero until the thread
// claims a slot, and for a thread that found no free slot.
inline constexpr unsigned stamp_index_bits = 11;
inline constinit thread_local std::uint16_t tls_holder_stamp = 0;

static_assert(thread_capacity < (1u << stamp_index_bits),
              "the one-based slot index must fit the low bits of a holder stamp");

// The records that this thread took as holder, each a one-based index
// packed with its generation.  An entry is only a candidate: the order
// check keeps the entries whose record is live, has that generation and
// names this thread as its owner.  Only this thread reads or writes the
// list.  When every entry is taken by a record this thread still holds,
// the check reads the whole table instead.
inline constexpr std::size_t held_capacity = 16;
inline constinit thread_local std::uint64_t tls_held[held_capacity]{};
inline constinit thread_local bool tls_held_overflowed = false;

[[nodiscard]] constexpr std::uint16_t holder_stamp_of(std::uint32_t slot, std::uint32_t generation) noexcept {
    return static_cast<std::uint16_t>(slot | (generation << stamp_index_bits));
}

[[nodiscard]] inline endpoint_record& record_at_(std::uint32_t one_based) noexcept {
    return g_registry.endpoints[one_based - 1];
}

[[nodiscard]] inline thread_record& thread_at_(std::uint32_t one_based) noexcept {
    return g_registry.threads[one_based - 1];
}

// Frees the slot of a thread when the thread ends.  Only the cold claim
// below names it, so no hot path pays for its destructor.
struct slot_releaser {
    std::uint32_t slot = 0;

    slot_releaser() noexcept = default;
    slot_releaser(const slot_releaser&) = delete("one releaser owns the slot of its thread");
    slot_releaser& operator=(const slot_releaser&) = delete("one releaser owns the slot of its thread");

    /// Publishes that the thread waits on nothing, bumps the slot
    /// generation so that no stale owner token matches it, and frees it.
    ~slot_releaser() {
        if (slot == 0) return;
        thread_record& me = thread_at_(slot);
        me.blocked_on.store(0, std::memory_order_release);
        me.generation.fetch_add(1, std::memory_order_acq_rel);
        me.is_taken.store(0, std::memory_order_release);
        tls_owner_token = 0;
        tls_holder_stamp = 0;
    }
};

/// Claims a thread slot for the calling thread and returns its owner
/// token.  A full table gives the untracked token, and the thread keeps
/// it.  Complexity: linear in thread_capacity, once for each thread.
[[gnu::cold, gnu::noinline]] inline std::uint64_t claim_thread_slot() noexcept {
    for (std::uint32_t slot = 1; slot <= thread_capacity; ++slot) {
        thread_record& candidate = thread_at_(slot);
        std::uint32_t expected = 0;
        if (candidate.is_taken.load(std::memory_order_acquire) != 0) continue;
        if (!candidate.is_taken.compare_exchange_strong(expected, 1, std::memory_order_acq_rel,
                                                        std::memory_order_acquire)) {
            continue;
        }
        thread_local slot_releaser releaser;
        releaser.slot = slot;
        const std::uint32_t generation = candidate.generation.load(std::memory_order_acquire);
        tls_owner_token = pack(slot, generation);
        tls_holder_stamp = holder_stamp_of(slot, generation);
        return tls_owner_token;
    }
    tls_owner_token = untracked_owner;
    return tls_owner_token;
}

/// The owner token of the calling thread.  The first call claims a slot.
[[nodiscard]] inline std::uint64_t owner_token() noexcept {
    const std::uint64_t token = tls_owner_token;
    if (token != 0) [[likely]]
        return token;
    return claim_thread_slot();
}

/// Pops a released record, one-based, or returns zero.
[[nodiscard]] inline std::uint32_t pop_free() noexcept {
    std::uint64_t head = g_registry.free_head.load(std::memory_order_acquire);
    for (;;) {
        const std::uint32_t top = index_of(head);
        if (top == 0) return 0;
        const std::uint32_t next = record_at_(top).next_free.load(std::memory_order_acquire);
        const std::uint64_t replacement = pack(next, generation_of(head) + 1);
        if (g_registry.free_head.compare_exchange_weak(head, replacement, std::memory_order_acq_rel,
                                                       std::memory_order_acquire)) {
            return top;
        }
    }
}

/// Pushes a record, one-based, onto the free list.
inline void push_free(std::uint32_t index) noexcept {
    std::uint64_t head = g_registry.free_head.load(std::memory_order_acquire);
    for (;;) {
        record_at_(index).next_free.store(index_of(head), std::memory_order_release);
        const std::uint64_t replacement = pack(index, generation_of(head) + 1);
        if (g_registry.free_head.compare_exchange_weak(head, replacement, std::memory_order_acq_rel,
                                                       std::memory_order_acquire)) {
            return;
        }
    }
}

/// Prints one endpoint: its protocol at the start and the site of its mint.
inline void print_endpoint(const char* label, std::uint32_t index) noexcept {
    const endpoint_record& record = record_at_(index);
    const char* text = record.protocol_text.load(std::memory_order_acquire);
    const std::uint32_t size = record.protocol_size.load(std::memory_order_acquire);
    const std::source_location site = record.site.load(std::memory_order_acquire);
    const char* file = site.file_name();
    const bool has_site = file != nullptr && file[0] != '\0';
    std::fprintf(stderr, "  %s endpoint %u: %.*s\n    minted at %s:%u\n", label, index,
                 static_cast<int>(text == nullptr ? 0 : size), text == nullptr ? "" : text,
                 has_site ? file : "<unknown site>", has_site ? site.line() : std::uint_least32_t{0});
}

/// The number of live records.  Complexity: linear in the records ever
/// claimed.
[[nodiscard]] inline std::uint32_t count_live() noexcept {
    const std::uint32_t claimed = g_registry.next_unused.load(std::memory_order_acquire);
    const std::uint32_t bound = claimed < endpoint_capacity ? claimed : endpoint_capacity;
    std::uint32_t live = 0;
    for (std::uint32_t index = 1; index <= bound; ++index) {
        if (record_at_(index).is_live.load(std::memory_order_acquire) != 0) ++live;
    }
    return live;
}

/// The hook that std::exit and std::quick_exit run.  A live record is a
/// protocol that nothing will finish: print each one, then abort.
/// Complexity: linear in the records ever claimed.
inline void report_live_at_exit() noexcept {
    const std::uint32_t live = count_live();
    if (live == 0) return;
    g_registry.has_reported_live.store(1, std::memory_order_release);
    std::fprintf(stderr,
                 "\n"
                 "fixy::session: LIVE PROTOCOL AT EXIT\n"
                 "  %u session(s) still owe a message, and no handle will finish them.  A handle was\n"
                 "  never destroyed: heap storage that was never freed, a released owner, a coroutine\n"
                 "  frame that was never destroyed, or a static object that quick_exit skips.\n",
                 live);
    const std::uint32_t claimed = g_registry.next_unused.load(std::memory_order_acquire);
    const std::uint32_t bound = claimed < endpoint_capacity ? claimed : endpoint_capacity;
    for (std::uint32_t index = 1; index <= bound; ++index) {
        if (record_at_(index).is_live.load(std::memory_order_acquire) != 0) print_endpoint("live", index);
    }
    std::abort();
}

// ── The report at a fatal signal ─────────────────────────────────────
//
// A signal handler may call only async-signal-safe functions.  These
// helpers write with write(2) and format a number by hand, and the
// records are read through lock-free atomics.

/// Writes `text` to standard error.  A failed write ends the output: the
/// process is going down, and nothing can report the failure.
inline void write_raw(std::string_view text) noexcept {
    const char* cursor = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        const ::ssize_t written = ::write(STDERR_FILENO, cursor, remaining);  // SYSCALL-CAP-OK: the report at a fatal signal writes with write(2), which is async-signal-safe
        if (written <= 0) return;
        cursor += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

/// Writes a number in decimal.
inline void write_number(std::uint64_t value) noexcept {
    char digits[20];
    std::size_t first = sizeof digits;
    do {
        digits[--first] = static_cast<char>('0' + static_cast<int>(value % 10));
        value /= 10;
    } while (value != 0);
    write_raw(std::string_view{digits + first, sizeof digits - first});
}

/// Writes each live record, once for the process.  Complexity: linear in
/// the records ever claimed.
inline void report_live_on_signal(int signal_number) noexcept {
    if (g_registry.has_reported_live.exchange(1, std::memory_order_acq_rel) != 0) return;
    const std::uint32_t live = count_live();
    if (live == 0) return;
    write_raw("\nfixy::session: LIVE PROTOCOL AT A FATAL SIGNAL\n  signal ");
    write_number(static_cast<std::uint64_t>(signal_number));
    write_raw(" ends the process, and ");
    write_number(live);
    write_raw(" session(s) still owe a message that no handle will send.\n");
    const std::uint32_t claimed = g_registry.next_unused.load(std::memory_order_acquire);
    const std::uint32_t bound = claimed < endpoint_capacity ? claimed : endpoint_capacity;
    for (std::uint32_t index = 1; index <= bound; ++index) {
        const endpoint_record& record = record_at_(index);
        if (record.is_live.load(std::memory_order_acquire) == 0) continue;
        const char* text = record.protocol_text.load(std::memory_order_acquire);
        const std::uint32_t size = record.protocol_size.load(std::memory_order_acquire);
        const std::source_location site = record.site.load(std::memory_order_acquire);
        const char* file = site.file_name();
        write_raw("  live endpoint ");
        write_number(index);
        write_raw(": ");
        if (text != nullptr) write_raw(std::string_view{text, size});
        write_raw("\n    minted at ");
        if (file != nullptr && file[0] != '\0') {
            write_raw(std::string_view{file});
            write_raw(":");
            write_number(site.line());
        } else {
            write_raw("<unknown site>");
        }
        write_raw("\n");
    }
}

/// The handler of each fatal signal.  It reports the live records, puts
/// back the action that was there before, and lets that action run: a
/// signal that a process sent (si_code at or below zero) is raised again,
/// and it arrives when the handler returns, because the handler blocks it
/// while it runs.  A fault is not raised again: the faulting instruction
/// runs again after the return and faults into the action put back.
inline void on_fatal_signal(int signal_number, ::siginfo_t* info, void* /*context*/) noexcept {
    report_live_on_signal(signal_number);
    for (std::size_t index = 0; index < std::size(fatal_signals); ++index) {
        if (fatal_signals[index] != signal_number) continue;
        static_cast<void>(::sigaction(signal_number, &g_signal_chain.previous[index], nullptr));  // SYSCALL-CAP-OK: puts back the action that the watch replaced
    }
    if (info == nullptr || info->si_code <= 0) static_cast<void>(::raise(signal_number));
}

/// Installs the handler of each fatal signal, and keeps the action that it
/// replaces.  A signal that the process ignores keeps its action.
inline void install_signal_handlers() noexcept {
    for (std::size_t index = 0; index < std::size(fatal_signals); ++index) {
        struct sigaction action{};
        action.sa_sigaction = &on_fatal_signal;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        ::sigemptyset(&action.sa_mask);
        struct sigaction& previous = g_signal_chain.previous[index];
        if (::sigaction(fatal_signals[index], &action, &previous) != 0) continue;  // SYSCALL-CAP-OK: installs the report at a fatal signal, once, from the first claim
        if (!(previous.sa_flags & SA_SIGINFO) && previous.sa_handler == SIG_IGN) {
            static_cast<void>(::sigaction(fatal_signals[index], &previous, nullptr));  // SYSCALL-CAP-OK: a signal that the process ignored stays ignored
        }
    }
}

/// Forgets every record in a child after fork.  The records belong to
/// threads of the parent, which the child does not have.
inline void forget_records_in_child() noexcept {
    const std::uint32_t claimed = g_registry.next_unused.load(std::memory_order_acquire);
    const std::uint32_t bound = claimed < endpoint_capacity ? claimed : endpoint_capacity;
    for (std::uint32_t index = 1; index <= bound; ++index) {
        record_at_(index).is_live.store(0, std::memory_order_release);
    }
}

/// Installs the exit hooks once for the process.
[[gnu::cold, gnu::noinline]] inline void install_exit_hooks() noexcept {
    std::uint32_t expected = 0;
    if (!g_registry.hooks_installed.compare_exchange_strong(expected, 1, std::memory_order_acq_rel,
                                                            std::memory_order_acquire)) {
        return;
    }
    static_cast<void>(std::atexit(&report_live_at_exit));
    static_cast<void>(std::at_quick_exit(&report_live_at_exit));
    static_cast<void>(::pthread_atfork(nullptr, nullptr, &forget_records_in_child));
    install_signal_handlers();
}

// One link of a wait-for chain: a thread waits on an endpoint, and the
// peer of that endpoint is held by the next thread.
struct chain_link {
    std::uint32_t endpoint = 0;
    std::uint32_t endpoint_generation = 0;
    std::uint32_t peer = 0;
    std::uint64_t holder = 0;
    std::uint64_t holder_epoch = 0;

    friend constexpr bool operator==(const chain_link&, const chain_link&) noexcept = default;
};

struct wait_chain {
    chain_link links[max_chain]{};
    std::size_t count = 0;
    bool is_cycle = false;

    /// Two chains are the same when they are cycles over the same links.
    [[nodiscard]] bool same_cycle_as(const wait_chain& other) const noexcept {
        if (!is_cycle || !other.is_cycle || count != other.count) return false;
        for (std::size_t index = 0; index < count; ++index) {
            if (!(links[index] == other.links[index])) return false;
        }
        return true;
    }
};

/// Follows the wait-for chain from the calling thread, which waits on
/// `start`.  The result is a cycle only when the chain comes back to the
/// caller.  Complexity: linear in max_chain.
[[nodiscard]] inline wait_chain trace_chain(std::uint64_t self, std::uint32_t start) noexcept {
    wait_chain chain{};
    std::uint32_t current = start;
    for (std::size_t step = 0; step < max_chain; ++step) {
        const endpoint_record& waiting = record_at_(current);
        if (waiting.is_live.load(std::memory_order_acquire) == 0) return chain;
        const std::uint64_t peer = waiting.peer.load(std::memory_order_acquire);
        if (peer == 0) return chain;
        const endpoint_record& held = record_at_(index_of(peer));
        if (held.is_live.load(std::memory_order_acquire) == 0
            || held.generation.load(std::memory_order_acquire) != generation_of(peer)) {
            return chain;
        }
        const std::uint64_t holder = held.owner.load(std::memory_order_acquire);
        if (holder == 0 || holder == untracked_owner) return chain;
        const thread_record& thread = thread_at_(index_of(holder));
        if (thread.is_taken.load(std::memory_order_acquire) == 0
            || thread.generation.load(std::memory_order_acquire) != generation_of(holder)) {
            return chain;
        }
        chain.links[chain.count++] =
            chain_link{current, waiting.generation.load(std::memory_order_acquire), index_of(peer), holder,
                       thread.wait_epoch.load(std::memory_order_acquire)};
        if (holder == self) {
            chain.is_cycle = true;
            return chain;
        }
        const std::uint32_t next = thread.blocked_on.load(std::memory_order_acquire);
        if (next == 0) return chain;
        for (std::size_t seen = 0; seen + 1 < chain.count; ++seen) {
            // A cycle that does not pass through the caller belongs to the
            // threads on it, and each of them detects it.
            if (chain.links[seen].holder == holder) return chain;
        }
        current = next;
    }
    return chain;
}

/// Prints a confirmed cycle that starts at the thread `self`, and aborts.
[[noreturn, gnu::cold, gnu::noinline]] inline void report_deadlock(const wait_chain& chain,
                                                                   std::uint64_t self) noexcept {
    if (g_registry.is_reporting.exchange(1, std::memory_order_acq_rel) != 0) {
        // Another thread on the cycle reports it and aborts the process.
        for (;;) CRUCIBLE_SPIN_PAUSE;
    }
    std::fprintf(stderr,
                 "\n"
                 "fixy::session: DEADLOCK ACROSS SESSIONS\n"
                 "  %zu thread(s) wait in a cycle, and the cycle held for two checks %lld ms apart.\n"
                 "  Each thread waits on an endpoint whose peer the next thread holds.  An endpoint\n"
                 "  escaped the fork that made its channel, so threads and channels no longer form\n"
                 "  a forest.\n",
                 chain.count, static_cast<long long>(confirm_after.count()));
    for (std::size_t index = 0; index < chain.count; ++index) {
        const chain_link& link = chain.links[index];
        const std::uint64_t waiter = index == 0 ? self : chain.links[index - 1].holder;
        std::fprintf(stderr, "  thread slot %u waits on:\n", index_of(waiter));
        print_endpoint("waited", link.endpoint);
        std::fprintf(stderr, "  and thread slot %u holds its peer:\n", index_of(link.holder));
        print_endpoint("peer", link.peer);
    }
    std::abort();
}

// ── The priority order ───────────────────────────────────────────────

/// True when the record at `index` is live, has `generation`, and has
/// `self` as its owner.
[[nodiscard]] inline bool holds_record(std::uint64_t self, std::uint32_t index, std::uint32_t generation) noexcept {
    const endpoint_record& record = record_at_(index);
    return record.is_live.load(std::memory_order_acquire) != 0
        && record.generation.load(std::memory_order_acquire) == generation
        && record.owner.load(std::memory_order_acquire) == self;
}

/// Adds a record that this thread just took as holder to its list.  It
/// takes an entry whose record this thread no longer holds, and marks the
/// list overflowed when every entry is still held.  Complexity: linear in
/// held_capacity, on the cold path of a claim or a change of holder.
inline void remember_held(std::uint64_t self, std::uint32_t index) noexcept {
    const std::uint64_t entry = pack(index, record_at_(index).generation.load(std::memory_order_acquire));
    std::size_t free_slot = held_capacity;
    for (std::size_t slot = 0; slot < held_capacity; ++slot) {
        const std::uint64_t held = tls_held[slot];
        if (held == entry) return;
        if (free_slot == held_capacity && (held == 0 || !holds_record(self, index_of(held), generation_of(held)))) {
            free_slot = slot;
        }
    }
    if (free_slot == held_capacity) {
        tls_held_overflowed = true;
        return;
    }
    tls_held[free_slot] = entry;
}

[[noreturn, gnu::cold, gnu::noinline]] inline void refuse_wait(const char* what, std::uint32_t waited,
                                                               std::uint32_t held) noexcept {
    std::fprintf(stderr,
                 "\n"
                 "fixy::session: diagnostic [%s]\n"
                 "  A thread opened a wait that could close a cycle of waits across sessions, so the watch\n"
                 "  refused it before the thread waited.  When a thread waits on an endpoint of priority p,\n"
                 "  each other endpoint it holds must have a priority below p, and it must not hold the peer\n"
                 "  of the endpoint it waits on.  A Resource states the priority of its session with a\n"
                 "  static member session_priority, and a session with none has priority 0, the lowest.\n",
                 what);
    std::fprintf(stderr, "  the thread waits on:\n");
    print_endpoint("waited", waited);
    std::fprintf(stderr, "  priority %u\n  and holds:\n",
                 record_at_(waited).priority.load(std::memory_order_acquire));
    print_endpoint("held", held);
    std::fprintf(stderr, "  priority %u\n", record_at_(held).priority.load(std::memory_order_acquire));
    std::abort();
}

/// Refuses a wait of the calling thread on `waited` that breaks the
/// priority order: the thread holds the peer of `waited`, or it holds
/// another live endpoint whose priority is not below the priority of
/// `waited`.  Complexity: linear in held_capacity, or in the records ever
/// claimed after the list overflowed.
inline void check_wait_order(std::uint64_t self, std::uint32_t waited) noexcept {
    const endpoint_record& waited_record = record_at_(waited);
    const std::uint32_t priority = waited_record.priority.load(std::memory_order_acquire);
    const std::uint64_t peer = waited_record.peer.load(std::memory_order_acquire);
    const auto check_one = [&](std::uint32_t index, std::uint32_t generation) noexcept {
        if (index == waited) return;
        if (peer != 0 && index == index_of(peer) && generation == generation_of(peer)) {
            refuse_wait("Wait_On_Held_Peer", waited, index);
        }
        if (record_at_(index).priority.load(std::memory_order_acquire) >= priority) {
            refuse_wait("Wait_Breaks_Priority_Order", waited, index);
        }
    };
    if (!tls_held_overflowed) {
        for (const std::uint64_t held : tls_held) {
            if (held == 0 || !holds_record(self, index_of(held), generation_of(held))) continue;
            check_one(index_of(held), generation_of(held));
        }
        return;
    }
    const std::uint32_t claimed = g_registry.next_unused.load(std::memory_order_acquire);
    const std::uint32_t bound = claimed < endpoint_capacity ? claimed : endpoint_capacity;
    for (std::uint32_t index = 1; index <= bound; ++index) {
        const endpoint_record& record = record_at_(index);
        if (record.is_live.load(std::memory_order_acquire) == 0) continue;
        if (record.owner.load(std::memory_order_acquire) != self) continue;
        check_one(index, record.generation.load(std::memory_order_acquire));
    }
}

}  // namespace detail

// Whether a claim names the calling thread as the holder.  A mint that
// opens the session on the calling thread names it.  A channel mint
// claims on the thread that forks, and each end's own thread becomes the
// holder when it opens its end.
enum class holder_on_claim : std::uint8_t { calling_thread, none_yet };

/// Claims a record for a session that starts at `protocol`, minted at
/// `site`, with the priority of its Resource.  A full table gives
/// endpoint_id::none, and the session is then not tracked.  The first
/// claim installs the exit hooks.
[[nodiscard]] inline endpoint_id claim(std::string_view protocol, std::source_location site, priority order,
                                       holder_on_claim holder) noexcept {
    if (detail::g_registry.hooks_installed.load(std::memory_order_acquire) == 0) [[unlikely]] {
        detail::install_exit_hooks();
    }
    std::uint32_t index = detail::pop_free();
    if (index == 0) {
        const std::uint32_t fresh = detail::g_registry.next_unused.fetch_add(1, std::memory_order_acq_rel);
        if (fresh >= endpoint_capacity) return endpoint_id::none;
        index = fresh + 1;
    }
    detail::endpoint_record& record = detail::record_at_(index);
    // The claimant is the only writer of a record's generation, so a load
    // and a store suffice.  A concurrent trace only reads it.
    record.generation.store(record.generation.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    record.peer.store(0, std::memory_order_release);
    record.protocol_text.store(protocol.data(), std::memory_order_release);
    record.protocol_size.store(static_cast<std::uint32_t>(protocol.size()), std::memory_order_release);
    record.site.store(site, std::memory_order_release);
    record.priority.store(std::to_underlying(order), std::memory_order_release);
    if (holder == holder_on_claim::calling_thread) {
        const std::uint64_t self = detail::owner_token();
        record.owner.store(self, std::memory_order_release);
        record.is_live.store(1, std::memory_order_release);
        if (self != detail::untracked_owner) detail::remember_held(self, index);
    } else {
        record.owner.store(0, std::memory_order_release);
        record.is_live.store(1, std::memory_order_release);
    }
    return static_cast<endpoint_id>(index);
}

/// Makes `first` and `second` the two ends of one channel.
inline void link(endpoint_id first, endpoint_id second) noexcept {
    if (first == endpoint_id::none || second == endpoint_id::none) return;
    const auto first_index = static_cast<std::uint32_t>(first);
    const auto second_index = static_cast<std::uint32_t>(second);
    detail::record_at_(first_index)
        .peer.store(detail::pack(second_index,
                                 detail::record_at_(second_index).generation.load(std::memory_order_acquire)),
                    std::memory_order_release);
    detail::record_at_(second_index)
        .peer.store(detail::pack(first_index,
                                 detail::record_at_(first_index).generation.load(std::memory_order_acquire)),
                    std::memory_order_release);
}

/// Ends the session of `endpoint`: it reached End, detached or cancelled.
inline void release(endpoint_id endpoint) noexcept {
    if (endpoint == endpoint_id::none) return;
    const auto index = static_cast<std::uint32_t>(endpoint);
    detail::endpoint_record& record = detail::record_at_(index);
    record.is_live.store(0, std::memory_order_release);
    record.owner.store(0, std::memory_order_release);
    record.peer.store(0, std::memory_order_release);
    detail::push_free(index);
}

// The slot of a thread in the thread table, one-based, in the low bits,
// and the low bits of the slot generation above them.  Zero is a thread
// with no slot.  A handle keeps the value of the thread that holds it, so
// a move or a step compares two small integers and touches no shared line
// unless the handle crossed threads.  The generation bits tell a thread
// from an earlier thread that had the same slot, until the slot has 32
// more owners.
enum class thread_slot : std::uint16_t { none = 0 };

/// The slot of the calling thread, or none before the thread first
/// touches the watch.  It reads one thread-local value.
[[nodiscard]] inline thread_slot current_thread_slot() noexcept {
    return static_cast<thread_slot>(detail::tls_holder_stamp);
}

// What a handle keeps of the watch: the session's record and the thread
// that the watch names as its holder.
struct session_ref {
    endpoint_id endpoint = endpoint_id::none;
    thread_slot holder = thread_slot::none;
};

/// Records that the calling thread holds the handle of `endpoint` now,
/// and returns the slot of the thread.  A handle calls it when it finds
/// itself on a thread other than the one it last saw.
[[gnu::cold, gnu::noinline]] inline thread_slot note_holder(endpoint_id endpoint) noexcept {
    const std::uint64_t token = detail::owner_token();
    if (endpoint != endpoint_id::none) {
        const auto index = static_cast<std::uint32_t>(endpoint);
        detail::record_at_(index).owner.store(token, std::memory_order_release);
        if (token != detail::untracked_owner) detail::remember_held(token, index);
    }
    return current_thread_slot();
}

/// True while the record of `endpoint` belongs to a live session.
[[nodiscard]] inline bool is_live(endpoint_id endpoint) noexcept {
    return endpoint != endpoint_id::none
        && detail::record_at_(static_cast<std::uint32_t>(endpoint)).is_live.load(std::memory_order_acquire) != 0;
}

/// The number of records live now, for tests.  Complexity: linear in the
/// records ever claimed.
[[nodiscard]] inline std::uint32_t live_count() noexcept { return detail::count_live(); }

// The wait of one thread on one endpoint.  A handle opens it before a
// declared blocking call of its transport, and when a polling or trying
// transport first finds nothing to do.  poll() runs between the retries.
// The scope lives on the stack of the waiting thread only.
class wait_scope {
    std::uint32_t endpoint_ = 0;
    std::uint64_t self_ = 0;
    std::uint32_t spins_ = 0;
    std::chrono::steady_clock::time_point start_{};
    std::chrono::steady_clock::time_point last_trace_{};
    detail::wait_chain suspect_{};

public:
    /// Refuses the wait when it breaks the priority order, and otherwise
    /// publishes that the calling thread waits on `endpoint`.
    explicit wait_scope(endpoint_id endpoint) noexcept : start_{std::chrono::steady_clock::now()} {
        last_trace_ = start_;
        if (endpoint == endpoint_id::none) return;
        const std::uint64_t self = detail::owner_token();
        if (self == detail::untracked_owner) return;
        endpoint_ = static_cast<std::uint32_t>(endpoint);
        self_ = self;
        detail::check_wait_order(self, endpoint_);
        detail::thread_record& me = detail::thread_at_(detail::index_of(self));
        me.wait_epoch.fetch_add(1, std::memory_order_acq_rel);
        me.blocked_on.store(endpoint_, std::memory_order_release);
    }

    wait_scope(const wait_scope&) = delete("a wait belongs to one frame of one thread");
    wait_scope& operator=(const wait_scope&) = delete("a wait belongs to one frame of one thread");
    static void* operator new(std::size_t) = delete("a wait_scope lives on the stack of the waiting thread");
    static void* operator new[](std::size_t) = delete("a wait_scope lives on the stack of the waiting thread");

    /// Publishes that the thread waits no more.
    ~wait_scope() {
        if (endpoint_ == 0) return;
        detail::thread_at_(detail::index_of(self_)).blocked_on.store(0, std::memory_order_release);
    }

    /// Pauses once.  After suspect_after, it traces the wait-for chain at
    /// most once for each confirm_after.  The same cycle at two traces is
    /// a deadlock: it prints the cycle and aborts.
    void poll() noexcept {
        CRUCIBLE_SPIN_PAUSE;
        if (endpoint_ == 0 || (++spins_ & 1023u) != 0) return;
        const auto now = std::chrono::steady_clock::now();
        if (now - start_ < suspect_after || now - last_trace_ < confirm_after) return;
        last_trace_ = now;
        const detail::wait_chain chain = detail::trace_chain(self_, endpoint_);
        if (chain.same_cycle_as(suspect_)) [[unlikely]]
            detail::report_deadlock(chain, self_);
        suspect_ = chain;
    }
};

}  // namespace fixy::session::watch
