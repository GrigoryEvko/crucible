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
// ── The hooks ───────────────────────────────────────────────────────
//
// The reports at exit and at a fatal signal run in hooks of the process,
// and mint_watch_hooks installs them.  The mint takes a context that owns
// Effect::Init, because each hook changes the process for the rest of its
// life (rule R20 of misc/01_10_2026_quarantine.md).  A program calls it one
// time, from its main.  A claim installs no hook, so a program that mints no
// hooks gets no report at exit or at a fatal signal.  src/fixy/session/
// Watch.cpp holds the cold code of the hooks and of the reports, and it
// installs the hooks through the doors of fixy/os.
//
// ── The report at exit ──────────────────────────────────────────────
//
// std::exit and std::quick_exit run a hook that walks the table.  A live
// record is a protocol that nothing will finish, so the hook prints each
// one and aborts.
//
// std::abort and the fatal signals SIGSEGV, SIGBUS, SIGILL and SIGFPE run
// no exit hook, so the hooks also install a handler for each of them
// through the signal door of fixy/os/Signal.h.  The handler writes the live
// records through the sink write of the Report family, which is safe in a
// signal handler.  Then the door gives the signal to the action that was
// installed before.  The handler runs on the alternate signal stack of the
// thread, so it runs also after a stack overflow on the thread that
// installs the hooks and on each thread that the thread door starts.
// std::_Exit and SIGKILL run no code at all, so the watch sees nothing
// there.  A child of a fork forgets each record of its parent.
//
// ── Every wait goes through the watch ───────────────────────────────
//
// A transport of fixy/session/Transport.h never waits in secret.  A read
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
// Every field is an atomic of the Atomic family, so a reader on another
// thread never races a writer.  Every "none" is zero, so the tables are in
// .bss and cost no page until a session touches them.

#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>
#include <utility>

namespace fixy::session::watch {

// The index of an endpoint record, plus one.  Zero is no record, so a
// handle that no mint tracks carries the zero value.
enum class endpoint_id : std::uint32_t {
    none = 0
};

// The priority of a session in the order that the watch keeps.  A
// Resource states it as a static constexpr member session_priority of
// this type.  lowest is the priority of a Resource that states none.
enum class priority : std::uint32_t {
    lowest = 0
};

inline constexpr std::uint32_t endpoint_capacity = 4096;
inline constexpr std::uint32_t thread_capacity = 1024;

// A wait that lasts this many nanoseconds of the monotonic clock starts
// to trace its chain.
inline constexpr std::uint64_t suspect_after_ns = 50000000;

// The same cycle must hold at two traces this many nanoseconds apart.
inline constexpr std::uint64_t confirm_after_ns = 50000000;

// The longest chain the detector follows.
inline constexpr std::size_t max_chain = 64;

class WatchHooks;

namespace detail {

inline constexpr std::uint64_t low_half = 0xFFFFFFFFu;

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
    ::foundation::core::Atomic<std::uint32_t> is_live{};
    ::foundation::core::Atomic<std::uint32_t> generation{};
    // The next free record, one-based, while the record is free.
    ::foundation::core::Atomic<std::uint32_t> next_free{};
    ::foundation::core::Atomic<std::uint32_t> protocol_size{};
    // The priority of the session, from its Resource.  0 is the lowest.
    ::foundation::core::Atomic<std::uint32_t> priority{};
    // The line of the site of the mint.
    ::foundation::core::Atomic<std::uint32_t> site_line{};
    // The peer endpoint, packed with its generation.
    ::foundation::core::Atomic<std::uint64_t> peer{};
    // The thread that holds the handle, packed with its slot generation.
    ::foundation::core::Atomic<std::uint64_t> owner{};
    ::foundation::core::Atomic<char const*> protocol_text{};
    // The file of the site of the mint, a text with a zero at its end.
    ::foundation::core::Atomic<char const*> site_file{};
};

// One thread that has touched the watch.
struct alignas(64) thread_record {
    ::foundation::core::Atomic<std::uint32_t> is_taken{};
    ::foundation::core::Atomic<std::uint32_t> generation{};
    // The endpoint the thread waits on, one-based, or zero.
    ::foundation::core::Atomic<std::uint32_t> blocked_on{};
    // Counts the waits of the thread, so that a detector can tell one
    // wait from the next.
    ::foundation::core::Atomic<std::uint64_t> wait_epoch{};
};

struct registry {
    std::array<endpoint_record, endpoint_capacity> endpoints{};
    std::array<thread_record, thread_capacity> threads{};
    // The free list of released records: a one-based index packed with a
    // tag that changes at each push and pop, which removes the ABA case.
    ::foundation::core::Atomic<std::uint64_t> free_head{};
    // The records never claimed start here, zero-based.
    ::foundation::core::Atomic<std::uint32_t> next_unused{};
    // Set by the first mint of the hooks.
    ::foundation::core::Atomic<std::uint32_t> hooks_installed{};
    // Set by the first thread that reports a deadlock.  Every thread on a
    // cycle detects it, and one report is enough.
    ::foundation::core::Atomic<std::uint32_t> is_reporting{};
    // Set by the first report of the live records, so that the abort that
    // ends the exit report does not print them again.
    ::foundation::core::Atomic<std::uint32_t> has_reported_live{};
};

// The registry and the per-thread state below are one per process,
// whatever shared library steps a session.  A thread with two slots, one in
// each library, would look like two threads to the detector.
CRUCIBLE_PROCESS_WIDE inline constinit registry g_registry{};

// The owner token of this thread, or zero before its first use.
CRUCIBLE_PROCESS_WIDE inline constinit thread_local std::uint64_t tls_owner_token = 0;

// The holder stamp of this thread: its one-based slot index, with the
// low bits of the slot generation above it.  Zero until the thread
// claims a slot, and for a thread that found no free slot.  The check
// file of this header checks that the one-based slot index fits the low
// bits.
inline constexpr unsigned stamp_index_bits = 11;
CRUCIBLE_PROCESS_WIDE inline constinit thread_local std::uint16_t tls_holder_stamp = 0;

// The records that this thread took as holder, each a one-based index
// packed with its generation.  An entry is only a candidate: the order
// check keeps the entries whose record is live, has that generation and
// names this thread as its owner.  Only this thread reads or writes the
// list.  When every entry is taken by a record this thread still holds,
// the check reads the whole table instead.
inline constexpr std::size_t held_capacity = 16;
CRUCIBLE_PROCESS_WIDE inline constinit thread_local std::uint64_t tls_held[held_capacity]{};
CRUCIBLE_PROCESS_WIDE inline constinit thread_local bool tls_held_overflowed = false;

[[nodiscard]] constexpr std::uint16_t holder_stamp_of(std::uint32_t slot, std::uint32_t generation) noexcept {
    return static_cast<std::uint16_t>(slot | (generation << stamp_index_bits));
}

[[nodiscard]] inline endpoint_record& record_at_(std::uint32_t one_based) noexcept {
    return g_registry.endpoints[one_based - 1];
}

[[nodiscard]] inline thread_record& thread_at_(std::uint32_t one_based) noexcept {
    return g_registry.threads[one_based - 1];
}

// The count of the records that a claim ever took: a bound of each walk
// of the table.
[[nodiscard]] inline std::uint32_t claimed_bound() noexcept {
    const std::uint32_t claimed = g_registry.next_unused.load_acquire();
    return claimed < endpoint_capacity ? claimed : endpoint_capacity;
}

// Frees the slot of a thread when the thread ends.  Only the cold claim
// below names it, so no hot path pays for its destructor.
class slot_releaser {
public:
    slot_releaser() noexcept = default;
    slot_releaser(const slot_releaser&) = delete("one releaser owns the slot of its thread");
    slot_releaser& operator=(const slot_releaser&) = delete("one releaser owns the slot of its thread");

    /// Takes the one-based slot that the claim of the thread took.
    void adopt(std::uint32_t slot) noexcept { slot_ = slot; }

    /// Publishes that the thread waits on nothing, bumps the slot
    /// generation so that no stale owner token matches it, and frees it.
    ~slot_releaser() {
        if (slot_ == 0) return;
        thread_record& me = thread_at_(slot_);
        me.blocked_on.store_release(0);
        static_cast<void>(me.generation.fetch_add_acq_rel(1));
        me.is_taken.store_release(0);
        tls_owner_token = 0;
        tls_holder_stamp = 0;
    }

private:
    std::uint32_t slot_ = 0;
};

/// Claims a thread slot for the calling thread and returns its owner
/// token.  A full table gives the untracked token, and the thread keeps
/// it.  Complexity: linear in thread_capacity, once for each thread.
CRUCIBLE_PROCESS_WIDE [[gnu::cold, gnu::noinline]] inline std::uint64_t claim_thread_slot() noexcept {
    for (std::uint32_t slot = 1; slot <= thread_capacity; ++slot) {
        thread_record& candidate = thread_at_(slot);
        if (candidate.is_taken.load_acquire() != 0) continue;
        if (candidate.is_taken.cas_acq_rel(0, 1).is_err()) continue;
        thread_local slot_releaser releaser;
        releaser.adopt(slot);
        const std::uint32_t generation = candidate.generation.load_acquire();
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
    for (;;) {
        const std::uint64_t head = g_registry.free_head.load_acquire();
        const std::uint32_t top = index_of(head);
        if (top == 0) return 0;
        const std::uint32_t next = record_at_(top).next_free.load_acquire();
        const std::uint64_t replacement = pack(next, generation_of(head) + 1);
        if (g_registry.free_head.cas_acq_rel(head, replacement).is_ok()) return top;
    }
}

/// Pushes a record, one-based, onto the free list.
inline void push_free(std::uint32_t index) noexcept {
    for (;;) {
        const std::uint64_t head = g_registry.free_head.load_acquire();
        record_at_(index).next_free.store_release(index_of(head));
        const std::uint64_t replacement = pack(index, generation_of(head) + 1);
        if (g_registry.free_head.cas_acq_rel(head, replacement).is_ok()) return;
    }
}

/// The number of live records.  Complexity: linear in the records ever
/// claimed.
[[nodiscard]] inline std::uint32_t count_live() noexcept {
    const std::uint32_t bound = claimed_bound();
    std::uint32_t live = 0;
    for (std::uint32_t index = 1; index <= bound; ++index) {
        if (record_at_(index).is_live.load_acquire() != 0) ++live;
    }
    return live;
}

// One link of a wait-for chain: a thread waits on an endpoint, and the
// peer of that endpoint is held by the next thread.
//
// pad holds the four bytes after the peer, so a link has no padding byte.
// With -ftrivial-auto-var-init=zero, GCC writes zero to each padding hole of
// each link of the chain that trace_chain returns, one store for each hole.
// test/fixy/test_session_handle.cpp makes sure that the link has no padding
// byte.
struct chain_link {
    std::uint32_t endpoint = 0;
    std::uint32_t endpoint_generation = 0;
    std::uint32_t peer = 0;
    std::array<std::uint8_t, 4> pad{};
    std::uint64_t holder = 0;
    std::uint64_t holder_epoch = 0;

    friend constexpr bool operator==(const chain_link&, const chain_link&) noexcept = default;
};

// links is a C array, because the base has no fixed array, and the report of a
// deadlock in src/fixy/session/Watch.cpp reads each link of a cycle.
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
        if (waiting.is_live.load_acquire() == 0) return chain;
        const std::uint64_t peer = waiting.peer.load_acquire();
        if (peer == 0) return chain;
        const endpoint_record& held = record_at_(index_of(peer));
        if (held.is_live.load_acquire() == 0 || held.generation.load_acquire() != generation_of(peer)) {
            return chain;
        }
        const std::uint64_t holder = held.owner.load_acquire();
        if (holder == 0 || holder == untracked_owner) return chain;
        const thread_record& thread = thread_at_(index_of(holder));
        if (thread.is_taken.load_acquire() == 0 || thread.generation.load_acquire() != generation_of(holder)) {
            return chain;
        }
        chain.links[chain.count++] = chain_link{
            .endpoint = current,
            .endpoint_generation = waiting.generation.load_acquire(),
            .peer = index_of(peer),
            .holder = holder,
            .holder_epoch = thread.wait_epoch.load_acquire(),
        };
        if (holder == self) {
            chain.is_cycle = true;
            return chain;
        }
        const std::uint32_t next = thread.blocked_on.load_acquire();
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
/// src/fixy/session/Watch.cpp holds the body.
[[noreturn, gnu::cold]] void report_deadlock(const wait_chain& chain, std::uint64_t self) noexcept;

// ── The priority order ───────────────────────────────────────────────

/// True when the record at `index` is live, has `generation`, and has
/// `self` as its owner.
[[nodiscard]] inline bool holds_record(std::uint64_t self, std::uint32_t index, std::uint32_t generation) noexcept {
    const endpoint_record& record = record_at_(index);
    return record.is_live.load_acquire() != 0 && record.generation.load_acquire() == generation
        && record.owner.load_acquire() == self;
}

/// Adds a record that this thread just took as holder to its list.  It
/// takes an entry whose record this thread no longer holds, and marks the
/// list overflowed when every entry is still held.  Complexity: linear in
/// held_capacity, on the cold path of a claim or a change of holder.
inline void remember_held(std::uint64_t self, std::uint32_t index) noexcept {
    const std::uint64_t entry = pack(index, record_at_(index).generation.load_acquire());
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

// The two checks that can refuse a wait.  The enumerator gives the tag of
// the diagnostic.
enum class wait_refusal : std::uint8_t {
    held_peer,
    breaks_priority_order
};

/// Prints why a wait of the calling thread on `waited` is refused, with
/// the record `held` that breaks the order, and aborts.
/// src/fixy/session/Watch.cpp holds the body.
[[noreturn, gnu::cold]] void refuse_wait(wait_refusal refusal, std::uint32_t waited, std::uint32_t held) noexcept;

/// Refuses a wait of the calling thread on `waited` that breaks the
/// priority order: the thread holds the peer of `waited`, or it holds
/// another live endpoint whose priority is not below the priority of
/// `waited`.  Complexity: linear in held_capacity, or in the records ever
/// claimed after the list overflowed.
inline void check_wait_order(std::uint64_t self, std::uint32_t waited) noexcept {
    const endpoint_record& waited_record = record_at_(waited);
    const std::uint32_t priority = waited_record.priority.load_acquire();
    const std::uint64_t peer = waited_record.peer.load_acquire();
    const auto check_one = [&](std::uint32_t index, std::uint32_t generation) noexcept {
        if (index == waited) return;
        if (peer != 0 && index == index_of(peer) && generation == generation_of(peer)) {
            refuse_wait(wait_refusal::held_peer, waited, index);
        }
        if (record_at_(index).priority.load_acquire() >= priority) {
            refuse_wait(wait_refusal::breaks_priority_order, waited, index);
        }
    };
    if (!tls_held_overflowed) {
        for (const std::uint64_t held : tls_held) {
            if (held == 0 || !holds_record(self, index_of(held), generation_of(held))) continue;
            check_one(index_of(held), generation_of(held));
        }
        return;
    }
    const std::uint32_t bound = claimed_bound();
    for (std::uint32_t index = 1; index <= bound; ++index) {
        const endpoint_record& record = record_at_(index);
        if (record.is_live.load_acquire() == 0) continue;
        if (record.owner.load_acquire() != self) continue;
        check_one(index, record.generation.load_acquire());
    }
}

/// The nanoseconds of the monotonic clock, through the clock door of
/// fixy/os/MonotonicClock.h.  A failed read gives zero, and poll() then
/// traces no chain.  src/fixy/session/Watch.cpp holds the body.
[[nodiscard]] std::uint64_t monotonic_ns() noexcept;

/// Installs the hooks of the watch one time for the process, and gives the
/// proof of them.  mint_watch_hooks holds its gate.
/// src/fixy/session/Watch.cpp holds the body.
[[nodiscard]] WatchHooks install_hooks_() noexcept;

}  // namespace detail

// Whether a claim names the calling thread as the holder.  A mint that
// opens the session on the calling thread names it.  A channel mint
// claims on the thread that forks, and each end's own thread becomes the
// holder when it opens its end.
enum class holder_on_claim : std::uint8_t {
    calling_thread,
    none_yet
};

/// Claims a record for a session that starts at `protocol`, minted at
/// `site`, with the priority of its Resource.  A full table gives
/// endpoint_id::none, and the session is then not tracked.
[[nodiscard]] inline endpoint_id claim(std::string_view protocol, std::source_location site, priority order,
                                       holder_on_claim holder) noexcept {
    std::uint32_t index = detail::pop_free();
    if (index == 0) {
        const std::uint32_t fresh = detail::g_registry.next_unused.fetch_add_acq_rel(1);
        if (fresh >= endpoint_capacity) return endpoint_id::none;
        index = fresh + 1;
    }
    detail::endpoint_record& record = detail::record_at_(index);
    // The claimant is the only writer of a record's generation, so a load
    // and a store suffice.  A concurrent trace only reads it.
    record.generation.store_release(record.generation.load_acquire() + 1);
    record.peer.store_release(0);
    record.protocol_text.store_release(protocol.data());
    record.protocol_size.store_release(static_cast<std::uint32_t>(protocol.size()));
    record.site_file.store_release(site.file_name());
    record.site_line.store_release(site.line());
    record.priority.store_release(std::to_underlying(order));
    if (holder == holder_on_claim::calling_thread) {
        const std::uint64_t self = detail::owner_token();
        record.owner.store_release(self);
        record.is_live.store_release(1);
        if (self != detail::untracked_owner) detail::remember_held(self, index);
    } else {
        record.owner.store_release(0);
        record.is_live.store_release(1);
    }
    return static_cast<endpoint_id>(index);
}

/// Makes `first` and `second` the two ends of one channel.
inline void link(endpoint_id first, endpoint_id second) noexcept {
    if (first == endpoint_id::none || second == endpoint_id::none) return;
    const auto first_index = static_cast<std::uint32_t>(first);
    const auto second_index = static_cast<std::uint32_t>(second);
    detail::record_at_(first_index)
        .peer.store_release(detail::pack(second_index, detail::record_at_(second_index).generation.load_acquire()));
    detail::record_at_(second_index)
        .peer.store_release(detail::pack(first_index, detail::record_at_(first_index).generation.load_acquire()));
}

/// Ends the session of `endpoint`: it reached End, detached or cancelled.
inline void release(endpoint_id endpoint) noexcept {
    if (endpoint == endpoint_id::none) return;
    const auto index = static_cast<std::uint32_t>(endpoint);
    detail::endpoint_record& record = detail::record_at_(index);
    record.is_live.store_release(0);
    record.owner.store_release(0);
    record.peer.store_release(0);
    detail::push_free(index);
}

// The slot of a thread in the thread table, one-based, in the low bits,
// and the low bits of the slot generation above them.  Zero is a thread
// with no slot.  A handle keeps the value of the thread that holds it, so
// a move or a step compares two small integers and touches no shared line
// unless the handle crossed threads.  The generation bits tell a thread
// from an earlier thread that had the same slot, until the slot has 32
// more owners.
enum class thread_slot : std::uint16_t {
    none = 0
};

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
        detail::record_at_(index).owner.store_release(token);
        if (token != detail::untracked_owner) detail::remember_held(token, index);
    }
    return current_thread_slot();
}

/// True while the record of `endpoint` belongs to a live session.
[[nodiscard]] inline bool is_live(endpoint_id endpoint) noexcept {
    return endpoint != endpoint_id::none
        && detail::record_at_(static_cast<std::uint32_t>(endpoint)).is_live.load_acquire() != 0;
}

/// The number of records live now, for tests.  Complexity: linear in the
/// records ever claimed.
[[nodiscard]] inline std::uint32_t live_count() noexcept { return detail::count_live(); }

// The proof that the hooks of the watch run in this process (The hooks).
// Only mint_watch_hooks makes one.  Each constructor is user-provided, so
// the proof is neither trivially copyable nor an implicit-lifetime type,
// and std::bit_cast and std::start_lifetime_as build none.  The copy stays
// public, so a holder can pass the proof along.
class WatchHooks final {
    WatchHooks() noexcept {}

    friend auto detail::install_hooks_() noexcept -> WatchHooks;

public:
    WatchHooks(const WatchHooks&) noexcept {}
};

// A context that can install the hooks: one that owns Effect::Init.
template <class Ctx>
concept CtxFitsWatchHooks = ::foundation::effects::IsExecCtx<Ctx>
                         && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

/// Installs the report at exit, the report at each fatal signal and the
/// reset of the table in the child of a fork, one time for the process
/// (The hooks).  A second call installs nothing and gives the proof again.
/// A second call on a different thread can return before the first call
/// completes the install, so call the mint from main before main starts a
/// thread.  The hooks and the alternate signal stack of the calling thread
/// stay until the process ends.  A refused install ends the process.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsWatchHooks<Ctx>
[[nodiscard]] WatchHooks mint_watch_hooks(Ctx const& /*init*/) noexcept {
    return detail::install_hooks_();
}

// The wait of one thread on one endpoint.  A handle opens it before a
// declared blocking call of its transport, and when a polling or trying
// transport first finds nothing to do.  poll() runs between the retries.
// The scope lives on the stack of the waiting thread only.
class wait_scope {
    std::uint32_t endpoint_ = 0;
    std::uint64_t self_ = 0;
    std::uint32_t spins_ = 0;
    std::uint64_t start_ns_ = 0;
    std::uint64_t last_trace_ns_ = 0;
    detail::wait_chain suspect_{};

public:
    /// Refuses the wait when it breaks the priority order, and otherwise
    /// publishes that the calling thread waits on `endpoint`.
    explicit wait_scope(endpoint_id endpoint) noexcept : start_ns_{detail::monotonic_ns()} {
        last_trace_ns_ = start_ns_;
        if (endpoint == endpoint_id::none) return;
        const std::uint64_t self = detail::owner_token();
        if (self == detail::untracked_owner) return;
        endpoint_ = static_cast<std::uint32_t>(endpoint);
        self_ = self;
        detail::check_wait_order(self, endpoint_);
        detail::thread_record& me = detail::thread_at_(detail::index_of(self));
        static_cast<void>(me.wait_epoch.fetch_add_acq_rel(1));
        me.blocked_on.store_release(endpoint_);
    }

    wait_scope(const wait_scope&) = delete("a wait belongs to one frame of one thread");
    wait_scope& operator=(const wait_scope&) = delete("a wait belongs to one frame of one thread");
    static void* operator new(std::size_t) = delete("a wait_scope lives on the stack of the waiting thread");
    static void* operator new[](std::size_t) = delete("a wait_scope lives on the stack of the waiting thread");

    /// Publishes that the thread waits no more.
    ~wait_scope() {
        if (endpoint_ == 0) return;
        detail::thread_at_(detail::index_of(self_)).blocked_on.store_release(0);
    }

    /// Pauses once.  After suspect_after_ns, it traces the wait-for chain
    /// at most once for each confirm_after_ns.  The same cycle at two
    /// traces is a deadlock: it prints the cycle and aborts.
    void poll() noexcept {
        CRUCIBLE_SPIN_PAUSE;
        if (endpoint_ == 0 || (++spins_ & 1023u) != 0) return;
        const std::uint64_t now_ns = detail::monotonic_ns();
        // The clock counts from boot, so neither sum wraps.
        if (now_ns == 0 || now_ns < start_ns_ + suspect_after_ns || now_ns < last_trace_ns_ + confirm_after_ns) return;
        last_trace_ns_ = now_ns;
        const detail::wait_chain chain = detail::trace_chain(self_, endpoint_);
        if (chain.same_cycle_as(suspect_)) [[unlikely]]
            detail::report_deadlock(chain, self_);
        suspect_ = chain;
    }
};

}  // namespace fixy::session::watch
