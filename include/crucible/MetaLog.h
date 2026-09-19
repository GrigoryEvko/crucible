#pragma once

// The tensor-metadata side channel of the trace ring, and single-producer
// single-consumer for the same reason and under the same rule.
//
// try_append reserves a run of slots by reading the head and advancing it,
// which is not one atomic operation, so two threads reserve the same run and
// write over each other. A ring entry then names a metadata index whose
// contents belong to a different op.
//
// Vigil owns the enforcement, and TraceRing.h states the rule in full.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <crucible/Platform.h>
#include <crucible/MerkleDag.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/warden/Registry.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/HugePageBuffer.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Stale.h>

namespace crucible::fixy::wrap {
using ::crucible::safety::AtomicMonotonic;
using ::crucible::safety::bounded_above;
using ::crucible::safety::HotPath;
using ::crucible::safety::HotPathTier_v;
using ::crucible::safety::HugePageBuffer;
using ::crucible::safety::Monotonic;
using ::crucible::safety::Refined;
using ::crucible::safety::Stale;
}  // namespace crucible::fixy::wrap

namespace crucible {

// The side channel for tensor metadata.
//
// The recording ring keeps one cache line per operation, which is far too
// small for a tensor's sizes, strides, type and device. That data lives here
// instead, and each ring slot carries the index of its block. An append that
// finds this buffer full yields no index: the operation is still recorded well
// enough to detect an iteration boundary, but the graph build does not skip
// that operation and carry on. build_trace_from returns a null graph the
// moment it meets an operation that had tensors and no index, which discards
// the whole iteration. A full buffer costs the iteration, not one node.
struct CRUCIBLE_OWNER MetaLog {
    static constexpr uint32_t CAPACITY = 1 << 20;
    static constexpr uint32_t MASK = CAPACITY - 1;

    // AtomicMonotonic carries its own alignas(64) and is 64 bytes wide, so
    // head fills bytes 0 through 63 by itself. An append therefore touches
    // two of the producer's lines, not one: head's, and the line after it
    // that holds cached_tail_, the buffer owner and the entries pointer. The
    // consumer's counter starts at byte 128 and sits alone, which is the
    // part that matters — neither thread ever invalidates a line the other
    // is reading.
    //
    // A thread reading its own counter needs no ordering, because coherence
    // already orders its own accesses. Publishing the counter with a release
    // is what makes the preceding copy visible: a consumer that acquires it
    // sees the entries. That also reaches a consumer that only acquires the
    // recording ring's counter, since this one is released first in program
    // order.
    alignas(64) crucible::fixy::wrap::AtomicMonotonic<uint32_t> head{0};
    // The producer's private view of the consumer's counter, refreshed only
    // when it claims the buffer is full. A stale value can only under-report
    // free space, so acting on it is safe. The type enforces the direction,
    // since a value that moved backwards would mean a lost acquire.
    crucible::fixy::wrap::Monotonic<uint32_t> cached_tail_{0};
    crucible::fixy::wrap::HugePageBuffer<TensorMeta> entries_buffer_;
    // A cached projection of the buffer's base, so an indexed access on the
    // hot path stays one load with no indirection through the owner.
    TensorMeta* entries = nullptr;

    alignas(64) crucible::fixy::wrap::AtomicMonotonic<uint32_t> tail{0};

    MetaLog()
        : entries_buffer_{crucible::fixy::wrap::HugePageBuffer<TensorMeta>::allocate(CAPACITY)},
          entries{entries_buffer_.data()} {
        // Fault the whole buffer in here rather than one page at a time
        // under the recording thread.
        //
        // TensorMeta is 168 bytes and CAPACITY is 1<<20, so this is
        // 176,160,768 bytes, or 43,008 pages of 4 KiB. Left untouched, every
        // one of them is a first-touch fault taken by whichever append
        // reaches it. Measured on this buffer: an append on the first lap
        // costs 25-31 ns against 5.1-5.6 ns once the page is resident, the
        // faulting 4.1% of appends put the first lap's p99 at 534-703 ns,
        // and each fault runs about 580 ns.
        //
        // The first lap is not a startup transient. SD 1.5's U-Net appends
        // 81,532 records for each iteration, which is 7.78% of CAPACITY, so
        // the lap spans roughly 13 iterations — exactly the window in which
        // the detector is deciding whether to compile and the phase timings
        // are being taken. Paying it here costs 28.6-30.6 ms in a
        // constructor that is already cold and already asks the allocator
        // for 168 MiB, and it makes the first recorded op cost the same as
        // the millionth.
        //
        // Measured end to end: constructing a Vigil, which builds one of
        // these, goes from 0.9 ms to 37.8 ms.
        //
        // Value-construction rather than a memset over the bytes. TensorMeta
        // is trivially copyable, which is what lets try_append memcpy into
        // it, but it carries member initializers and so is not trivially
        // default-constructible; memset on it is what -Wclass-memaccess
        // exists to reject. This form says the same thing to the optimizer,
        // which lowers an all-zero initializer to the same stores, and it
        // additionally begins the lifetime of every slot instead of leaving
        // the buffer as raw storage.
        if (entries != nullptr) {
            std::uninitialized_value_construct_n(entries, CAPACITY);
        }

        // Registering the region records it in a process-wide table. It
        // issues no system call, so despite the flag below nothing here
        // asks for huge pages, and on a host with
        // transparent_hugepage=madvise — the common setting, and this
        // one — the buffer gets 4 KiB pages: measured AnonHugePages is 0 kB
        // and THPeligible is 0, so khugepaged will not collapse it later
        // either. The 2 MiB alignment HugePageBuffer provides is necessary
        // for the advice and not sufficient on its own.
        //
        // The advice itself lives in warden::Hardening::hint_hugepage, which
        // only a benchmark that opts into a hardening policy ever reaches.
        // It is deliberately not called here: with defrag=madvise an advised
        // region enters direct compaction at fault time, and on a fragmented
        // host that turned this construction into a 2,724 ms stall in
        // measurement. Trading a bounded 30 ms for an unbounded multi-second
        // one is not a trade a runtime constructor can make. MADV_COLLAPSE,
        // which puts the stall where the caller chooses, is the shape to
        // reach for if huge pages are wanted later.
        crucible::warden::register_hot_region(entries, entries_buffer_.bytes(),
                                              /*huge=*/true, "MetaLog.entries");
    }

    ~MetaLog() {
        if (entries != nullptr) {
            crucible::warden::unregister_hot_region(entries);
        }
    }

    MetaLog(const MetaLog&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
    MetaLog& operator=(const MetaLog&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
    MetaLog(MetaLog&&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
    MetaLog& operator=(MetaLog&&) = delete("SPSC buffer is pinned to producer/consumer thread pair");

    // Appends a run of metadata records and returns the index of the first,
    // or no index if the buffer is full. Only the producing thread writes the
    // counter or the slots it is about to fill, which the thread-safety
    // analysis cannot express, so it is suppressed here.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] CRUCIBLE_INLINE MetaIndex try_append(const TensorMeta* metas, uint32_t n)
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::in_range<std::uint32_t>(n, std::uint32_t{0}, CAPACITY))
            pre(::crucible::decide::valid_span(n, metas)) {
        if (n == 0) [[unlikely]]
            return MetaIndex::none();

        const uint32_t h = head.peek_relaxed();

        // A private view that says there is room is always right, because the
        // real counter only ever frees more. Only the other answer needs the
        // cross-core read.
        if (h - cached_tail_.get() + n > CAPACITY) [[unlikely]] {
            cached_tail_.advance(tail.get());
            if (h - cached_tail_.get() + n > CAPACITY) [[unlikely]] {
                return MetaIndex::none();
            }
        }

        uint32_t start_pos = h & MASK;
        [[assume(start_pos < CAPACITY)]];
        uint32_t end_pos = start_pos + n;

        if (end_pos <= CAPACITY) [[likely]] {
            std::memcpy(&entries[start_pos], metas, n * sizeof(TensorMeta));
        } else {
            // The run wraps the end of the buffer, so it splits in two.
            uint32_t first_chunk = CAPACITY - start_pos;
            std::memcpy(&entries[start_pos], metas, first_chunk * sizeof(TensorMeta));
            std::memcpy(&entries[0], metas + first_chunk, (n - first_chunk) * sizeof(TensorMeta));
        }

        // One record spans three cache lines, hence three prefetches. Issuing
        // them before the publishing store rather than after gives them the
        // whole of the caller's remaining work to complete in. The prefetch
        // itself has no bearing on when the copy above becomes visible.
        {
            uint32_t next_pos = (h + n) & MASK;
            // Byte arithmetic for the builtin's address only. No array of
            // characters begins life here.
            const char* next_ptr = static_cast<const char*>(static_cast<const void*>(&entries[next_pos]));
            __builtin_prefetch(next_ptr, 1 /*write*/, 3 /*high locality*/);
            __builtin_prefetch(next_ptr + 64, 1 /*write*/, 3 /*high locality*/);
            __builtin_prefetch(next_ptr + 128, 1 /*write*/, 3 /*high locality*/);
        }

        // This release publishes the copy above to the consumer.
        head.advance(h + n);
        const MetaIndex result{h};
        // The index returned is the one from before the advance. Returning the
        // one after it would name the slot past the block just written, and
        // every reader would skip the block.
        CRUCIBLE_POST(result, !result.is_valid() || result.raw() == h);
        return result;
    }

    // The same body, with the tier declared in the return type so a consumer
    // that demands a hot-tier producer can be checked at compile time.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]]
    CRUCIBLE_INLINE crucible::fixy::wrap::HotPath<crucible::fixy::wrap::HotPathTier_v::Hot, MetaIndex>
    try_append_pinned(const TensorMeta* metas, uint32_t n)
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::valid_span(n, metas)) {
        return crucible::fixy::wrap::HotPath<crucible::fixy::wrap::HotPathTier_v::Hot, MetaIndex>{try_append(metas, n)};
    }

    // Preferred at new call sites: appending touches memory only, so the
    // caller's effect row must be empty, and a caller that allocates, blocks,
    // performs I/O, or runs at init or test time is rejected here rather than
    // discovered later.
    template <typename CallerRow = ::crucible::effects::Row<>>
        requires ::crucible::effects::IsPure<CallerRow>
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] CRUCIBLE_INLINE MetaIndex try_append_pure(const TensorMeta* metas,
                                                                                         uint32_t n)
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::valid_span(n, metas)) {
        return try_append(metas, n);
    }

    // The raw overload below is for the one caller that walks absolute
    // positions arithmetically, where wrapping each intermediate sum would
    // claim a strength the sum does not have. Everything else passes the
    // strong index.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] const TensorMeta& at(MetaIndex idx) const CRUCIBLE_LIFETIMEBOUND
    CRUCIBLE_NO_THREAD_SAFETY pre(idx.is_valid()) {
        return entries[idx.raw() & MASK];
    }

    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] const TensorMeta& at(uint32_t idx) const CRUCIBLE_LIFETIMEBOUND
    CRUCIBLE_NO_THREAD_SAFETY {
        return entries[idx & MASK];
    }

    // A pointer straight into the buffer when the requested run does not wrap
    // the end of it, saving the consumer a copy. A run that wraps yields no
    // pointer and the caller reads it element by element instead.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] TensorMeta* try_contiguous(uint32_t start, uint32_t count) const
        CRUCIBLE_LIFETIMEBOUND CRUCIBLE_NO_THREAD_SAFETY {
        if (count == 0) [[unlikely]]
            return nullptr;
        uint32_t const start_pos = start & MASK;
        TensorMeta* const result = (start_pos + count <= CAPACITY) ? &entries[start_pos] : nullptr;
        // The pointer, when there is one, aliases the slice starting at the
        // masked position. Dropping the mask or missing it by one would hand
        // the consumer a different block with nothing to signal the mistake.
        CRUCIBLE_POST(result, result == nullptr || result == &entries[start_pos]);
        return result;
    }

    void advance_tail(uint32_t new_tail) CRUCIBLE_NO_THREAD_SAFETY { tail.advance(new_tail); }

    // The two counters are read at different instants while both threads run,
    // so the difference is a snapshot of a value that was never simultaneously
    // true. The return type says so and forces the caller to acknowledge it.
    [[nodiscard]] crucible::fixy::wrap::Stale<uint32_t> size() const CRUCIBLE_NO_THREAD_SAFETY {
        return crucible::fixy::wrap::Stale<uint32_t>::at_infinity(head.get() - tail.get());
    }

    // Valid only once both threads have stopped. Every counter here moves
    // backwards, which the monotonicity contract otherwise forbids, so the
    // caller must have joined the producer and the consumer first.
    void reset() CRUCIBLE_NO_THREAD_SAFETY post(head.get() == 0) post(tail.get() == 0) post(cached_tail_.get() == 0) {
        head.reset_under_quiescence();
        tail.reset_under_quiescence();
        cached_tail_.reset_under_quiescence();
    }
};

// An append count that has already been checked against the capacity. Asking
// for more records than the buffer holds is not a transient failure that a
// retry clears: no state of the consumer can ever satisfy it, so the request
// is refused rather than looping. A caller that establishes the bound once at
// a boundary can carry this instead of re-checking at every layer.
using ValidMetaAppendCount =
    ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::bounded_above<MetaLog::CAPACITY>, uint32_t>;

[[nodiscard, gnu::const]] inline constexpr uint32_t make_meta_append_count(ValidMetaAppendCount raw) noexcept {
    return raw.value();
}

}  // namespace crucible
