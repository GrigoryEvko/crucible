#pragma once

// Single-writer many-reader snapshot publisher, a seqlock.  One writer
// publishes T, any number of readers observe a consistent T without
// locking.  A later write overwrites an earlier one, so there is no
// per-write delivery guarantee, which fits traffic where only the
// latest value matters.
//
// The caller guarantees that publish is never called concurrently.
// Supporting several writers takes a CAS on the sequence counter, and
// that is a different primitive with worse reader latency.
//
// A write brackets its bytes between two increments of the counter, so
// an odd counter means a write is in flight.  A reader samples the
// counter, copies the bytes, and samples again, keeping the copy only
// when the two samples agree.
//
// The construction carries two separate undefined-behavior concerns,
// and neither mitigation covers the other.
//
// The first is the data race between the reader's byte copy and a
// concurrent write.  std::start_lifetime_as does not launder this: the
// race is in the byte reads, ahead of any type interpretation.  Four
// things together contain it.  T is trivially copyable and trivially
// destructible, so a torn read is at worst a byte mix of two epochs,
// with no constructor, destructor or vtable to corrupt.  The retry
// discards a torn read before the caller can see it.  Compilers treat
// the copy as opaque bytes and do not speculate through it.  A stress
// fuzzer stands as the witness, and what it would catch is a missed
// retry rather than silent corruption.
//
// The second is the transition from byte buffer to T.  Reading a
// T-shaped value out of a byte buffer is undefined on its own, because
// the bytes hold byte lifetime and not T lifetime.  A
// std::start_lifetime_as on the retry-success path ends the byte
// lifetime and starts a T lifetime in place, which makes the following
// dereference well defined.  That is the whole of what it does.
//
// The object is pinned because a reader can be holding the address of
// the sequence counter at any moment.

#include <crucible/Platform.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Wait.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <type_traits>

namespace crucible::concurrent {

// The size cap is a soft one.  A wider payload widens the writer's
// copy, and reader retries climb with it.  A payload past the cap
// belongs in a different primitive, such as a double-buffered pointer
// swap.

template <typename T>
concept SnapshotValue =
    std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> && sizeof(T) <= 256 && sizeof(T) > 0;

template <SnapshotValue T>
class AtomicSnapshot : public safety::Pinned<AtomicSnapshot<T>> {
public:
    using value_type = T;

    AtomicSnapshot() noexcept = default;

    explicit AtomicSnapshot(const T& initial) noexcept : seq_{0} {
        std::memcpy(storage_, &initial, sizeof(T));
        // No other thread holds this address yet, so the write above
        // needs no bracketing.  A plain advance would also be accepted
        // here, but reset_under_quiescence states the no-concurrent-
        // access precondition at the call site.
        seq_.reset_under_quiescence(2);
    }

    // The caller guarantees no concurrent publish.
    //
    // The first increment has to carry acquire as well as release.
    // Release is a one-way barrier: earlier operations cannot move
    // after it, but later ones may move before it.  Under release
    // alone the copy is free to be hoisted above both increments, and
    // a reader then reads the in-flight bytes while the counter still
    // reads even.  Both of that reader's samples agree, so the retry
    // never fires and the torn read is returned.  Acquire on this
    // increment is what brackets the copy between the two of them.
    void publish(const T& value) noexcept {
        const uint64_t old_seq = seq_.bump_by(1);
        // The single-writer contract is checked on every build rather
        // than in debug alone, because an unguarded release path admits
        // two failures.  Two concurrent writers' copies race, and a
        // reader that sees the second writer's even counter gets a byte
        // mix of both.  Or a writer that increments once and then dies
        // leaves the counter odd, and every reader spins forever.
        // Aborting is right: the violation belongs to the caller, and
        // continuing can park all readers.  A contract assertion was
        // rejected because it collapses to nothing in the hot-path
        // translation units, which is where the check has to hold.
        if ((old_seq & 1u) != 0u) [[unlikely]] {
            std::abort();
        }

        std::memcpy(storage_, &value, sizeof(T));

        // Release alone would serve here, since the copy above cannot
        // move after it.  The counter type supplies more than that, and
        // publish is the slow side, so the difference is not worth a
        // second counter.
        (void)seq_.bump_by(1);
    }

    // Spins until it observes a coherent snapshot.
    //
    // Acquire loads on the counter alone would not be enough.  Acquire
    // is one-way downward, so nothing stops the byte copy from sinking
    // below the second counter load.  The reader would then compare a
    // stale pair while reading fresh and possibly torn bytes.  The
    // acquire fence between the copy and the second load closes that
    // direction, so the copy is bracketed on both sides.
    [[nodiscard]] T load() const noexcept {
        alignas(T) std::byte buf[sizeof(T)]{};

        for (;;) {
            uint64_t pre = seq_.get();

            while ((pre & 1u) != 0u) {
                CRUCIBLE_SPIN_PAUSE;
                pre = seq_.get();
            }

            std::memcpy(buf, storage_, sizeof(T));

            std::atomic_thread_fence(std::memory_order_acquire);

            const uint64_t post = seq_.get();

            if (pre == post) {
                // The lifetime start is the type-system step alone and
                // says nothing about the data race.
                return *std::start_lifetime_as<T>(buf);
            }

            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // Never blocks.  A write in flight and a torn read are both
    // reported as nullopt, which suits best-effort instrumentation.
    [[nodiscard]] std::optional<T> try_load() const noexcept {
        const uint64_t pre = seq_.get();
        if ((pre & 1u) != 0u) {
            return std::nullopt;
        }

        alignas(T) std::byte buf[sizeof(T)]{};
        std::memcpy(buf, storage_, sizeof(T));

        // Brackets the copy for the same reason load does.
        std::atomic_thread_fence(std::memory_order_acquire);

        const uint64_t post = seq_.get();
        if (pre != post) {
            return std::nullopt;
        }
        return *std::start_lifetime_as<T>(buf);
    }

    // The reader spins with a pause instruction, so its wait strategy
    // is the top of the wait lattice.  This overlay pins that at the
    // type level for a consumer that wants to state the constraint.
    // It is additive because existing callers take a bare T, and it
    // costs nothing beyond load itself.

    [[nodiscard]] safety::Wait<safety::WaitStrategy_v::SpinPause, T> load_pinned() const noexcept {
        return safety::Wait<safety::WaitStrategy_v::SpinPause, T>{load()};
    }

    // Every reader path takes acquire on the counter and an acquire
    // fence around the byte copy, so the reader's memory-order class is
    // Acquire.  Pinning it at the type level lets a consumer reject a
    // site that has crept to a stronger order, usually because a fence
    // was widened to make some other bug disappear.

    [[nodiscard]] safety::MemOrder<safety::MemOrderTag_v::Acquire, T> load_mo_pinned() const noexcept {
        return safety::MemOrder<safety::MemOrderTag_v::Acquire, T>{load()};
    }

    [[nodiscard]] std::optional<safety::MemOrder<safety::MemOrderTag_v::Acquire, T>>
    try_load_mo_pinned() const noexcept {
        auto opt = try_load();
        if (!opt) return std::nullopt;
        return safety::MemOrder<safety::MemOrderTag_v::Acquire, T>{std::move(*opt)};
    }

    [[nodiscard]] safety::MemOrder<safety::MemOrderTag_v::Acquire, uint64_t> version_mo_pinned() const noexcept {
        return safety::MemOrder<safety::MemOrderTag_v::Acquire, uint64_t>{version()};
    }

    // Counts completed publishes, and never wraps at this width.  A
    // caller that caches the value can tell whether anything changed
    // since it last looked.
    [[nodiscard]] uint64_t version() const noexcept { return seq_.get() >> 1; }

private:
    // Every thread reads the counter, and the writer writes both it and
    // the payload.  Each takes a cache line of its own so a publish
    // does not invalidate the other's line.
    alignas(64) safety::AtomicMonotonic<uint64_t> seq_{0};

    alignas(64) std::byte storage_[sizeof(T)]{};
};

static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "AtomicSnapshot's seq counter requires lock-free uint64_t atomic");

}  // namespace crucible::concurrent
