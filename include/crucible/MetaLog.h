#pragma once

// The tensor-metadata side channel of the trace ring, and single-producer
// single-consumer for the same reason and under the same rule.
//
// try_append reserves a run of slots by reading the head and advancing it,
// which is not one atomic operation, so two threads reserve the same run and
// write over each other. A ring entry then names a metadata index whose
// contents belong to a different op.
//
// Vigil owns the enforcement, and TraceRing.h states the rule in full.  The
// log has the two publishes of the ring (THE TWO PUBLISHES of TraceRing.h):
// try_append checks the head at each publish, and try_append_proven takes the
// proof of the record path and loads nothing from the head.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <crucible/ForegroundCtx.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <crucible/warden/Registry.h>

#include <fixy/Aliases.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/Stale.h>
#include <fixy/os/Pages.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

namespace crucible {

// The side channel for tensor metadata.
//
// The recording ring keeps one cache line per operation, which is far too
// small for a tensor's sizes, strides, type and device. That data lives here
// instead, and each ring slot carries the index of its block. An append that
// finds this buffer full yields no index: the operation is still recorded well
// enough to detect an iteration boundary, but the graph build does not skip
// that operation and carry on. build_trace_from returns no graph the moment
// it meets an operation that had tensors and no index, which discards the
// whole iteration. A full buffer costs the iteration, not one node.
struct CRUCIBLE_OWNER MetaLog {
    static constexpr uint32_t CAPACITY = 1 << 20;
    static constexpr uint32_t MASK = CAPACITY - 1;

    // The writers of the two counters (THE WRITER OF A COUNTER of
    // fixy/Mutation.h): the producer writes head, and the consumer writes
    // tail.  The log is the owner of each proof, so only the doors of the log
    // publish a counter.
    struct HeadWriter final {};
    struct TailWriter final {};
    using head_writer = ::fixy::SoleWriterProof<MetaLog, HeadWriter>;
    using tail_writer = ::fixy::SoleWriterProof<MetaLog, TailWriter>;

    // AtomicMonotonic carries its own alignas(64) and is 64 bytes wide, so
    // head fills bytes 0 through 63 by itself. An append therefore touches
    // two of the producer's lines, not one: head's, and the line after it
    // that holds cached_tail_, own_head_ and the buffer owner with its base.
    // The consumer's counter starts at byte 128 and sits alone, which is the
    // part that matters — neither thread ever invalidates a line the other
    // is reading.  The producer reads its position from own_head_, so an
    // append with the proof only stores to the line of head.
    //
    // A thread reading its own counter needs no ordering, because coherence
    // already orders its own accesses. Publishing the counter with a release
    // is what makes the preceding copy visible: a consumer that acquires it
    // sees the entries. That also reaches a consumer that only acquires the
    // recording ring's counter, since this one is released first in program
    // order.
    //
    // Each counter counts the records that passed it from the first record
    // that the log held, and a MetaIndex is such a count.  The counters have
    // 64 bits.  At 2^32 records a 32-bit counter wraps to zero, and the
    // contract of the counter stops the process.  A large model reaches that
    // count in hours.  The slot of a record is its count masked by MASK.
    // The producer is the one writer of head, and the consumer is the one
    // writer of tail.
    alignas(64)::fixy::AtomicMonotonic<uint64_t, head_writer> head =
        ::fixy::mint_atomic_monotonic<uint64_t, head_writer>(0);
    // The producer's private view of the consumer's counter, refreshed only
    // when it claims the buffer is full. A stale value can only under-report
    // free space, so acting on it is safe. The type enforces the direction,
    // since a value that moved backwards would mean a lost acquire.
    ::fixy::Monotonic<uint64_t> cached_tail_ = ::fixy::mint_monotonic<uint64_t>(0);
    // The copy of the head of the producer.  It holds the value of the head
    // after each publish.
    ::fixy::Monotonic<uint64_t> own_head_ = ::fixy::mint_monotonic<uint64_t>(0);

private:
    // The buffer of the CAPACITY records, at the alignment of a huge page.
    // Its count is in its type, so a masked slot and the window of a run of
    // an append have no check that the optimizer keeps.  An indexed access on
    // the hot path is one load of the base of the buffer from this object.
    // The producer writes a run through the frame door of the buffer, and the
    // consumer reads through its View of the const records.  The buffer does
    // not move, and it holds its storage until the end of the log.
    ::fixy::FixedHeapArray<TensorMeta, CAPACITY, crucible::warden::kHugePageBytes> records_;

public:
    alignas(64)::fixy::AtomicMonotonic<uint64_t, tail_writer> tail =
        ::fixy::mint_atomic_monotonic<uint64_t, tail_writer>(0);

    // The caller gives the allocation capability of its context for the
    // buffer of the records.
    explicit MetaLog(::foundation::effects::Alloc allocation)
        : records_{::fixy::mint_fixed_heap_array<TensorMeta, CAPACITY, crucible::warden::kHugePageBytes>(
              allocation, TensorMeta{})} {
        // The allocation faults the whole buffer in here rather than one page
        // at a time under the recording thread.
        //
        // TensorMeta is 176 bytes and CAPACITY is 1<<20, so this is
        // 184,549,376 bytes, or 45,056 pages of 4 KiB. Left untouched, every
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
        // for 176 MiB, and it makes the first recorded op cost the same as
        // the millionth.
        //
        // Measured end to end: constructing a Vigil, which builds one of
        // these, goes from 0.9 ms to 37.8 ms.
        //
        // Each record is a copy of TensorMeta{}, not a zero byte. TensorMeta
        // is trivially copyable, which is what lets try_append copy its
        // bytes, but it carries member initializers, so a zero byte is not
        // its default value.

        // Registering the region records it in a process-wide table. It
        // issues no system call, so despite the flag below nothing here
        // asks for huge pages, and on a host with
        // transparent_hugepage=madvise — the common setting, and this
        // one — the buffer gets 4 KiB pages: measured AnonHugePages is 0 kB
        // and THPeligible is 0, so khugepaged will not collapse it later
        // either. The huge-page alignment of the buffer is necessary for the
        // advice and not sufficient on its own.
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
        crucible::warden::register_hot_region(::fixy::pages::storage_of(records_.view()), /*huge_hint=*/true);
    }

    ~MetaLog() { crucible::warden::unregister_hot_region(::fixy::pages::storage_of(records_.view())); }

    MetaLog(const MetaLog&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
    MetaLog& operator=(const MetaLog&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
    MetaLog(MetaLog&&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
    MetaLog& operator=(MetaLog&&) = delete("SPSC buffer is pinned to producer/consumer thread pair");

    // Appends a run of metadata records and returns the index of the first,
    // or no index if the buffer is full or the run is empty. Only the
    // producing thread writes the counter or the slots it is about to fill,
    // which the thread-safety analysis cannot express, so it is suppressed
    // here.  The check of the publish loads the head.
    [[nodiscard]] CRUCIBLE_INLINE MetaIndex try_append(::fixy::View<const TensorMeta> metas) CRUCIBLE_NO_THREAD_SAFETY {
        return fill_run_(metas).match(
            [&](MetaIndex start) noexcept {
                // This release publishes the copy of fill_run_ to the consumer,
                // and the copy of the head steps from the checked value.
                const uint64_t h = start.raw();
                head.advance_sole_writer_from(prove_head_writer_(), h, h + metas.size(), own_head_);
                return start;
            },
            [] noexcept { return MetaIndex::none(); });
    }

    // The append of the producer of the Vigils, which holds the proof of the
    // record path.  The type proves the one writer, so the publish loads
    // nothing from the head.
    [[nodiscard]] CRUCIBLE_INLINE MetaIndex try_append_proven(RecordProof const& /*proof*/,
                                                              ::fixy::View<const TensorMeta> metas)
        CRUCIBLE_NO_THREAD_SAFETY {
        return fill_run_(metas).match(
            [&](MetaIndex start) noexcept {
                head.advance_sole_writer_by_proof(prove_head_writer_(), own_head_, start.raw() + metas.size());
                return start;
            },
            [] noexcept { return MetaIndex::none(); });
    }

    // The append of a pure caller, with the gate of TraceRing::try_append_pure:
    // the row of the context of the caller is empty, and its source permits no
    // atom.  A context narrowed with in_row keeps its source, so a narrowed
    // context of a test, an init step or a background stage fails the gate.
    template <::foundation::effects::IsExecCtx Ctx>
        requires ::fixy::IsPure<::foundation::effects::row_type_of_t<Ctx>>
              && ::foundation::effects::CtxHasPureSource<Ctx>
    [[nodiscard]] CRUCIBLE_INLINE MetaIndex try_append_pure(Ctx const&, ::fixy::View<const TensorMeta> metas)
        CRUCIBLE_NO_THREAD_SAFETY {
        return try_append(metas);
    }

    // Copies the run of records that starts at index start into dst, one
    // record for each element of dst.  A run that passes the end of the
    // buffer takes two copies.  The tail moves past a run when the consumer
    // releases it.  The producer then writes new records over those slots.  A
    // reader that keeps the records after the release keeps this copy, and no
    // reference into the buffer.  The consumer copies a run before it
    // releases the run, and the producer writes no slot of that run while the
    // copy operates.
    void copy_run(uint64_t start, ::fixy::View<TensorMeta> dst) const CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(dst.size() <= CAPACITY);
        if (dst.size() == 0) return;
        const uint32_t count = static_cast<uint32_t>(dst.size());
        const ::fixy::View<TensorMeta const> ring = records_.view();
        const uint32_t start_pos = static_cast<uint32_t>(start & MASK);
        const uint32_t first_chunk = (count <= CAPACITY - start_pos) ? count : CAPACITY - start_pos;
        copy_records_(dst, 0, ring, start_pos, first_chunk);
        copy_records_(dst, first_chunk, ring, 0, count - first_chunk);
        // The first and the last record of the copy are the records at the
        // two ends of the run.  A start without the mask, or a split of the
        // run at the wrong slot, puts a different record at one end.
        CRUCIBLE_POST(0, *dst.get(0).expect("the copy holds its first record") == record_at_(start));
        CRUCIBLE_POST(0, *dst.get(count - 1).expect("the copy holds its last record") == record_at_(start + count - 1));
    }

    // Copies the record at index, with the rule of copy_run.
    [[nodiscard]] TensorMeta copy_record(uint64_t index) const CRUCIBLE_NO_THREAD_SAFETY { return record_at_(index); }

    // True when the record lies in the buffer of the log, such as for a test
    // that no op of a built graph points into the log.  The test compares
    // addresses only (offset_of of foundation/core/Region.h), and it gives no
    // reference into a slot, because the producer writes a new record into a
    // slot after its release.
    [[nodiscard]] bool holds_record(TensorMeta const& record) const noexcept {
        const ::fixy::View<TensorMeta const> ring = records_.view();
        return ::foundation::core::offset_of(ring, record).is_some();
    }

    // The consumer is the one writer of tail, so its relaxed load reads its
    // own last store.  The callers give only the new tail.
    void advance_tail(uint64_t new_tail) CRUCIBLE_NO_THREAD_SAFETY {
        tail.advance_sole_writer_from(prove_tail_writer_(), tail.peek_relaxed(), new_tail);
    }

    // The two counters are read at different instants while both threads run,
    // so the difference is a snapshot of a value that was never simultaneously
    // true. The return type says so and forces the caller to acknowledge it.
    //
    // The function reads the tail first.  The head that it reads next is at
    // least that tail, and the difference does not wrap.  The tail can move
    // between the two reads, and the difference is then larger than the log
    // holds.  The result stops at CAPACITY.
    [[nodiscard]] ::fixy::Stale<uint32_t> size() const CRUCIBLE_NO_THREAD_SAFETY {
        const uint64_t tail_seen = tail.get();
        const uint64_t live = head.get() - tail_seen;
        return ::fixy::Stale<uint32_t>::at_infinity(static_cast<uint32_t>(live < CAPACITY ? live : CAPACITY));
    }

    // Valid only once both threads have stopped. Every counter here moves
    // backwards, which the monotonicity contract otherwise forbids, so the
    // caller must have joined the producer and the consumer first.
    void reset() CRUCIBLE_NO_THREAD_SAFETY { reset_at(MetaIndex{0}); }

    // reset with each counter at the index: the log is empty, and the next
    // record takes the index.  The same condition holds.  The copy of the head
    // of the producer takes the index too, so the next publish finds the head
    // and the copy equal.  A test puts the counters near the end of the
    // buffer, or near the wrap of 32 bits, with it.
    void reset_at(MetaIndex position) CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(position.is_valid());
        head.reset_under_quiescence(position.raw());
        tail.reset_under_quiescence(position.raw());
        cached_tail_.reset_under_quiescence(position.raw());
        own_head_.reset_under_quiescence(position.raw());
        CRUCIBLE_POST(0, head.get() == position.raw());
        CRUCIBLE_POST(0, tail.get() == position.raw());
        CRUCIBLE_POST(0, cached_tail_.get() == position.raw());
        CRUCIBLE_POST(0, own_head_.get() == position.raw());
    }

private:
    // The proofs of the writers of the two counters.  Each door of a side
    // makes the proof of its side: the proven append after the record proof
    // of its caller, and each checked publish, which still compares the
    // counter with the value of the writer, so a second writer ends the
    // process at the next publish of the first one.
    [[nodiscard]] static constexpr head_writer prove_head_writer_() noexcept { return {}; }

    [[nodiscard]] static constexpr tail_writer prove_tail_writer_() noexcept { return {}; }

    // Writes the run of records at the head, and gives the index of its first
    // record, or no value if the buffer is full or the run is empty.  The two
    // appends share this body and then publish the head past the run.  An
    // Option and not the index none, so the optimizer folds the test of each
    // append into the branch that wrote the run.
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::Option<MetaIndex> fill_run_(::fixy::View<const TensorMeta> metas)
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(metas.size() <= CAPACITY);
        // The count and the positions below have the width of a View count
        // (64 bits on each supported platform), so the test of the run and
        // the check of each window compare the same values, and the
        // optimizer removes the checks.  A violation of the precondition
        // ends the process, so the count is at most CAPACITY below, and the
        // window of the second run of a wrap fits.
        const uint64_t n = metas.size();
        [[assume(n <= CAPACITY)]];
        if (n == 0) [[unlikely]]
            return ::fixy::none;

        const uint64_t h = own_head_.get();

        // A private view that says there is room is always right, because the
        // real counter only ever frees more. Only the other answer needs the
        // cross-core read.
        if (h - cached_tail_.get() + n > CAPACITY) [[unlikely]] {
            cached_tail_.advance(tail.get());
            if (h - cached_tail_.get() + n > CAPACITY) [[unlikely]] {
                return ::fixy::none;
            }
        }

        const uint64_t start_pos = h & MASK;

        if (n <= CAPACITY - start_pos) [[likely]] {
            write_records_(start_pos, metas, 0, n);
        } else {
            // The run wraps the end of the buffer, so it splits in two.
            const uint64_t first_chunk = CAPACITY - start_pos;
            write_records_(start_pos, metas, 0, first_chunk);
            write_records_(0, metas, first_chunk, n - first_chunk);
        }

        // One record spans three cache lines, hence three prefetches. The
        // sizes, the strides and the data pointer start at the offsets 0, 64
        // and 128 of a record. Issuing them before the publishing store
        // rather than after gives them the whole of the caller's remaining
        // work to complete in. The prefetch itself has no bearing on when
        // the copy above becomes visible.  Each prefetch prepares a write, and
        // the locality 3 keeps the line in each level of the cache.
        {
            const TensorMeta& next_record = record_at_(h + n);
            constexpr ::fixy::PrefetchAccess write_access = ::fixy::PrefetchAccess::write;
            ::fixy::prefetch<write_access, 3>(next_record.sizes);
            ::fixy::prefetch<write_access, 3>(next_record.strides);
            ::fixy::prefetch<write_access, 3>(next_record.data_ptr);
        }

        const MetaIndex result{h};
        // The index is the one from before the publish. The one after it
        // would name the slot past the block just written, and every reader
        // would skip the block.
        CRUCIBLE_POST(result, result.raw() == h);
        return ::fixy::Option<MetaIndex>::some(result);
    }

    // The record in the slot of index: the index masked by MASK.  The mask
    // keeps the slot below CAPACITY, so the check of the slot folds away.
    [[nodiscard]] TensorMeta const& record_at_(uint64_t index) const noexcept {
        return records_.at(::fixy::Index<CAPACITY>::check(index & MASK).expect("a masked slot is below the capacity"));
    }

    // Copies the count records of src from src_at into the buffer from at,
    // through the frame door of the buffer.  The caller gives two windows that
    // fit, and the count of the buffer is in its type, so the optimizer
    // removes each check of a window.  The body of the frame inlines into
    // the append: a call of it takes its captures through the stack and puts
    // a stack canary on the hot path of each record.
    CRUCIBLE_INLINE void write_records_(uint64_t at, ::fixy::View<TensorMeta const> src, uint64_t src_at,
                                        uint64_t count) noexcept {
        records_.with_mut([&] [[gnu::always_inline]] (auto ring) noexcept {
            ::fixy::copy(ring.view().window(at, count).expect("the records fit in the buffer"),
                         src.window(src_at, count).expect("the records of the copy are records of the run"))
                .expect("the two windows of the copy hold the same count");
        });
    }

    // Copies the count records of src from src_at into dst from dst_at.  Each
    // caller gives two windows that fit.
    static void copy_records_(::fixy::View<TensorMeta> dst, uint32_t dst_at, ::fixy::View<TensorMeta const> src,
                              uint32_t src_at, uint32_t count) noexcept {
        ::fixy::copy(dst.window(dst_at, count).expect("the records fit in the destination of the copy"),
                     src.window(src_at, count).expect("the records of the copy are slots of the buffer"))
            .expect("the two windows of the copy hold the same count");
    }
};

// An append count that has already been checked against the capacity. Asking
// for more records than the buffer holds is not a transient failure that a
// retry clears: no state of the consumer can ever satisfy it, so the request
// is refused rather than looping. A caller that establishes the bound once at
// a boundary can carry this instead of re-checking at every layer.
using ValidMetaAppendCount = ::fixy::Refined<::fixy::bounded_above<MetaLog::CAPACITY>, uint32_t>;

[[nodiscard, gnu::const]] inline constexpr uint32_t make_meta_append_count(ValidMetaAppendCount raw) noexcept {
    return raw.value();
}

}  // namespace crucible
