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
// Two publishes
//   The producer keeps its own copy of the head on its private line, and it
//   reads its position there.  The consumer polls the head, so a load of the
//   head by the producer can wait for a transfer of its line.  commit checks
//   the head against the copy at each publish, so a second producer ends the
//   process at the next publish of the first one.  That check loads the head
//   one time.  commit_proven takes the proof of the record path (RecordProof
//   of crucible/ForegroundCtx.h), which only the producer claim of the Vigils
//   makes.  The type proves the one writer, so that publish loads nothing
//   from the head.  The Vigil records each op with the proof.  A test and a
//   bench drive a ring of their own with the checked form.
//
// Single-producer is also what makes the recorded op order reproducible. The
// order is the trace's identity: it fixes the region content hash, the memory
// plan and the replay order. An order that two threads interleaved is a
// different order on every run, so a multi-producer ring would not be a
// faster version of this one, it would be a ring that cannot support
// bit-exact replay.
//
// A caller that has work on several threads gives that work one producer
// rather than giving this ring several. The PyTorch adapter is the worked
// example: a backward pass runs on the calling thread and on one worker
// thread for each accelerator, so a recording session holds the autograd
// engine on the calling thread and the whole pass reaches this ring in the
// order that thread produced. Refer to the serialisation note in
// vessel/torch/crucible_native.py.

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <crucible/ForegroundCtx.h>
#include <crucible/Types.h>
#include <crucible/warden/Registry.h>

#include <fixy/Aliases.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/atoms/Hw.h>
#include <fixy/os/Pages.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

namespace crucible {

// The schema hash of the closing entry of a nest whose op did not reach the
// Vigil: the op raised, or the adapter refused its record, after an op inside
// its nest reached the Vigil (OpNest::is_unrecorded).  The adapter gives the
// entry at the depth of the op, with no record, so a reader of the post-order
// stream gives the ops of the nest to it and not to the next op.  The op did
// not finish its computation in the trace, so the build refuses each region
// that holds the entry, and the region stays eager.  The value is the FNV-1a
// hash of the name "crucible::unrecorded_nest", the fold of the schema hash of
// each name of the vessel, and no operator of PyTorch has that name.
inline constexpr SchemaHash kUnrecordedNestSchema{0xb984e8ae07060156ULL};

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

// How to read back an inline scalar slot. Every scalar is stored as an int64
// bit pattern, and a double bit-cast to int64 is indistinguishable from the
// integer holding the same bits, so the tag is the only thing that recovers
// the original type.  The content hash of a region folds the tag of each
// slot beside its bits.
//
// The tag is the tag of the value as PyTorch boxes it, because the boxed
// fallback reads a boxed value and the unboxed kernels must write the same
// entry.  An enumerator, such as a ScalarType or a MemoryFormat, boxes into an
// integer, so it takes INT.  An argument that boxes into None, such as an
// absent optional, takes one slot with the value zero and the tag NONE.  So
// clamp with only min and clamp with only max give two entries.
enum class ScalarType2 : uint8_t {
    INT = 0,  // int64_t, sign-extended from any narrower integer, or an enumerator
    FLOAT = 1,  // double, recovered with std::bit_cast
    BOOL = 2,  // canonicalised to 0 or 1 when recorded
    NONE = 3,  // an argument that boxes into None; the value is zero
};

namespace tracering_hw {

// This value is the locality of each prefetch in try_append.  A change of the
// value changes the emitted instruction, and not a description of it.
inline constexpr int kPrefetchLocality = 3;

// The widest instruction class the ring issues: loads, stores and a prefetch
// hint, all scalar.  A change that adds a timestamp read or a privileged
// instruction to the append must raise this tier, and the hot path refuses
// every tier from the timestamp read upwards.
using InstructionTier = ::fixy::atom::hw::scalar;

}  // namespace tracering_hw

namespace detail {

// The run of the frame of one output of a drain: a ViewMut of elements T,
// of each brand and each extent.
template <class Run, class T>
concept DrainRunOf = std::same_as<Run, ::fixy::ViewMut<T, typename Run::brand_type, Run::extent>>;

// True when the runs are a prefix of the four parallel outputs of a drain, in
// their order: the meta starts, the scope hashes, the callsite hashes and the
// depths.
template <class... Runs>
inline constexpr bool is_drain_prefix_v = false;
template <>
inline constexpr bool is_drain_prefix_v<> = true;
template <class Metas>
inline constexpr bool is_drain_prefix_v<Metas> = DrainRunOf<Metas, MetaIndex>;
template <class Metas, class Scopes>
inline constexpr bool is_drain_prefix_v<Metas, Scopes> = DrainRunOf<Metas, MetaIndex> && DrainRunOf<Scopes, ScopeHash>;
template <class Metas, class Scopes, class Callsites>
inline constexpr bool is_drain_prefix_v<Metas, Scopes, Callsites> =
    is_drain_prefix_v<Metas, Scopes> && DrainRunOf<Callsites, CallsiteHash>;
template <class Metas, class Scopes, class Callsites, class Depths>
inline constexpr bool is_drain_prefix_v<Metas, Scopes, Callsites, Depths> =
    is_drain_prefix_v<Metas, Scopes, Callsites> && DrainRunOf<Depths, OpDepth>;

}  // namespace detail

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
        uint8_t num_scalar_args = 0;  // the stored slots, at most 5; the overflow digest is the last one
        // The word of the data slots (DataSlots::bits()): the slots below the
        // count that hold a data scalar.  A DataSlots is not trivially
        // copyable, so the entry keeps its word.
        uint8_t data_slot_bits = 0;
        uint8_t scalar_types = 0;  // a two-bit tag for each of slots 0 to 3, with slot 4 in op_flags
        uint8_t op_flags = 0;
        // Zero-initialised so the content hash never sees indeterminate bytes.
        ::fixy::FixedArray<int64_t, 5> scalar_values{};

        [[nodiscard, gnu::pure]] ScalarType2 get_scalar_type(uint32_t i) const noexcept {
            CRUCIBLE_PRE(::foundation::decide::in_range<uint32_t>(i, 0u, 4u));
            if (i < 4) {
                return ::fixy::enum_from_field<ScalarType2, kScalarTypeBits>(scalar_types >> (i * 2));
            }
            return ::fixy::enum_from_field<ScalarType2, kScalarTypeBits>(op_flags >> op_flag::SCALAR4_TYPE_SHIFT);
        }

        void set_scalar_type(uint32_t i, ScalarType2 t) noexcept {
            CRUCIBLE_PRE(::foundation::decide::in_range<uint32_t>(i, 0u, 4u));
            const uint8_t bits = static_cast<uint8_t>(t) & 0x3;
            if (i < 4) {
                const uint8_t shift = static_cast<uint8_t>(i * 2);
                scalar_types = static_cast<uint8_t>((scalar_types & ~(uint8_t{0x3} << shift)) | (bits << shift));
            } else {
                op_flags = static_cast<uint8_t>((op_flags & ~op_flag::SCALAR4_TYPE_MASK)
                                                | (bits << op_flag::SCALAR4_TYPE_SHIFT));
            }
            CRUCIBLE_POST(0, get_scalar_type(i) == t);
        }

        // The tags of the five slots, two bits for each slot, slot 0 in the
        // low bits.  Slots 0 to 3 are in scalar_types, and slot 4 is in the
        // two high bits of op_flags.
        [[nodiscard, gnu::pure]] constexpr uint16_t packed_scalar_types() const noexcept {
            const auto slot4 =
                static_cast<uint16_t>((op_flags & op_flag::SCALAR4_TYPE_MASK) >> op_flag::SCALAR4_TYPE_SHIFT);
            return static_cast<uint16_t>(scalar_types | (slot4 << 8));
        }

        // Writes the tags of the five slots from their packed form, and keeps
        // the other bits of op_flags.
        constexpr void set_packed_scalar_types(uint16_t packed) noexcept {
            scalar_types = static_cast<uint8_t>(packed & 0xFF);
            const auto slot4 = static_cast<uint8_t>((packed >> 8) & 0x3);
            op_flags =
                static_cast<uint8_t>((op_flags & ~op_flag::SCALAR4_TYPE_MASK) | (slot4 << op_flag::SCALAR4_TYPE_SHIFT));
        }

        // The data slots of the entry, from their word, or no value when the
        // word holds a bit past the inline slots.  The validator of an entry
        // from outside refuses such a word (entry_is_well_formed of
        // crucible/ValidatedOp.h), and set_data_slots writes none.
        [[nodiscard, gnu::pure]] constexpr ::fixy::Option<DataSlots> data_slots() const noexcept {
            return DataSlots::from_bits(data_slot_bits);
        }

        constexpr void set_data_slots(const DataSlots& data) noexcept { data_slot_bits = data.bits(); }

        // The tags and the data slots of the stored slots, in the word that
        // an op of a region keeps.  The bits of a slot past the count go.  An
        // entry of the ring passed the checks of its recording path, so its
        // word of the data slots names only inline slots.
        [[nodiscard, gnu::pure]] constexpr ScalarSlotKinds scalar_kinds() const noexcept {
            const ScalarCount stored = ScalarCount::check(::fixy::min(uint32_t{num_scalar_args}, kScalarSlots))
                                           .expect("the minimum is a count");
            const DataSlots data = data_slots().expect("an entry of the ring holds no data slot past the inline slots");
            return ScalarSlotKinds::of(packed_scalar_types(), data).first(stored);
        }
    };

    static_assert(sizeof(Entry) == 64, "Entry must be exactly one cache line");
    static_assert(alignof(Entry) == 64);
    static_assert(std::is_standard_layout_v<Entry>);
    static_assert(offsetof(Entry, schema_hash) == 0 && offsetof(Entry, shape_hash) == 8
                      && offsetof(Entry, num_inputs) == 16 && offsetof(Entry, num_outputs) == 18
                      && offsetof(Entry, num_scalar_args) == 20 && offsetof(Entry, data_slot_bits) == 21
                      && offsetof(Entry, scalar_types) == 22 && offsetof(Entry, op_flags) == 23
                      && offsetof(Entry, scalar_values) == 24,
                  "Entry is bit-copied into the trace file, so each field offset is part of a persisted format");
    CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Entry);

    // An Entry built straight from foreign-runtime input carries the first
    // tag and no recording entry point accepts it. Validating it, or building
    // it internally where every field is certified by construction, produces
    // the second tag. Tagging the pointer rather than the Entry keeps the
    // 64-byte payload where the caller put it.
    using FromPytorchEntryPtr = ::fixy::Tagged<const Entry*, ::fixy::tags::vessel_trust::FromPytorch>;
    using ValidatedEntryPtr = ::fixy::Tagged<const Entry*, ::fixy::tags::vessel_trust::Validated>;

    static constexpr uint32_t CAPACITY = 1u << 16;
    static constexpr uint32_t MASK = CAPACITY - 1;
    static_assert((CAPACITY & MASK) == 0, "CAPACITY must be a power of two");

    // head and tail are each written by one thread and read by the other, so
    // they sit on separate cache lines: sharing one line would make every
    // producer write invalidate the consumer's copy and back again.  The
    // publish of the head (advance_sole_writer_from or
    // advance_sole_writer_by_proof) releases and get() acquires, which is the
    // edge that makes the slot writes visible across the pair.  The producer
    // reads its own position from own_head_ (the two publishes).
    //
    // The type of each counter names its writer (the writer of a counter of
    // fixy/Mutation.h).  The ring is the owner of each proof, so only the
    // doors of the ring publish a counter.
    struct HeadWriter final {};
    struct TailWriter final {};
    using head_writer = ::fixy::SoleWriterProof<TraceRing, HeadWriter>;
    using tail_writer = ::fixy::SoleWriterProof<TraceRing, TailWriter>;

    alignas(64)::fixy::AtomicMonotonic<uint64_t, head_writer> head =
        ::fixy::mint_atomic_monotonic<uint64_t, head_writer>(0);

    alignas(64)::fixy::AtomicMonotonic<uint64_t, tail_writer> tail =
        ::fixy::mint_atomic_monotonic<uint64_t, tail_writer>(0);

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
    alignas(64)::fixy::Monotonic<uint64_t> cached_tail_ = ::fixy::mint_monotonic<uint64_t>(0);
    // The copy of the head of the producer, on the same private line (the two
    // publishes).  It holds the value of the head after each publish.
    ::fixy::Monotonic<uint64_t> own_head_ = ::fixy::mint_monotonic<uint64_t>(0);

    alignas(64)::fixy::FixedArray<Entry, CAPACITY> entries{};
    ::fixy::FixedArray<MetaIndex, CAPACITY> meta_starts{};
    ::fixy::FixedArray<ScopeHash, CAPACITY> scope_hashes{};
    ::fixy::FixedArray<CallsiteHash, CAPACITY> callsite_hashes{};
    ::fixy::FixedArray<OpDepth, CAPACITY> depths{};

    // The slot of a counter of the ring: its low bits.  The mask keeps the
    // slot below CAPACITY, so the optimizer removes the check.
    using RingSlot = ::fixy::Index<CAPACITY>;
    [[nodiscard]] CRUCIBLE_INLINE static RingSlot mask_slot_(uint64_t counter) noexcept {
        return RingSlot::check(counter & MASK).expect("a masked counter of the ring is below CAPACITY");
    }

    TraceRing() noexcept {
        crucible::warden::register_hot_region(::fixy::pages::storage_of(*this), /*huge_hint=*/true);
    }
    ~TraceRing() { crucible::warden::unregister_hot_region(::fixy::pages::storage_of(*this)); }
    TraceRing(const TraceRing&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
    TraceRing& operator=(const TraceRing&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
    TraceRing(TraceRing&&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
    TraceRing& operator=(TraceRing&&) = delete("SPSC ring is pinned to a producer/consumer thread pair");

    // A free slot of the ring, which try_reserve found.  The token holds the
    // head at the reservation.  Only commit moves the head, so a token that
    // the producer drops leaves the ring as it was.  The token has no
    // destructor work, and it only moves, so one reservation fills one slot.
    //
    // The producer commits a token before it reserves again.  The ring has
    // one producer, so no other append can move the head between the two
    // calls.  A token that names an old head fails the step check of each
    // commit, in each build.
    class [[nodiscard]] Reservation {
    public:
        Reservation(const Reservation&) = delete("one reservation fills one slot of the ring");
        Reservation& operator=(const Reservation&) = delete("one reservation fills one slot of the ring");
        ~Reservation() = default;

        // The moves are those of the bytes.  The token only moves, so an
        // Option of a token takes the owning slot, which moves it.
        Reservation(Reservation&&) noexcept = default;
        Reservation& operator=(Reservation&&) noexcept = default;

    private:
        explicit constexpr Reservation(uint64_t reserved_head) noexcept : head_{reserved_head} {}
        friend struct TraceRing;

        uint64_t head_ = 0;
    };

    // The thread-safety analysis is suppressed throughout: this thread is the
    // sole writer of head and of the slot it is about to fill, which the
    // analysis cannot express.
    //
    // Reserves the slot at the head, or gives no token when the ring is full.
    // The ring does not change: the head moves in commit.
    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE ::fixy::Option<Reservation> try_reserve() noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        const uint64_t h = own_head_.get();

        // Refreshing from the consumer's atomic is the slow path, taken only
        // once the private view says the ring is full.
        if (h - cached_tail_.get() >= CAPACITY) [[unlikely]] {
            cached_tail_.advance(tail.get());
            if (h - cached_tail_.get() >= CAPACITY) [[unlikely]]
                return ::fixy::none;
        }
        return ::fixy::Option<Reservation>::some(Reservation{h});
    }

    // Appends one entry, or returns false when the ring is full.  The append
    // is a reservation and its commit.  A caller that names no depth records
    // an op that no recorded op runs.
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE bool
    try_append(const Entry& e, MetaIndex meta_start = MetaIndex::none(), ScopeHash scope_hash = {},
               CallsiteHash callsite_hash = {}, OpDepth depth = {}) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return try_reserve().match(
            [&](Reservation reserved) noexcept {
                commit(std::move(reserved), e, meta_start, scope_hash, callsite_hash, depth);
                return true;
            },
            [] noexcept { return false; });
    }

    // Writes the entry into the reserved slot, and moves the head past it.
    // The consumer then sees the entry.  Each argument has no default, so a
    // caller cannot drop a field of the slot.  The release of the publish
    // carries the five slot writes to the consumer, and its check loads the
    // head (the two publishes).
    [[gnu::hot]] CRUCIBLE_INLINE void commit(Reservation reserved, const Entry& e, MetaIndex meta_start,
                                             ScopeHash scope_hash, CallsiteHash callsite_hash, OpDepth depth) noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        const uint64_t h = fill_slot_(reserved, e, meta_start, scope_hash, callsite_hash, depth);
        head.advance_sole_writer_from(prove_head_writer_(), h, h + 1, own_head_);
    }

    // The commit of the producer of the Vigils, which holds the proof of the
    // record path.  The type proves the one writer, so the publish loads
    // nothing from the head (the two publishes).  A token that names an old
    // head fails the step check of the publish in each build.
    [[gnu::hot]] CRUCIBLE_INLINE void commit_proven(RecordProof const& /*proof*/, Reservation reserved, const Entry& e,
                                                    MetaIndex meta_start, ScopeHash scope_hash,
                                                    CallsiteHash callsite_hash, OpDepth depth) noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        const uint64_t h = fill_slot_(reserved, e, meta_start, scope_hash, callsite_hash, depth);
        head.advance_sole_writer_by_proof(prove_head_writer_(), own_head_, h + 1);
    }

    // The append of a pure caller.  The gate reads the row of the context of
    // the caller and its source.  A context that allocates, blocks, performs
    // I/O, or runs at init or test time fails the row test.  A context that
    // narrowed its row with in_row keeps its source, and the source of a test,
    // an init step or a background stage permits atoms, so it fails the
    // source test (CtxHasPureSource).  The plain try_append stays for a
    // caller that holds no context.
    template <::foundation::effects::IsExecCtx Ctx>
        requires ::fixy::IsPure<::foundation::effects::row_type_of_t<Ctx>>
              && ::foundation::effects::CtxHasPureSource<Ctx>
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE bool
    try_append_pure(Ctx const&, const Entry& e, MetaIndex meta_start = MetaIndex::none(), ScopeHash scope_hash = {},
                    CallsiteHash callsite_hash = {}, OpDepth depth = {}) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return try_append(e, meta_start, scope_hash, callsite_hash, depth);
    }

    // The ring array of the elements T: the entries, the meta starts, the
    // scope hashes, the callsite hashes or the depths.
    template <class T>
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::FixedArray<T, CAPACITY> const& slots_of_() const noexcept {
        if constexpr (std::is_same_v<T, Entry>) {
            return entries;
        } else if constexpr (std::is_same_v<T, MetaIndex>) {
            return meta_starts;
        } else if constexpr (std::is_same_v<T, ScopeHash>) {
            return scope_hashes;
        } else if constexpr (std::is_same_v<T, CallsiteHash>) {
            return callsite_hashes;
        } else {
            return depths;
        }
    }

    // Copies count slots of the ring array of T from start into the first
    // count elements of out.  The caller tests that the run does not wrap,
    // and the count fits out, so the optimizer removes each check of the
    // windows and of the copy.  The start and the count have the width of a
    // View count, so the tests of the caller and the checks of the windows
    // compare the same values.
    template <class T, class Brand, std::size_t Extent>
    CRUCIBLE_INLINE void copy_run_(::fixy::ViewMut<T, Brand, Extent> out, std::size_t start,
                                   std::size_t count) const noexcept {
        ::fixy::copy(::fixy::take_window(std::move(out), 0, count).expect("the count fits the output"),
                     slots_of_<T>().view().window(start, count).expect("the run fits the ring"))
            .expect("the two windows hold the same count");
    }

    // Copies a run of count slots that wraps the end of the ring into the
    // first count elements of out: first slots from start to the end of the
    // ring, then the rest from the first slot of the ring.  Each size comes
    // from count and first, so the optimizer removes each check, as for
    // copy_run_.
    template <class T, class Brand, std::size_t Extent>
    CRUCIBLE_INLINE void copy_wrapped_run_(::fixy::ViewMut<T, Brand, Extent> out, std::size_t start, std::size_t first,
                                           std::size_t count) const noexcept {
        const std::size_t second = count - first;
        auto [front, back] =
            ::fixy::split_at(::fixy::take_window(std::move(out), 0, count).expect("the count fits the output"), first)
                .expect("the first run fits the drained count");
        ::fixy::FixedArray<T, CAPACITY> const& slots = slots_of_<T>();
        ::fixy::copy(std::move(front), slots.view().window(start, first).expect("the run fits the ring"))
            .expect("the two windows hold the same count");
        ::fixy::copy(std::move(back), slots.view().window(0, second).expect("the run fits the ring"))
            .expect("the two windows hold the same count");
    }

    // The body of the drains: copies at most out.size() entries into out,
    // and the same count of slots into each parallel output, then publishes
    // the new tail.  Each parallel output holds out.size() elements, and the
    // requires clause of each caller orders the parallel outputs.
    template <class Brand, std::size_t Extent, class... Parallel>
    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE uint32_t drain_into_(::fixy::ViewMut<Entry, Brand, Extent> out,
                                                                 Parallel... parallel) noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        const std::size_t room = out.size();
        CRUCIBLE_PRE(((parallel.size() == room) && ...));
        if (room == 0) [[unlikely]]
            return 0;

        const uint64_t h = head.get();
        const uint64_t t = consumer_tail_;

        // The clamp is an assignment and a conditional assignment.  GCC 16
        // at -O1 with -fsanitize=bounds-strict does not keep the bound
        // count <= room of a conditional expression.  The count and the start
        // have the width of a View count, for the reason of copy_run_.
        std::size_t count = static_cast<uint32_t>(h - t);
        if (room < count) count = room;
        if (count == 0) [[unlikely]]
            return 0;

        // A run that wraps the end of the ring splits into two.  The one
        // test below proves each window of the two arms.
        const std::size_t start = t & MASK;
        if (count <= CAPACITY - start) [[likely]] {
            copy_run_(std::move(out), start, count);
            (copy_run_(std::move(parallel), start, count), ...);
        } else {
            const std::size_t first = CAPACITY - start;
            const std::size_t second = count - first;
            // first is at least one, so the second run is shorter than the
            // count, and the bound of the count bounds the run of the ring.
            [[assume(second < count)]];
            copy_wrapped_run_(std::move(out), start, first, count);
            (copy_wrapped_run_(std::move(parallel), start, first, count), ...);
        }

        // Publishing the new tail declares slots [t, t + count) free. The
        // producer only reads tail after finding the ring full, so it sees
        // this before it can overwrite any of them.
        const uint64_t next_tail = t + count;
        consumer_tail_ = next_tail;
        // t is the consumer's own copy of its last store to tail.
        tail.advance_sole_writer_from(prove_tail_writer_(), t, next_tail);
        CRUCIBLE_POST(count, count <= room);
        // The count came from a 32-bit difference, so it fits the result.
        return static_cast<uint32_t>(count);
    }

    // Drains at most out.size() entries into out.  The parallel outputs are
    // a prefix of the four parallel arrays, in their order: the meta starts,
    // the scope hashes, the callsite hashes and the depths.  Each one holds
    // out.size() elements.  Each output is the run of a frame, which the drain
    // takes by move, so the drain is the one writer of each run.
    template <class Brand, std::size_t Extent, class... Parallel>
        requires detail::is_drain_prefix_v<Parallel...>
    [[nodiscard, gnu::hot]] uint32_t drain(::fixy::ViewMut<Entry, Brand, Extent> out, Parallel... parallel) noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        return drain_into_(std::move(out), std::move(parallel)...);
    }

    // The consumer counterpart of try_append_pure, with the same gate: the
    // row of the context of the caller is empty, and its source permits no
    // atom.  A background stage holds a source that permits atoms, so it
    // drains with try_pop_batch, and not here.
    template <::foundation::effects::IsExecCtx Ctx, class Brand, std::size_t Extent, class... Parallel>
        requires ::fixy::IsPure<::foundation::effects::row_type_of_t<Ctx>>
              && ::foundation::effects::CtxHasPureSource<Ctx> && detail::is_drain_prefix_v<Parallel...>
    [[nodiscard, gnu::hot]] uint32_t drain_pure(Ctx const&, ::fixy::ViewMut<Entry, Brand, Extent> out,
                                                Parallel... parallel) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return drain(std::move(out), std::move(parallel)...);
    }

    // drain with each of the four parallel outputs, and the five outputs of
    // one size.  A batch consumer can rely on each popped entry having all
    // five arrays populated.  The drain stage of the background thread calls
    // it, and the forced inline with its copies keeps that stage free of a
    // call and of a window check.
    template <class Brand, std::size_t Extent, class Metas, class Scopes, class Callsites, class Depths>
        requires detail::is_drain_prefix_v<Metas, Scopes, Callsites, Depths>
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE uint32_t
    try_pop_batch(::fixy::ViewMut<Entry, Brand, Extent> out_entries, Metas out_meta_starts, Scopes out_scope_hashes,
                  Callsites out_callsite_hashes, Depths out_depths) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return drain_into_(std::move(out_entries), std::move(out_meta_starts), std::move(out_scope_hashes),
                           std::move(out_callsite_hashes), std::move(out_depths));
    }

    // head and tail are read at different instants while both threads run, so
    // the difference is a snapshot of a value that was never simultaneously
    // true. The return type says so and forces the caller to acknowledge it.
    //
    // Neither read is gnu::pure.  The other thread moves head and tail, and a
    // pure read lets the compiler fold the reads of a wait loop into one.
    [[nodiscard]] ::fixy::Stale<uint32_t> size() const noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return ::fixy::Stale<uint32_t>::at_infinity(static_cast<uint32_t>(head.get() - tail.get()));
    }

    // The acquire in get() means the returned bound also implies visibility of
    // every entry produced up to it, which is what makes it usable as a
    // catch-up target.
    [[nodiscard]] uint64_t total_produced() const noexcept CRUCIBLE_NO_THREAD_SAFETY { return head.get(); }

    // Valid only once both threads have stopped. Every counter here moves
    // backwards, which the monotonicity contract otherwise forbids, so the
    // caller must have joined the producer and the consumer first.
    void reset() noexcept CRUCIBLE_NO_THREAD_SAFETY {
        head.reset_under_quiescence();
        tail.reset_under_quiescence();
        consumer_tail_ = 0;
        cached_tail_.reset_under_quiescence();
        own_head_.reset_under_quiescence();
        CRUCIBLE_POST(0, head.get() == 0);
        CRUCIBLE_POST(0, tail.get() == 0);
        CRUCIBLE_POST(0, consumer_tail_ == 0);
        CRUCIBLE_POST(0, cached_tail_.get() == 0);
        CRUCIBLE_POST(0, own_head_.get() == 0);
    }

private:
    // The proofs of the writers of the two counters.  Each door of a side
    // makes the proof of its side: the proven commit after the record proof
    // of its caller, and each checked publish, which still compares the
    // counter with the copy of the writer, so a second writer ends the
    // process at the next publish of the first one.
    [[nodiscard]] static constexpr head_writer prove_head_writer_() noexcept { return {}; }

    [[nodiscard]] static constexpr tail_writer prove_tail_writer_() noexcept { return {}; }

    // Writes the entry and its four parallel slots into the reserved slot,
    // and warms the slot of the next append.  Gives the head of the
    // reservation, which each commit publishes.  The two commits share this
    // body, so they write the same slot.
    [[gnu::hot]] CRUCIBLE_INLINE uint64_t fill_slot_(Reservation const& reserved, const Entry& e, MetaIndex meta_start,
                                                     ScopeHash scope_hash, CallsiteHash callsite_hash,
                                                     OpDepth depth) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        const uint64_t h = reserved.head_;
        const RingSlot slot = mask_slot_(h);
        entries.with_element(slot, [&e](Entry& target) noexcept { target = e; });
        meta_starts.with_element(slot, [meta_start](MetaIndex& target) noexcept { target = meta_start; });
        scope_hashes.with_element(slot, [scope_hash](ScopeHash& target) noexcept { target = scope_hash; });
        callsite_hashes.with_element(slot, [callsite_hash](CallsiteHash& target) noexcept { target = callsite_hash; });
        depths.with_element(slot, [depth](OpDepth& target) noexcept { target = depth; });

        // Warm the destination of the following append while the caller is
        // still busy with its own post-append work.  One line of the depths
        // holds the depths of 32 slots, so the prefetch of that line warms
        // the next 31 appends too.
        const RingSlot next_slot = mask_slot_(h + 1);
        constexpr ::fixy::PrefetchAccess write_access = ::fixy::PrefetchAccess::write;
        ::fixy::prefetch<write_access, tracering_hw::kPrefetchLocality>(entries.at(next_slot));
        ::fixy::prefetch<write_access, tracering_hw::kPrefetchLocality>(meta_starts.at(next_slot));
        ::fixy::prefetch<write_access, tracering_hw::kPrefetchLocality>(scope_hashes.at(next_slot));
        ::fixy::prefetch<write_access, tracering_hw::kPrefetchLocality>(callsite_hashes.at(next_slot));
        ::fixy::prefetch<write_access, tracering_hw::kPrefetchLocality>(depths.at(next_slot));
        return h;
    }
};

// The first rung of the recording trust ladder. An adapter that builds an
// Entry out of values a foreign runtime supplied mints this tag, and the only
// transition the catalog admits from it is FromPytorch into Validated. A
// recording entry point takes the second tag, so the adapter reaches one by
// running its checks and retagging, and by no other route.
//
// There is deliberately no counterpart that mints the second tag directly.
// One existed, and an adapter calling it was a way of skipping the checks
// while reading like a certification.
[[nodiscard]] CRUCIBLE_INLINE constexpr TraceRing::FromPytorchEntryPtr mint_ffi_entry(const TraceRing::Entry& e
                                                                                      CRUCIBLE_LIFETIMEBOUND) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::vessel_trust::FromPytorch>(&e);
}

// The tagged pointer points at the entry.  The lifetime of a temporary entry
// stops at the end of the statement, and this twin refuses it.
TraceRing::FromPytorchEntryPtr mint_ffi_entry(const TraceRing::Entry&&) =
    delete("the lifetime of a temporary entry stops at the end of the statement. Tag an entry that has a name");

}  // namespace crucible
