#pragma once

// A single-producer single-consumer ring. The ring is never resized, and an
// append onto a full ring drops the entry rather than blocking the producer:
// the recording is re-taken on the next iteration, so a drop costs one
// iteration of trace, never a stall on the recorded thread.
//
// Single-producer is a requirement on the caller, not a property this class
// defends. try_append reads the head, writes the slot it names and then
// advances it, and those three steps are not one atomic operation. Two
// threads running them at once name the same slot, write over each other and
// advance the head once, so one entry is lost and the surviving one can be
// half of each. Nothing reports it.
//
// Vigil owns the enforcement. Its record_op and dispatch_op give the producer
// role to the first thread that arrives and end the process on the second,
// and Vigil::is_producer_thread() lets an adapter ask before it arrives. A
// caller that reaches this class through Vigil::ring() instead carries the
// obligation itself.
//
// Single-producer is also what makes the recorded op order reproducible. The
// order is the trace's identity: it fixes the region content hash, the memory
// plan and the replay order. An order that two threads interleaved is a
// different order on every run, so a multi-producer ring would not be a
// faster version of this one, it would be a ring that cannot support
// bit-exact replay.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/warden/Registry.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/_Post.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/_Stale.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/fixy/Hw.h>
#include <crucible/fixy/Dim.h>

namespace crucible::fixy::wrap {
using ::crucible::safety::AtomicMonotonic;
using ::crucible::safety::bounded_above;
using ::crucible::safety::FixedArray;
using ::crucible::safety::HotPath;
using ::crucible::safety::HotPathTier_v;
using ::crucible::safety::Monotonic;
using ::crucible::safety::Refined;
using ::crucible::safety::Stale;
using ::crucible::safety::Tagged;
}  // namespace crucible::fixy::wrap
namespace crucible::fixy::tags {
namespace vessel_trust = ::crucible::safety::vessel_trust;
}  // namespace crucible::fixy::tags

namespace crucible {

namespace op_flag {
inline constexpr uint8_t INFERENCE_MODE = 1 << 0;
inline constexpr uint8_t IS_MUTABLE = 1 << 1;
inline constexpr uint8_t PHASE_MASK = 0x3 << 2;
inline constexpr uint8_t PHASE_SHIFT = 2;
inline constexpr uint8_t TORCH_FUNCTION = 1 << 4;
inline constexpr uint8_t GRAD_ENABLED = 1 << 5;
inline constexpr uint8_t SCALAR4_TYPE_MASK = 0x3 << 6;
inline constexpr uint8_t SCALAR4_TYPE_SHIFT = 6;
}  // namespace op_flag

enum class TrainingPhase : uint8_t {
    FORWARD = 0,
    BACKWARD = 1,
    OPTIMIZER = 2,
    OTHER = 3,
};

// How to read back an inline scalar slot. Every scalar is stored as an int64
// bit pattern, and a double bit-cast to int64 is indistinguishable from the
// integer holding the same bits, so the tag is the only thing that recovers
// the original type. Content hashing covers the bit pattern alone and ignores
// the tag.
enum class ScalarType2 : uint8_t {
    INT = 0,  // int64_t, sign-extended from any narrower integer
    FLOAT = 1,  // double, recovered with std::bit_cast
    BOOL = 2,  // canonicalised to 0 or 1 when recorded
    ENUM = 3,  // the underlying integer of an enum class
};

namespace tracering_hw {

namespace fh = ::crucible::fixy::hw;
namespace fgh = ::crucible::fixy::grant::hw;

// One source of truth: this value parameterises both the grant tag below and
// the third argument of every prefetch builtin in try_append, so editing it
// edits the emitted instruction rather than a description of it.
inline constexpr int kPrefetchLocality = 3;

using ActiveCacheGrant = fgh::cache<fh::CacheOp::Prefetch, kPrefetchLocality>;

static_assert(::crucible::fixy::grant::IsGrantTag<ActiveCacheGrant>,
              "the active cache grant must be a well-formed grant tag");
static_assert(::crucible::fixy::grant::which_dim_v<ActiveCacheGrant>
                  == ::crucible::fixy::dim::DimensionAxis::HwInstruction,
              "grant::hw::cache routes to the HwInstruction axis");

static_assert(kPrefetchLocality >= 0 && kPrefetchLocality <= 3, "prefetch locality must be in [0, 3], the "
                                                                "__builtin_prefetch third-argument domain");

}  // namespace tracering_hw

// 2 MB alignment so the allocation lands on a PMD-aligned region and the
// huge-page advice is not rejected.
struct alignas(crucible::warden::kHugePageBytes) CRUCIBLE_OWNER TraceRing {
    // Entry is bit-copied into the trace file, so its layout is a persisted
    // format, not an implementation detail: reordering or resizing a field
    // changes what readers of an existing trace see.
    struct alignas(64) Entry {
        SchemaHash schema_hash;
        ShapeHash shape_hash;
        uint16_t num_inputs = 0;
        uint16_t num_outputs = 0;
        uint16_t num_scalar_args = 0;  // the total count, of which only the first 5 are stored inline
        uint8_t scalar_types = 0;  // a two-bit tag for each of slots 0 to 3, with slot 4 in op_flags
        uint8_t op_flags = 0;
        // Zero-initialised so the content hash never sees indeterminate bytes.
        ::crucible::fixy::wrap::FixedArray<int64_t, 5> scalar_values{};

        [[nodiscard, gnu::pure]] ScalarType2 get_scalar_type(uint32_t i) const noexcept
            pre(::crucible::decide::in_range<uint32_t>(i, 0u, 4u)) {
            if (i < 4) {
                return static_cast<ScalarType2>((scalar_types >> (i * 2)) & 0x3);
            }
            return static_cast<ScalarType2>((op_flags & op_flag::SCALAR4_TYPE_MASK) >> op_flag::SCALAR4_TYPE_SHIFT);
        }

        void set_scalar_type(uint32_t i, ScalarType2 t) noexcept
            pre(::crucible::decide::in_range<uint32_t>(i, 0u, 4u)) {
            const uint8_t bits = static_cast<uint8_t>(t) & 0x3;
            if (i < 4) {
                const uint8_t shift = static_cast<uint8_t>(i * 2);
                scalar_types = static_cast<uint8_t>((scalar_types & ~(uint8_t{0x3} << shift)) | (bits << shift));
            } else {
                op_flags = static_cast<uint8_t>((op_flags & ~op_flag::SCALAR4_TYPE_MASK)
                                                | (bits << op_flag::SCALAR4_TYPE_SHIFT));
            }
        }
    };

    static_assert(sizeof(Entry) == 64, "Entry must be exactly one cache line");
    static_assert(alignof(Entry) == 64);
    CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Entry);

    // An Entry built straight from foreign-runtime input carries the first
    // tag and no recording entry point accepts it. Validating it, or building
    // it internally where every field is certified by construction, produces
    // the second tag. Tagging the pointer rather than the Entry keeps the
    // 64-byte payload where the caller put it.
    using FromPytorchEntryPtr =
        crucible::fixy::wrap::Tagged<const Entry*, crucible::fixy::tags::vessel_trust::FromPytorch>;
    using ValidatedEntryPtr = crucible::fixy::wrap::Tagged<const Entry*, crucible::fixy::tags::vessel_trust::Validated>;

    static constexpr uint32_t CAPACITY = 1u << 16;
    static constexpr uint32_t MASK = CAPACITY - 1;
    static_assert((CAPACITY & MASK) == 0, "CAPACITY must be a power of two");

    // head and tail are each written by one thread and read by the other, so
    // they sit on separate cache lines: sharing one line would make every
    // producer write invalidate the consumer's copy and back again. advance()
    // publishes with release and get() reads with acquire, which is the edge
    // that makes the slot writes visible across the pair. A thread reading its
    // own counter uses peek_relaxed and needs no ordering.
    alignas(64) crucible::fixy::wrap::AtomicMonotonic<uint64_t> head{0};

    alignas(64) crucible::fixy::wrap::AtomicMonotonic<uint64_t> tail{0};

    // The consumer is the only party that reads or writes this, so drains
    // track their position here and publish to tail only once the copies are
    // complete.
    //
    // It does not share a line with tail, although sharing one would be
    // harmless: AtomicMonotonic carries its own alignas(64) and is 64 bytes
    // wide, so tail fills bytes 64 through 127 by itself and this field
    // starts the line after it. The producer never touches either, so the
    // extra line costs a second consumer-private fetch and no invalidation.
    uint64_t consumer_tail_ = 0;

    // The producer's private view of tail, on its own line. Packing it with
    // the consumer-written counters above would make every producer refresh
    // invalidate the consumer's hot line. The 56 bytes that follow are not
    // paid for by this alignment: entries[] starts at the next 64-byte
    // boundary regardless, so dropping the alignment moves the padding rather
    // than reclaiming it.
    //
    // A stale value here is safe because it can only under-report free space,
    // which costs a real reload of tail and never an overwrite of a slot the
    // consumer has not read. Monotonic enforces the direction: tail only ever
    // advances, so an observation that moved backwards is a lost acquire.
    alignas(64) crucible::fixy::wrap::Monotonic<uint64_t> cached_tail_{0};

    alignas(64) Entry entries[CAPACITY]{};
    MetaIndex meta_starts[CAPACITY]{};
    ScopeHash scope_hashes[CAPACITY]{};
    CallsiteHash callsite_hashes[CAPACITY]{};

    TraceRing() noexcept {
        crucible::warden::register_hot_region(this, sizeof(*this),
                                              /*huge=*/true, "TraceRing");
    }
    ~TraceRing() { crucible::warden::unregister_hot_region(this); }
    TraceRing(const TraceRing&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
    TraceRing& operator=(const TraceRing&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
    TraceRing(TraceRing&&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
    TraceRing& operator=(TraceRing&&) = delete("SPSC ring is pinned to a producer/consumer thread pair");

    // The thread-safety analysis is suppressed throughout: this thread is the
    // sole writer of head and of the slot it is about to fill, which the
    // analysis cannot express.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE bool
    try_append(const Entry& e, MetaIndex meta_start = MetaIndex::none(), ScopeHash scope_hash = {},
               CallsiteHash callsite_hash = {}) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        const uint64_t h = head.peek_relaxed();

        // Refreshing from the consumer's atomic is the slow path, taken only
        // once the private view says the ring is full.
        if (h - cached_tail_.get() >= CAPACITY) [[unlikely]] {
            cached_tail_.advance(tail.get());
            if (h - cached_tail_.get() >= CAPACITY) [[unlikely]]
                return false;
        }

        const uint32_t slot = static_cast<uint32_t>(h) & MASK;
        [[assume(slot < CAPACITY)]];
        entries[slot] = e;
        meta_starts[slot] = meta_start;
        scope_hashes[slot] = scope_hash;
        callsite_hashes[slot] = callsite_hash;

        // Warm the destination of the following append while the caller is
        // still busy with its own post-append work.
        {
            const uint32_t next_slot = (slot + 1u) & MASK;
            __builtin_prefetch(&entries[next_slot], 1, tracering_hw::kPrefetchLocality);
            __builtin_prefetch(&meta_starts[next_slot], 1, tracering_hw::kPrefetchLocality);
            __builtin_prefetch(&scope_hashes[next_slot], 1, tracering_hw::kPrefetchLocality);
            __builtin_prefetch(&callsite_hashes[next_slot], 1, tracering_hw::kPrefetchLocality);
        }

        // This release publishes all four slot writes above to the consumer.
        head.advance(h + 1);
        return true;
    }

    // Same body, with the tier declared in the return type so a consumer that
    // demands a hot-tier producer can be checked at compile time. A change to
    // try_append that made it unfit for the hot path would have to weaken the
    // tier here, and every fenced consumer would then reject the call.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot, gnu::flatten]]
    CRUCIBLE_INLINE crucible::fixy::wrap::HotPath<crucible::fixy::wrap::HotPathTier_v::Hot, bool>
    try_append_pinned(const Entry& e, MetaIndex meta_start = MetaIndex::none(), ScopeHash scope_hash = {},
                      CallsiteHash callsite_hash = {}) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return crucible::fixy::wrap::HotPath<crucible::fixy::wrap::HotPathTier_v::Hot, bool>{
            try_append(e, meta_start, scope_hash, callsite_hash)};
    }

    // Preferred at new call sites: appending touches memory only, so the
    // caller's effect row must be empty, and a caller that allocates, blocks,
    // performs I/O, or runs at init or test time is rejected here rather than
    // discovered later. The plain try_append remains for callers that cannot
    // name their row.
    template <typename CallerRow = ::crucible::effects::Row<>>
        requires ::crucible::effects::IsPure<CallerRow>
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE bool
    try_append_pure(const Entry& e, MetaIndex meta_start = MetaIndex::none(), ScopeHash scope_hash = {},
                    CallsiteHash callsite_hash = {}) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return try_append(e, meta_start, scope_hash, callsite_hash);
    }

    // The three parallel-array outputs are optional and may each be null.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot]] uint32_t
    drain(Entry* out, uint32_t max_count, MetaIndex* out_meta_starts = nullptr, ScopeHash* out_scope_hashes = nullptr,
          CallsiteHash* out_callsite_hashes = nullptr) noexcept
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::in_range<std::uint32_t>(max_count, std::uint32_t{0},
                                                                                  CAPACITY))
            pre(::crucible::decide::valid_span(max_count, out)) {
        if (max_count == 0) [[unlikely]]
            return 0;

        const uint64_t h = head.get();
        const uint64_t t = consumer_tail_;

        const uint32_t available = static_cast<uint32_t>(h - t);
        const uint32_t count = std::min(available, max_count);
        if (count == 0) [[unlikely]]
            return 0;

        // A run that wraps the end of the ring splits into at most two.
        const uint32_t start = static_cast<uint32_t>(t) & MASK;
        const uint32_t first = std::min(count, CAPACITY - start);
        const uint32_t second = count - first;

        std::memcpy(out, &entries[start], first * sizeof(Entry));
        if (out_meta_starts) std::memcpy(out_meta_starts, &meta_starts[start], first * sizeof(MetaIndex));
        if (out_scope_hashes) std::memcpy(out_scope_hashes, &scope_hashes[start], first * sizeof(ScopeHash));
        if (out_callsite_hashes)
            std::memcpy(out_callsite_hashes, &callsite_hashes[start], first * sizeof(CallsiteHash));

        if (second > 0) [[unlikely]] {
            std::memcpy(out + first, &entries[0], second * sizeof(Entry));
            if (out_meta_starts) std::memcpy(out_meta_starts + first, &meta_starts[0], second * sizeof(MetaIndex));
            if (out_scope_hashes) std::memcpy(out_scope_hashes + first, &scope_hashes[0], second * sizeof(ScopeHash));
            if (out_callsite_hashes)
                std::memcpy(out_callsite_hashes + first, &callsite_hashes[0], second * sizeof(CallsiteHash));
        }

        // Publishing the new tail declares slots [t, t + count) free. The
        // producer only reads tail after finding the ring full, so it sees
        // this before it can overwrite any of them.
        const uint64_t next_tail = t + count;
        consumer_tail_ = next_tail;
        tail.advance(next_tail);
        CRUCIBLE_POST(count, count <= max_count);
        return count;
    }

    // Warm rather than hot: the caller may allocate and copies large buffers,
    // so a consumer fenced to the hot tier rejects a value from here.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot]]
    crucible::fixy::wrap::HotPath<crucible::fixy::wrap::HotPathTier_v::Warm, uint32_t>
    drain_pinned(Entry* out, uint32_t max_count, MetaIndex* out_meta_starts = nullptr,
                 ScopeHash* out_scope_hashes = nullptr, CallsiteHash* out_callsite_hashes = nullptr) noexcept
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::in_range<std::uint32_t>(max_count, std::uint32_t{0},
                                                                                  CAPACITY))
            pre(::crucible::decide::valid_span(max_count, out)) {
        return crucible::fixy::wrap::HotPath<crucible::fixy::wrap::HotPathTier_v::Warm, uint32_t>{
            drain(out, max_count, out_meta_starts, out_scope_hashes, out_callsite_hashes)};
    }

    // The consumer counterpart of try_append_pure. A drain touches memory
    // only, so the caller's row must be empty. The thread that owns the
    // consumer side declares its own context separately from this call.
    template <typename CallerRow = ::crucible::effects::Row<>>
        requires ::crucible::effects::IsPure<CallerRow>
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot]] uint32_t
    drain_pure(Entry* out, uint32_t max_count, MetaIndex* out_meta_starts = nullptr,
               ScopeHash* out_scope_hashes = nullptr, CallsiteHash* out_callsite_hashes = nullptr) noexcept
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::in_range<std::uint32_t>(max_count, std::uint32_t{0},
                                                                                  CAPACITY))
            pre(::crucible::decide::valid_span(max_count, out)) {
        return drain(out, max_count, out_meta_starts, out_scope_hashes, out_callsite_hashes);
    }

    // drain with every output buffer mandatory. The caller gives up the option
    // of skipping an array, and in exchange the copies below carry no
    // per-array null test, and a batch consumer can rely on every popped entry
    // having all four arrays populated.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot]] uint32_t
    try_pop_batch(Entry* out_entries, MetaIndex* out_meta_starts, ScopeHash* out_scope_hashes,
                  CallsiteHash* out_callsite_hashes, uint32_t max_count) noexcept
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::in_range<std::uint32_t>(max_count, std::uint32_t{0},
                                                                                  CAPACITY))
            pre(max_count == 0
                || (out_entries != nullptr && out_meta_starts != nullptr && out_scope_hashes != nullptr
                    && out_callsite_hashes != nullptr)) {
        if (max_count == 0) [[unlikely]]
            return 0;

        const uint64_t h = head.get();
        const uint64_t t = consumer_tail_;

        const uint32_t available = static_cast<uint32_t>(h - t);
        const uint32_t count = std::min(available, max_count);
        if (count == 0) [[unlikely]]
            return 0;

        // A run that wraps the end of the ring splits into at most two.
        const uint32_t start = static_cast<uint32_t>(t) & MASK;
        const uint32_t first = std::min(count, CAPACITY - start);
        const uint32_t second = count - first;

        std::memcpy(out_entries, &entries[start], first * sizeof(Entry));
        std::memcpy(out_meta_starts, &meta_starts[start], first * sizeof(MetaIndex));
        std::memcpy(out_scope_hashes, &scope_hashes[start], first * sizeof(ScopeHash));
        std::memcpy(out_callsite_hashes, &callsite_hashes[start], first * sizeof(CallsiteHash));

        if (second > 0) [[unlikely]] {
            std::memcpy(out_entries + first, &entries[0], second * sizeof(Entry));
            std::memcpy(out_meta_starts + first, &meta_starts[0], second * sizeof(MetaIndex));
            std::memcpy(out_scope_hashes + first, &scope_hashes[0], second * sizeof(ScopeHash));
            std::memcpy(out_callsite_hashes + first, &callsite_hashes[0], second * sizeof(CallsiteHash));
        }

        const uint64_t next_tail = t + count;
        consumer_tail_ = next_tail;
        tail.advance(next_tail);
        CRUCIBLE_POST(count, count <= max_count);
        return count;
    }

    // head and tail are read at different instants while both threads run, so
    // the difference is a snapshot of a value that was never simultaneously
    // true. The return type says so and forces the caller to acknowledge it.
    [[nodiscard, gnu::pure]] crucible::fixy::wrap::Stale<uint32_t> size() const noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return crucible::fixy::wrap::Stale<uint32_t>::at_infinity(static_cast<uint32_t>(head.get() - tail.get()));
    }

    // The acquire in get() means the returned bound also implies visibility of
    // every entry produced up to it, which is what makes it usable as a
    // catch-up target.
    [[nodiscard, gnu::pure]] uint64_t total_produced() const noexcept CRUCIBLE_NO_THREAD_SAFETY { return head.get(); }

    // Valid only once both threads have stopped. Every counter here moves
    // backwards, which the monotonicity contract otherwise forbids, so the
    // caller must have joined the producer and the consumer first.
    void reset() noexcept CRUCIBLE_NO_THREAD_SAFETY post(head.get() == 0) post(tail.get() == 0)
        post(consumer_tail_ == 0) post(cached_tail_.get() == 0) {
        head.reset_under_quiescence();
        tail.reset_under_quiescence();
        consumer_tail_ = 0;
        cached_tail_.reset_under_quiescence();
    }
};

static_assert(sizeof(TraceRing) >= (5u * 1024u * 1024u) && sizeof(TraceRing) <= (6u * 1024u * 1024u),
              "TraceRing footprint must stay inside the 5-6 MB envelope");

// A drain count that has already been checked against the ring capacity, for
// callers that want to establish the bound once and carry the witness. Asking
// for more than the ring holds is silently clamped inside the drain, which
// hides both a caller that never bounded a value it read from outside and a
// caller whose output buffer is smaller than the count it passed.
using ValidDrainCount =
    ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::bounded_above<TraceRing::CAPACITY>, uint32_t>;

[[nodiscard, gnu::const]] inline constexpr uint32_t make_drain_count(ValidDrainCount raw) noexcept {
    return raw.value();
}

// Certifies an Entry whose every field was built internally and is correct by
// construction. An Entry carrying data that crossed a foreign-runtime boundary
// is certified by validating it, never by this, so a call to this from an
// adapter is a way of skipping the checks.
[[nodiscard]] CRUCIBLE_INLINE TraceRing::ValidatedEntryPtr vouch(const TraceRing::Entry& e
                                                                 CRUCIBLE_LIFETIMEBOUND) noexcept {
    return TraceRing::ValidatedEntryPtr{&e};
}

}  // namespace crucible
