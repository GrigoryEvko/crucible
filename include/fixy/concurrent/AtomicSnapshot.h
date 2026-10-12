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
// The payload lives in 64-bit words, and each access to a shared word
// is an atomic operation: a release store in the writer and an acquire
// load in the reader.  A copy that a write overlaps is then a word mix
// of two epochs and no data race, and the retry discards it before the
// caller can see it.  ThreadSanitizer sees the atomic operations, so it
// needs no suppression, and it still reports a race on the value that
// a caller gives to publish.  The two copies between a local buffer and
// the words are plain copies of thread-private bytes.
//
// The transition from byte buffer to T needs one more step.  Reading a
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
// checked lifetime start refuses any other T.  The payload words and the
// read buffers are std::array, because the rule no-raw-array-member of the
// quarantine plugin refuses a C array data member.

#include <fixy/Mutation.h>

#include <foundation/Lifetime.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>
#include <foundation/diag/Runtime.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

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
        write_words_(initial, word_indices_{});
        // No other thread holds this address yet, so the write above
        // needs no bracketing.  A plain advance would also be accepted
        // here, but reset_under_quiescence states the no-concurrent-
        // access precondition at the call site.
        seq_.reset_under_quiescence(2);
    }

    // The caller guarantees no concurrent publish.
    //
    // A word must not become visible before the odd counter does, or a
    // reader reads an in-flight word while the counter still reads even.
    // Both of its samples agree, so the retry never fires and the torn
    // read is returned.  Each word store is a release store, so the
    // increment before it becomes visible first.  The release pairs with
    // the acquire load of the word in the reader (Boehm, "Can seqlocks get
    // along with programming language memory models?", 2012).  On x86 a
    // release store and an acquire load are plain moves.
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

        write_words_(value, word_indices_{});

        // Release alone would serve here, since the word stores above
        // cannot move after it.  The counter type supplies more than
        // that, and publish is the slow side, so the difference is not
        // worth a second counter.
        (void)seq_.bump_by(1);
    }

    // Spins until it observes a coherent snapshot.
    //
    // Acquire loads on the counter alone would not be enough.  Acquire
    // is one-way downward, so a plain copy could sink below the second
    // counter load.  The reader would then compare a stale pair while
    // reading fresh and possibly torn words.  Each word load is an
    // acquire load, so the second counter load cannot move before it, and
    // the copy is bracketed on both sides.
    [[nodiscard]] T load() const noexcept {
        alignas(T) std::array<std::byte, sizeof(T)> buf{};

        for (;;) {
            std::uint64_t pre = seq_.get();

            while ((pre & 1u) != 0u) {
                CRUCIBLE_SPIN_PAUSE;
                pre = seq_.get();
            }

            read_words_(buf.data(), word_indices_{});

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
        // The acquire word loads bracket the copy, as in load.
        read_words_(buf.data(), word_indices_{});

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
    static constexpr std::size_t kWordBytes = sizeof(std::uint64_t);
    // The words of the payload.  The last word holds the tail of T, and its other bytes stay zero.
    static constexpr std::size_t kWords = (sizeof(T) + kWordBytes - 1) / kWordBytes;

    // One index for each word.  A fold over the indices gives one access for each word with no
    // loop.  A loop of word accesses stays a loop in a Release build, because GCC does not unroll
    // it, and the hardening flags then add two checks to each turn.
    using word_indices_ = std::make_index_sequence<kWords>;

    // The bytes of T that one word holds: kWordBytes, or the tail in the last word.
    [[nodiscard]] static constexpr std::size_t bytes_of_word_(std::size_t word_index) noexcept {
        const std::size_t offset = word_index * kWordBytes;
        return sizeof(T) - offset < kWordBytes ? sizeof(T) - offset : kWordBytes;
    }

    // Copy one word of a value into a local word, and store it with a release store.
    CRUCIBLE_INLINE static void store_word_(const std::byte* source, std::uint64_t* words,
                                            std::size_t word_index) noexcept {
        std::uint64_t word = 0;
        __builtin_memcpy(&word, source + word_index * kWordBytes, bytes_of_word_(word_index));
        __atomic_store(words + word_index, &word, __ATOMIC_RELEASE);
    }

    // Load one word with an acquire load, and copy its bytes of T into a thread-private buffer.
    CRUCIBLE_INLINE static void load_word_(const std::uint64_t* words, std::byte* destination,
                                           std::size_t word_index) noexcept {
        std::uint64_t word = 0;
        __atomic_load(words + word_index, &word, __ATOMIC_ACQUIRE);
        __builtin_memcpy(destination + word_index * kWordBytes, &word, bytes_of_word_(word_index));
    }

    // Store each word of a value with a release store.  The read of the value is a plain read.
    // Complexity: one store for each word, at most 32.
    template <std::size_t... WordIndex>
    CRUCIBLE_INLINE void write_words_(const T& value, std::index_sequence<WordIndex...>) noexcept {
        const auto* const source = static_cast<const std::byte*>(static_cast<const void*>(&value));
        std::uint64_t* const words = storage_.data();
        (store_word_(source, words, WordIndex), ...);
    }

    // Load each word with an acquire load into a thread-private buffer.
    // Complexity: one load for each word, at most 32.
    template <std::size_t... WordIndex>
    CRUCIBLE_INLINE void read_words_(std::byte* destination, std::index_sequence<WordIndex...>) const noexcept {
        const std::uint64_t* const words = storage_.data();
        (load_word_(words, destination, WordIndex), ...);
    }

    // A second writer entered publish while the first one was inside it.
    [[noreturn]] CRUCIBLE_COLD static void two_writers_abort_() noexcept {
        ::foundation::diag::report_violation_at_and_abort(
            ::foundation::diag::Category::LinearityViolation,
            "two writers published into one AtomicSnapshot at the same time. The snapshot takes one writer.");
    }

    // Every thread reads the counter, and the writer writes both it and
    // the payload.  Each takes a cache line of its own so a publish
    // does not invalidate the other's line.  The payload words come
    // first, so their address is the address of the snapshot, and the
    // word accesses need no separate address computation.
    alignas(64) std::array<std::uint64_t, kWords> storage_{};

    alignas(64)::fixy::AtomicMonotonic<std::uint64_t> seq_ = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
};

}  // namespace fixy::concurrent
