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
// concurrent write.  A lifetime start does not launder this: the race
// is in the byte reads, ahead of any type interpretation.  Four things
// together contain it.  T is trivially copyable and trivially
// destructible, so a torn read is at worst a byte mix of two epochs,
// with no constructor, destructor or vtable to corrupt.  The retry
// discards a torn read before the caller can see it.  Compilers treat
// the copy as opaque bytes and do not speculate through it.  A stress
// fuzzer stands as the witness, and what it would catch is a missed
// retry rather than silent corruption.
//
// The second is the transition from byte buffer to T.  Reading a
// T-shaped value out of a byte buffer is undefined on its own, because
// the bytes hold byte lifetime and not T lifetime.  A lifetime start on
// the retry-success path ends the byte lifetime and starts a T lifetime
// in place, which makes the following dereference well defined.  That
// is the whole of what it does.  The start goes through
// foundation::lifetime::start_as_array, which also refuses a T with a
// proof subobject at compile time.
//
// The object is pinned because a reader can be holding the address of
// the sequence counter at any moment.
//
// SnapshotValue also requires ImplicitLifetimeThroughout, because the
// checked lifetime start refuses any other T.  The payload bytes and the
// read buffers are std::array, because
// utils/scripts/check-no-raw-array-member.py refuses a C array data member.

#include <fixy/Mutation.h>

#include <foundation/Lifetime.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>
#include <foundation/diag/Runtime.h>

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

// The claim the snapshot makes that no lattice grades.
// foundation/diag/RowHash.h folds the identity, so the carrier takes a
// cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct atomic_snapshot;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

// The size cap is a soft one.  A wider payload widens the writer's
// copy, and reader retries climb with it.  A payload past the cap
// belongs in a different primitive, such as a double-buffered pointer
// swap.

template <typename T>
concept SnapshotValue = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> && sizeof(T) <= 256
                     && sizeof(T) > 0 && ::foundation::lifetime::ImplicitLifetimeThroughout<T>;

template <SnapshotValue T>
class AtomicSnapshot : public ::foundation::Pinned<AtomicSnapshot<T>> {
public:
    using value_type = T;
    using row_discipline = ::fixy::row_discipline::atomic_snapshot;
    using row_payload = T;

    AtomicSnapshot() noexcept = default;

    explicit AtomicSnapshot(const T& initial) noexcept {
        std::memcpy(storage_.data(), &initial, sizeof(T));
        // No other thread holds this address yet, so the write above
        // needs no bracketing.  A plain advance would also be accepted
        // here, but reset_under_quiescence states the no-concurrent-
        // access precondition at the call site.
        seq_.reset_under_quiescence(2);
    }

    // The caller guarantees no concurrent publish.
    //
    // The copy must not become visible before the odd counter does, or a
    // reader reads the in-flight bytes while the counter still reads even.
    // Both of its samples agree, so the retry never fires and the torn
    // read is returned.  The acquire half of the increment does not order
    // the later stores after its store half: on a weakly ordered machine
    // the plain stores of the copy can become visible first.  The release
    // fence after the increment orders them, and it pairs with the acquire
    // fence of the reader (Boehm, "Can seqlocks get along with programming
    // language memory models?", 2012).  On x86 the fence emits nothing.
    void publish(const T& value) noexcept {
        const std::uint64_t old_seq = seq_.bump_by(1);
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
            two_writers_abort_();
        }
        std::atomic_thread_fence(std::memory_order_release);

        std::memcpy(storage_.data(), &value, sizeof(T));

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
        alignas(T) std::array<std::byte, sizeof(T)> buf{};

        for (;;) {
            std::uint64_t pre = seq_.get();

            while ((pre & 1u) != 0u) {
                CRUCIBLE_SPIN_PAUSE;
                pre = seq_.get();
            }

            std::memcpy(buf.data(), storage_.data(), sizeof(T));

            std::atomic_thread_fence(std::memory_order_acquire);

            const std::uint64_t post = seq_.get();

            if (pre == post) {
                // The lifetime start is the type-system step alone and
                // says nothing about the data race.
                return ::foundation::lifetime::start_as_array<T>(buf.data(), 1).front();
            }

            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // Never blocks.  A write in flight and a torn read are both
    // reported as nullopt, which suits best-effort instrumentation.
    [[nodiscard]] std::optional<T> try_load() const noexcept {
        const std::uint64_t pre = seq_.get();
        if ((pre & 1u) != 0u) {
            return std::nullopt;
        }

        alignas(T) std::array<std::byte, sizeof(T)> buf{};
        std::memcpy(buf.data(), storage_.data(), sizeof(T));

        // Brackets the copy for the same reason load does.
        std::atomic_thread_fence(std::memory_order_acquire);

        const std::uint64_t post = seq_.get();
        if (pre != post) {
            return std::nullopt;
        }
        return ::foundation::lifetime::start_as_array<T>(buf.data(), 1).front();
    }

    // Counts completed publishes, and never wraps at this width.  A
    // caller that caches the value can tell whether anything changed
    // since it last looked.
    [[nodiscard]] std::uint64_t version() const noexcept { return seq_.get() >> 1; }

private:
    // A second writer entered publish while the first one was inside it.
    [[noreturn]] CRUCIBLE_COLD static void two_writers_abort_() noexcept {
        ::foundation::diag::report_violation_at_and_abort(
            ::foundation::diag::Category::LinearityViolation,
            "two writers published into one AtomicSnapshot at the same time. The snapshot takes one writer.");
    }

    // Every thread reads the counter, and the writer writes both it and
    // the payload.  Each takes a cache line of its own so a publish
    // does not invalidate the other's line.
    alignas(64)::fixy::AtomicMonotonic<std::uint64_t> seq_ = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);

    alignas(64) std::array<std::byte, sizeof(T)> storage_{};
};

}  // namespace fixy::concurrent
