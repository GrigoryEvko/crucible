#pragma once

// A single-producer single-consumer ring. The ring is never resized, and an
// append onto a full ring drops the entry rather than blocking the producer:
// the recording is re-taken on the next iteration, so a drop costs one
// iteration of trace, never a stall on the recorded thread.
//
// The ring splits into two ends (fixy/concurrent/SpscEnds.h).  try_append
// reads the head, writes the slot it names and then advances it, and those
// three steps are not one atomic operation.  Two threads that run them at
// once name the same slot, write over each other and advance the head once.
// So each door of a side takes the end of its side, and only a frame door of
// the split lends an end: the producer token or the held side opens the
// producer end, and the consumer token or the held side opens the consumer
// end.  The type then proves the one producer and the one consumer.
//
// The Vigil holds its ring in a held split.  Its record_op and dispatch_op
// give the producer role to the first thread that arrives and end the
// process on the second, and only that claim makes the proof that opens the
// producer side (RecordProof of crucible/ForegroundCtx.h).  The drain stage
// of the background thread opens the consumer side with its own proof.  A
// test and a bench drive a keyed split of their own.
//
// Two publishes
//   The producer keeps its own copy of the head on the private line of its
//   end, and it reads its position there.  The consumer polls the head, so a
//   load of the head by the producer can wait for a transfer of its line.
//   The end proves the one writer, so the publish of the head loads nothing
//   from the head, and the publish of the tail loads nothing from the tail.
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

#include <crucible/MetaLog.h>
#include <crucible/TensorMeta.h>
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
#include <fixy/concurrent/SpscEnds.h>
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

namespace side {

// The six fields of the flags of an op (OpFlags).
struct InferenceModeField final {};
struct MutableField final {};
struct PhaseField final {};
struct TorchFunctionField final {};
struct GradEnabledField final {};
struct FifthTagField final {};

// The four fields of the tags of the scalar slots 0 to 3 (FirstFourTags).
struct SlotZeroTagField final {};
struct SlotOneTagField final {};
struct SlotTwoTagField final {};
struct SlotThreeTagField final {};

// The two fields of the tags of the five scalar slots (SlotTags).
struct FirstFourTagsField final {};
struct LastTagField final {};

}  // namespace side

// The tags of the scalar slots 0 to 3 of an entry, two bits for each, slot 0
// in the low bits.  Each byte is a word of the four fields.
using FirstFourTags = ::fixy::PackedWord<uint8_t, ::fixy::BitField<side::SlotZeroTagField, kScalarTypeBits>,
                                         ::fixy::BitField<side::SlotOneTagField, kScalarTypeBits>,
                                         ::fixy::BitField<side::SlotTwoTagField, kScalarTypeBits>,
                                         ::fixy::BitField<side::SlotThreeTagField, kScalarTypeBits>>;

// The tags of the five scalar slots of an entry, in the order of a word of
// ScalarSlotKinds: the tags of slots 0 to 3 in bits 0 to 7, and the tag of
// slot 4 in bits 8 and 9.  Bits 10 to 15 are zero.
using SlotTags = ::fixy::PackedWord<uint16_t, ::fixy::BitField<side::FirstFourTagsField, 8>,
                                    ::fixy::BitField<side::LastTagField, kScalarTypeBits>>;

// Why a door of the flags of an op refuses a byte from outside.
enum class OpFlagsRefusal : uint8_t {
    // The byte sets the tag of scalar slot 4, which only the scalar slots of
    // an entry give.
    TagOfAScalarSlot,
    // A stored byte sets the gradient mode, which a trace record keeps in a
    // byte of its own and an image of a region does not keep.
    GradientInStoredFlags,
    // The byte of the gradient mode of a trace record is neither 0 nor 1.
    GradientByteNotABit,
};

// The flags of an op in one byte: the inference mode at bit 0, the mutability
// at bit 1, the phase of the training step in bits 2 and 3, the torch function
// mode at bit 4, the gradient mode at bit 5, and the tag of scalar slot 4 in
// bits 6 and 7.  The front end gives the first five fields, and the scalar
// slots of the entry give the tag.  The byte is part of an entry, a trace
// file and an image of a region, so the layout is a format.
//
// A byte of the tree comes through a builder.  A byte from outside comes
// through the door of its boundary, which refuses a pattern that no front end
// gives: from_caller_byte for a caller of the C entries, from_record for a
// trace file, and from_stored_byte for an image of a region.
class OpFlags {
    using Word =
        ::fixy::PackedWord<uint8_t, ::fixy::BitField<side::InferenceModeField, 1>,
                           ::fixy::BitField<side::MutableField, 1>, ::fixy::BitField<side::PhaseField, 2>,
                           ::fixy::BitField<side::TorchFunctionField, 1>, ::fixy::BitField<side::GradEnabledField, 1>,
                           ::fixy::BitField<side::FifthTagField, kScalarTypeBits>>;

public:
    constexpr OpFlags() noexcept = default;

    // The flags of the byte of a caller of the C entries: the five fields of
    // the front end.  A byte that sets the tag of scalar slot 4 gives the
    // refusal TagOfAScalarSlot.  One compare.  The result is a
    // Result<OpFlags, OpFlagsRefusal>.  The return type of each door of a
    // byte is deduced, so the Result is named only where this class is
    // complete.
    [[nodiscard]] static constexpr auto from_caller_byte(uint8_t byte) noexcept {
        using Answer = ::fixy::Result<OpFlags, OpFlagsRefusal>;
        const OpFlags flags{word_of_(byte)};
        if (flags.word_.get<side::FifthTagField>() != 0) return Answer{::fixy::err(OpFlagsRefusal::TagOfAScalarSlot)};
        return Answer{flags};
    }

    // The flags of the stored byte of an image of a region: the inference
    // mode, the mutability, the phase and the torch function mode.  A byte
    // that sets the gradient mode or the tag of scalar slot 4 gives its
    // refusal.
    [[nodiscard]] static constexpr auto from_stored_byte(uint8_t byte) noexcept {
        using Answer = ::fixy::Result<OpFlags, OpFlagsRefusal>;
        const OpFlags flags{word_of_(byte)};
        if (flags.word_.get<side::GradEnabledField>() != 0) {
            return Answer{::fixy::err(OpFlagsRefusal::GradientInStoredFlags)};
        }
        if (flags.word_.get<side::FifthTagField>() != 0) return Answer{::fixy::err(OpFlagsRefusal::TagOfAScalarSlot)};
        return Answer{flags};
    }

    // The flags of a trace record: the stored byte, as from_stored_byte reads
    // it, and the gradient mode in a byte of its own, 0 or 1.  A gradient byte
    // of another value gives the refusal GradientByteNotABit.
    [[nodiscard]] static constexpr auto from_record(uint8_t stored, uint8_t gradient) noexcept {
        using Answer = ::fixy::Result<OpFlags, OpFlagsRefusal>;
        if (gradient > 1) return Answer{::fixy::err(OpFlagsRefusal::GradientByteNotABit)};
        return from_stored_byte(stored).match(
            [gradient](OpFlags flags) noexcept { return Answer{gradient != 0 ? flags.with_grad_enabled() : flags}; },
            [](OpFlagsRefusal refusal) noexcept { return Answer{::fixy::err(refusal)}; });
    }

    [[nodiscard]] constexpr bool is_inference_mode() const noexcept {
        return word_.get<side::InferenceModeField>() != 0;
    }
    [[nodiscard]] constexpr bool is_mutable() const noexcept { return word_.get<side::MutableField>() != 0; }
    [[nodiscard]] constexpr TrainingPhase phase() const noexcept {
        return ::fixy::enum_from_field<TrainingPhase, 2>(word_.get<side::PhaseField>());
    }
    [[nodiscard]] constexpr bool is_torch_function() const noexcept {
        return word_.get<side::TorchFunctionField>() != 0;
    }
    [[nodiscard]] constexpr bool is_grad_enabled() const noexcept { return word_.get<side::GradEnabledField>() != 0; }
    [[nodiscard]] constexpr ScalarType2 fifth_tag() const noexcept {
        return ::fixy::enum_from_field<ScalarType2, kScalarTypeBits>(word_.get<side::FifthTagField>());
    }

    // The same flags with one field set.  Each one-bit field takes the value
    // 1, and an enumerator of TrainingPhase and of ScalarType2 is below four,
    // so each write keeps each bit of its value.
    [[nodiscard]] constexpr OpFlags with_inference_mode() const noexcept {
        return OpFlags{word_.with_low_bits<side::InferenceModeField>(uint8_t{1})};
    }
    [[nodiscard]] constexpr OpFlags with_mutable() const noexcept {
        return OpFlags{word_.with_low_bits<side::MutableField>(uint8_t{1})};
    }
    [[nodiscard]] constexpr OpFlags with_phase(TrainingPhase phase) const noexcept {
        return OpFlags{word_.with_low_bits<side::PhaseField>(::fixy::bit_cast<uint8_t>(phase))};
    }
    [[nodiscard]] constexpr OpFlags with_torch_function() const noexcept {
        return OpFlags{word_.with_low_bits<side::TorchFunctionField>(uint8_t{1})};
    }
    [[nodiscard]] constexpr OpFlags with_grad_enabled() const noexcept {
        return OpFlags{word_.with_low_bits<side::GradEnabledField>(uint8_t{1})};
    }
    [[nodiscard]] constexpr OpFlags with_fifth_tag(ScalarType2 tag) const noexcept {
        return OpFlags{word_.with_low_bits<side::FifthTagField>(::fixy::bit_cast<uint8_t>(tag))};
    }

    // The byte of the flags.
    [[nodiscard]] constexpr uint8_t bits() const noexcept { return word_.bits(); }

    [[nodiscard]] friend constexpr bool operator==(OpFlags, OpFlags) noexcept = default;

private:
    constexpr explicit OpFlags(Word word) noexcept : word_{word} {}

    // The fields fill the byte, so each byte is a word of the layout.
    [[nodiscard]] static constexpr Word word_of_(uint8_t byte) noexcept {
        return Word::from_bits(byte).expect("the fields fill the byte");
    }

    Word word_{};
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

// The door of the trust ladder (crucible/ValidatedOp.h), the one class that
// writes the key of the tag Validated.
class EntryLadderDoor;

// The key of the tag of an entry that passed the trust ladder.  It has no
// public constructor, no copy and no move, so only its door writes it, as a
// prvalue at the call of the keyed retag (fixy/Tagged.h).
class EntryLadderKey {
    constexpr EntryLadderKey() noexcept = default;
    friend class EntryLadderDoor;

public:
    EntryLadderKey(const EntryLadderKey&) = delete("only the door of the trust ladder writes the key, at the retag");
    EntryLadderKey(EntryLadderKey&&) = delete("only the door of the trust ladder writes the key, at the retag");
    EntryLadderKey& operator=(const EntryLadderKey&) = delete("a key holds no value to assign");
    EntryLadderKey& operator=(EntryLadderKey&&) = delete("a key holds no value to assign");
    ~EntryLadderKey() = default;
};

}  // namespace detail

namespace vessel_trust {

// The tag of an entry that passed the trust ladder (entry_is_well_formed of
// crucible/ValidatedOp.h): a keyed tag, earned from the tag of the FFI.  Only
// the door of the ladder moves an entry under it.  A validated payload drops
// to its value in the order of the payloads of a session
// (fixy/session/Subtype.h), as the payload of a check does.
struct Validated {
    using retag_key = detail::EntryLadderKey;
    using earned_from = ::fixy::tags::vessel_trust::FromPytorch;
    using drops_to = void;
};

}  // namespace vessel_trust

// 2 MB alignment so the allocation lands on a PMD-aligned region and the
// huge-page advice is not rejected.
//
// Each data member is private.  Each public member function is const or
// static, and each static door that writes takes the end of its side.
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
        FirstFourTags scalar_types{};  // the tags of slots 0 to 3; the tag of slot 4 is in op_flags
        OpFlags op_flags{};
        // Zero-initialised so the content hash never sees indeterminate bytes.
        ::fixy::FixedArray<int64_t, 5> scalar_values{};

        // The tag of one slot, at a position known only at run time, so the
        // shift of the slot in the word of the five tags stays by hand.
        [[nodiscard, gnu::pure]] ScalarType2 get_scalar_type(uint32_t i) const noexcept {
            CRUCIBLE_PRE(::foundation::decide::in_range<uint32_t>(i, 0u, 4u));
            return ::fixy::enum_from_field<ScalarType2, kScalarTypeBits>(packed_scalar_types()
                                                                         >> (i * kScalarTypeBits));
        }

        void set_scalar_type(uint32_t i, ScalarType2 t) noexcept {
            CRUCIBLE_PRE(::foundation::decide::in_range<uint32_t>(i, 0u, 4u));
            const uint32_t shift = i * kScalarTypeBits;
            const uint32_t others = uint32_t{packed_scalar_types()} & ~(uint32_t{0x3} << shift);
            set_packed_scalar_types(static_cast<uint16_t>(others | (uint32_t{::fixy::bit_cast<uint8_t>(t)} << shift)));
            CRUCIBLE_POST(0, get_scalar_type(i) == t);
        }

        // The tags of the five slots, two bits for each slot, slot 0 in the
        // low bits (SlotTags).  Slots 0 to 3 are in scalar_types, and slot 4
        // is in op_flags.
        [[nodiscard, gnu::pure]] constexpr uint16_t packed_scalar_types() const noexcept {
            return SlotTags{}
                .with_fitting<side::FirstFourTagsField>(scalar_types.bits())
                .with_low_bits<side::LastTagField>(::fixy::bit_cast<uint8_t>(op_flags.fifth_tag()))
                .bits();
        }

        // Writes the tags of the five slots from the low ten bits of their
        // packed form, and keeps the other fields of op_flags.
        constexpr void set_packed_scalar_types(uint16_t packed) noexcept {
            constexpr uint16_t kTagBits = (uint16_t{1} << (kScalarTypeBits * kScalarSlots)) - 1;
            set_slot_tags(SlotTags::from_bits(static_cast<uint16_t>(packed & kTagBits))
                              .expect("the mask keeps the bits of the five tags"));
        }

        // Writes the tags of the five slots, and keeps the other fields of
        // op_flags.
        constexpr void set_slot_tags(SlotTags tags) noexcept {
            scalar_types =
                FirstFourTags::from_bits(tags.get<side::FirstFourTagsField>()).expect("the four tags fill the byte");
            op_flags = op_flags.with_fifth_tag(
                ::fixy::enum_from_field<ScalarType2, kScalarTypeBits>(tags.get<side::LastTagField>()));
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

    // An entry that no code changes from its construction to its end
    // (foundation/core/Sealed.h).  A caller builds it in its place with
    // build_sealed, so the seal copies no byte.
    using SealedEntry = ::fixy::Sealed<Entry>;

    // An Entry built from the input of a foreign runtime carries the first
    // tag, and no recording entry point takes it.  Only the door of the trust
    // ladder gives the second tag (validate_entry and
    // validate_entry_within_ceiling of crucible/ValidatedOp.h), because the
    // second tag is keyed.  Each tag holds a Ref of a sealed entry, not a
    // copy: no code writes the entry that the ladder checks, so a validated
    // op reads the fields that the checks read, and the 64-byte entry stays
    // where the caller built it.
    using FromPytorchEntry = ::fixy::Tagged<::fixy::Ref<SealedEntry const>, ::fixy::tags::vessel_trust::FromPytorch>;
    using ValidatedEntry = ::fixy::Tagged<::fixy::Ref<SealedEntry const>, vessel_trust::Validated>;

    static constexpr uint32_t CAPACITY = 1u << 16;
    static constexpr uint32_t MASK = CAPACITY - 1;
    static_assert((CAPACITY & MASK) == 0, "CAPACITY must be a power of two");

    // The writers of the two counters (the writer of a counter of
    // fixy/Mutation.h): the producer end writes head, and the consumer end
    // writes tail.  The ring is the owner of each proof, so only the side
    // doors of the ring publish a counter.
    struct HeadWriter final {};
    struct TailWriter final {};
    using head_writer = ::fixy::SoleWriterProof<TraceRing, HeadWriter>;
    using tail_writer = ::fixy::SoleWriterProof<TraceRing, TailWriter>;

    // The state of each side lives in the end of that side
    // (fixy/concurrent/SpscEnds.h), on a cache line of its own.  So the line
    // of the producer is never the line of the consumer, of head_ or of
    // tail_, and only the doors of a side, which take its end, write it.
    //
    // The producer keeps its copy of the head and its view of the tail.  A
    // stale view of the tail is safe, because it can only under-report free
    // space, which costs a real reload of tail and never an overwrite of a
    // slot the consumer has not read.  Monotonic enforces the direction: tail
    // only ever advances, so an observation that moved backwards is a lost
    // acquire.  The copy of the head is a wide counter that starts at zero
    // and steps by one entry, so a publish moves it forward with no compare.
    struct alignas(64) producer_state {
        ::fixy::WideMonotonic<1> own_head = ::fixy::mint_wide_monotonic<1>();
        ::fixy::Monotonic<uint64_t> cached_tail = ::fixy::mint_monotonic<uint64_t>(0);
    };
    // The consumer keeps its copy of the tail.  The drains track their
    // position here, and they publish to tail only once the copies are
    // complete.  The copy is a wide counter that starts at zero and steps by
    // the entries of one drain, at most CAPACITY, so a publish moves it
    // forward with no compare.
    struct alignas(64) consumer_state {
        ::fixy::WideMonotonic<CAPACITY> own_tail = ::fixy::mint_wide_monotonic<CAPACITY>();
    };

    // The two ends of a ring.  The side doors below are the only doors that
    // write a side of the ring, and each takes the end of its side.  A door
    // that writes the two sides takes the pair of the two ends of one ring.
    using Producer = ::fixy::concurrent::ProducerEnd<TraceRing>;
    using Consumer = ::fixy::concurrent::ConsumerEnd<TraceRing>;
    using EndPair = ::fixy::concurrent::SpscEndPair<TraceRing>;

private:
    // head and tail are each written by one end and read by the other, so
    // they sit on separate cache lines: sharing one line would make every
    // producer write invalidate the consumer's copy and back again.  The
    // publish of the head releases and get() acquires, which is the edge that
    // makes the slot writes visible across the pair.
    alignas(64)::fixy::AtomicMonotonic<uint64_t, head_writer> head_ =
        ::fixy::mint_atomic_monotonic<uint64_t, head_writer>(0);

    alignas(64)::fixy::AtomicMonotonic<uint64_t, tail_writer> tail_ =
        ::fixy::mint_atomic_monotonic<uint64_t, tail_writer>(0);

    // The five parallel arrays of the slots.  The producer end writes a slot,
    // and the consumer end copies it out after the publish of the head.
    alignas(64)::fixy::FixedArray<Entry, CAPACITY> entries_{};
    ::fixy::FixedArray<MetaIndex, CAPACITY> meta_starts_{};
    ::fixy::FixedArray<ScopeHash, CAPACITY> scope_hashes_{};
    ::fixy::FixedArray<CallsiteHash, CAPACITY> callsite_hashes_{};
    ::fixy::FixedArray<OpDepth, CAPACITY> depths_{};

    // The slot of a counter of the ring: its low bits.  The mask keeps the
    // slot below CAPACITY, so the optimizer removes the check.
    using RingSlot = ::fixy::Index<CAPACITY>;
    [[nodiscard]] CRUCIBLE_INLINE static RingSlot mask_slot_(uint64_t counter) noexcept {
        return RingSlot::check(counter & MASK).expect("a masked counter of the ring is below CAPACITY");
    }

public:
    TraceRing() noexcept {
        crucible::warden::register_hot_region(::fixy::pages::storage_of(*this), crucible::warden::RegionShape::stable);
    }
    ~TraceRing() { crucible::warden::unregister_hot_region(::fixy::pages::storage_of(*this)); }
    TraceRing(const TraceRing&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
    TraceRing& operator=(const TraceRing&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
    TraceRing(TraceRing&&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
    TraceRing& operator=(TraceRing&&) = delete("SPSC ring is pinned to a producer/consumer thread pair");

    // The doors of the two sides.  Each door takes the end of its side, and
    // only a frame door of a split lends an end (fixy/concurrent/SpscEnds.h).
    // So the type proves the one producer and the one consumer, and no
    // publish loads the counter that its own side writes.  The thread-safety
    // analysis cannot express the one writer of a side, so each door
    // suppresses it.
    //
    // Each append of the producer is one door: a reservation of the slot at
    // the head and its commit, in one body.  The reservation is private, and
    // nothing between the room test and the commit appends to the ring, so
    // the reserved head is the copy of the head of the end.
    //
    // Appends one entry, or returns false when the ring is full.  A caller
    // that names no depth records an op that no recorded op runs.
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE static bool
    try_append(Producer& producer, const Entry& e, MetaIndex meta_start = MetaIndex::none(), ScopeHash scope_hash = {},
               CallsiteHash callsite_hash = {}, OpDepth depth = {}) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return try_reserve_(producer).match(
            [&](Reservation reserved) noexcept {
                commit_(producer, std::move(reserved), e, meta_start, scope_hash, callsite_hash, depth);
                return true;
            },
            [] noexcept { return false; });
    }

    // Records one op through the producer ends of the ring and of its
    // metadata log, and returns false when the ring is full.  The slot of the
    // ring is reserved first.  A full ring then refuses the op before the log
    // takes a record, so the log holds no record of an op that the ring does
    // not hold.  A full log gives the op no metadata index, and the ring
    // still takes the op, so the detector sees it.  An op with no records
    // gets no index from the log either.  The door flattens, so the match and
    // its arms inline, and the record makes no call apart from the copy of
    // the records.
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE static bool
    try_record(Producer& ring, MetaLog::Producer& log, MetaRun metas, const Entry& entry, ScopeHash scope_hash,
               CallsiteHash callsite_hash, OpDepth depth) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return try_reserve_(ring).match(
            [&](Reservation reserved) noexcept {
                const MetaIndex meta_start = MetaLog::try_append(log, metas);
                commit_(ring, std::move(reserved), entry, meta_start, scope_hash, callsite_hash, depth);
                return true;
            },
            [] noexcept { return false; });
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
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE static bool
    try_append_pure(Ctx const&, Producer& producer, const Entry& e, MetaIndex meta_start = MetaIndex::none(),
                    ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {}, OpDepth depth = {}) noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        return try_append(producer, e, meta_start, scope_hash, callsite_hash, depth);
    }

    // Drains at most out.size() entries into out.  The parallel outputs are
    // a prefix of the four parallel arrays, in their order: the meta starts,
    // the scope hashes, the callsite hashes and the depths.  Each one holds
    // out.size() elements.  Each output is the run of a frame, which the drain
    // takes by move, so the drain is the one writer of each run.
    template <class Brand, std::size_t Extent, class... Parallel>
        requires detail::is_drain_prefix_v<Parallel...>
    [[nodiscard, gnu::hot]] static uint32_t drain(Consumer& consumer, ::fixy::ViewMut<Entry, Brand, Extent> out,
                                                  Parallel... parallel) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return drain_into_(consumer, erased_run_(std::move(out)), erased_run_(std::move(parallel))...);
    }

    // The consumer counterpart of try_append_pure, with the same gate: the
    // row of the context of the caller is empty, and its source permits no
    // atom.  A background stage holds a source that permits atoms, so it
    // drains with try_pop_batch, and not here.
    template <::foundation::effects::IsExecCtx Ctx, class Brand, std::size_t Extent, class... Parallel>
        requires ::fixy::IsPure<::foundation::effects::row_type_of_t<Ctx>>
              && ::foundation::effects::CtxHasPureSource<Ctx> && detail::is_drain_prefix_v<Parallel...>
    [[nodiscard, gnu::hot]] static uint32_t drain_pure(Ctx const&, Consumer& consumer,
                                                       ::fixy::ViewMut<Entry, Brand, Extent> out,
                                                       Parallel... parallel) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return drain(consumer, std::move(out), std::move(parallel)...);
    }

    // drain with each of the four parallel outputs, and the five outputs of
    // one size.  A batch consumer can rely on each popped entry having all
    // five arrays populated.  The drain stage of the background thread calls
    // it, and the forced inline with its copies keeps that stage free of a
    // call and of a window check.
    template <class Brand, std::size_t Extent, class Metas, class Scopes, class Callsites, class Depths>
        requires detail::is_drain_prefix_v<Metas, Scopes, Callsites, Depths>
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE static uint32_t
    try_pop_batch(Consumer& consumer, ::fixy::ViewMut<Entry, Brand, Extent> out_entries, Metas out_meta_starts,
                  Scopes out_scope_hashes, Callsites out_callsite_hashes, Depths out_depths) noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        return drain_into_(consumer, erased_run_(std::move(out_entries)), erased_run_(std::move(out_meta_starts)),
                           erased_run_(std::move(out_scope_hashes)), erased_run_(std::move(out_callsite_hashes)),
                           erased_run_(std::move(out_depths)));
    }

    // Empties the ring: each counter and each copy of a counter go back to
    // zero.  Every counter here moves backwards, which the monotonicity
    // contract otherwise forbids.  The door takes the pair of the two ends of
    // the ring, so the thread is in the frame of the two sides of the ring,
    // and no other thread is in a frame of it.  Only a pair door builds a
    // pair, from the two ends of one split, so two frames of two rings give
    // no pair (neg_tracering_nested_frames_of_two_rings).  A reset of a ring whose
    // consumer another thread can hold thus does not compile.
    static void reset(EndPair& ends) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        Producer& producer = ends.producer();
        Consumer& consumer = ends.consumer();
        TraceRing& ring = *producer.owner_;
        ring.head_.reset_under_quiescence();
        ring.tail_.reset_under_quiescence();
        consumer.state_.own_tail.reset_under_quiescence(::fixy::WideStart::zero());
        producer.state_.cached_tail.reset_under_quiescence();
        producer.state_.own_head.reset_under_quiescence(::fixy::WideStart::zero());
        CRUCIBLE_POST(0, ring.head_.get() == 0);
        CRUCIBLE_POST(0, ring.tail_.get() == 0);
        CRUCIBLE_POST(0, consumer.state_.own_tail.get() == 0);
        CRUCIBLE_POST(0, producer.state_.cached_tail.get() == 0);
        CRUCIBLE_POST(0, producer.state_.own_head.get() == 0);
    }

    // head and tail are read at different instants while both threads run, so
    // the difference is a snapshot of a value that was never simultaneously
    // true. The return type says so and forces the caller to acknowledge it.
    //
    // Neither read is gnu::pure.  The other thread moves head and tail, and a
    // pure read lets the compiler fold the reads of a wait loop into one.
    [[nodiscard]] ::fixy::Stale<uint32_t> size() const noexcept CRUCIBLE_NO_THREAD_SAFETY {
        return ::fixy::Stale<uint32_t>::at_infinity(static_cast<uint32_t>(head_.get() - tail_.get()));
    }

    // The acquire in get() means the returned bound also implies visibility of
    // every entry produced up to it, which is what makes it usable as a
    // catch-up target.
    [[nodiscard]] uint64_t total_produced() const noexcept CRUCIBLE_NO_THREAD_SAFETY { return head_.get(); }

    // The count of the entries that the consumer took, such as for a new
    // consumer that counts only its own entries.
    [[nodiscard]] uint64_t total_consumed() const noexcept CRUCIBLE_NO_THREAD_SAFETY { return tail_.get(); }

private:
    // A free slot of the ring, which try_reserve_ found.  The token holds the
    // head at the reservation.  Only commit_ moves the head, so a token that
    // the producer drops leaves the ring as it was.  The token has no
    // destructor work, and it only moves, so one reservation fills one slot.
    // Only the body of one append door holds a token, from its room test to
    // its commit.
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
    // A reservation only moves, and its moves are those of its bytes.  An
    // Option of a reservation moves it through the owning slot.
    static_assert(__is_trivially_copyable(Reservation) && !__is_constructible(Reservation, Reservation const&));

    // Reserves the slot at the head, or gives no token when the ring is full.
    // The ring does not change: the head moves in commit_.
    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE static ::fixy::Option<Reservation> try_reserve_(Producer& producer) noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        TraceRing& ring = *producer.owner_;
        producer_state& state = producer.state_;
        const uint64_t h = state.own_head.get();

        // Refreshing from the consumer's atomic is the slow path, taken only
        // once the private view says the ring is full.
        if (h - state.cached_tail.get() >= CAPACITY) [[unlikely]] {
            state.cached_tail.advance(ring.tail_.get());
            if (h - state.cached_tail.get() >= CAPACITY) [[unlikely]]
                return ::fixy::none;
        }
        return ::fixy::Option<Reservation>::some(Reservation{h});
    }

    // Writes the entry into the reserved slot, and moves the head past it.
    // The consumer then sees the entry.  Each argument has no default, so a
    // door cannot drop a field of the slot.  The release of the publish
    // carries the five slot writes to the consumer.  The producer end is the
    // proof of the one writer, so the publish loads nothing from the head
    // (the two publishes).  The reserved slot is the slot of own_head, because
    // nothing appends between the room test and the commit.  So the publish
    // is a step of one entry of the wide copy, with no compare.
    [[gnu::hot]] CRUCIBLE_INLINE static void commit_(Producer& producer, Reservation reserved, const Entry& e,
                                                     MetaIndex meta_start, ScopeHash scope_hash,
                                                     CallsiteHash callsite_hash, OpDepth depth) noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        TraceRing& ring = *producer.owner_;
        ring.fill_slot_(reserved, e, meta_start, scope_hash, callsite_hash, depth);
        ring.head_.advance_sole_writer_by_wide_step(prove_head_writer_(), producer.state_.own_head,
                                                    ::fixy::WideStep<1>::one());
    }

    // The proofs of the writers of the two counters.  The doors of a side
    // make the proof of its side, and the end of the side that each door
    // takes proves the one writer.
    [[nodiscard]] static constexpr head_writer prove_head_writer_() noexcept { return {}; }

    [[nodiscard]] static constexpr tail_writer prove_tail_writer_() noexcept { return {}; }

    // The run of a frame on the erased identity (erase_brand of
    // foundation/core/Region.h).  Each drain erases each of its runs at its
    // entry, so the body of the drains is one instance for each element type
    // and extent of its runs, and not one for each frame of a caller.  The
    // drain takes each run by move and keeps none, so the erased run ends in
    // the drain.  A run that its caller erased passes as it is.
    template <class T, class Brand, std::size_t Extent>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr auto erased_run_(::fixy::ViewMut<T, Brand, Extent>&& run) noexcept {
        if constexpr (::foundation::brand::IsFreshBrand<Brand>) {
            return ::fixy::erase_brand(std::move(run));
        } else {
            return std::move(run);
        }
    }

    // The ring array of the elements T: the entries, the meta starts, the
    // scope hashes, the callsite hashes or the depths.
    template <class T>
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::FixedArray<T, CAPACITY> const& slots_of_() const noexcept {
        if constexpr (std::is_same_v<T, Entry>) {
            return entries_;
        } else if constexpr (std::is_same_v<T, MetaIndex>) {
            return meta_starts_;
        } else if constexpr (std::is_same_v<T, ScopeHash>) {
            return scope_hashes_;
        } else if constexpr (std::is_same_v<T, CallsiteHash>) {
            return callsite_hashes_;
        } else {
            return depths_;
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
    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE static uint32_t
    drain_into_(Consumer& consumer, ::fixy::ViewMut<Entry, Brand, Extent> out, Parallel... parallel) noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        const std::size_t room = out.size();
        CRUCIBLE_PRE(((parallel.size() == room) && ...));
        if (room == 0) [[unlikely]]
            return 0;

        TraceRing& ring = *consumer.owner_;
        const uint64_t h = ring.head_.get();
        const uint64_t t = consumer.state_.own_tail.get();

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
            ring.copy_run_(std::move(out), start, count);
            (ring.copy_run_(std::move(parallel), start, count), ...);
        } else {
            const std::size_t first = CAPACITY - start;
            const std::size_t second = count - first;
            // first is at least one, so the second run is shorter than the
            // count, and the bound of the count bounds the run of the ring.
            [[assume(second < count)]];
            ring.copy_wrapped_run_(std::move(out), start, first, count);
            (ring.copy_wrapped_run_(std::move(parallel), start, first, count), ...);
        }

        // Publishing the new tail declares slots [t, t + count) free. The
        // producer only reads tail after finding the ring full, so it sees
        // this before it can overwrite any of them.  The copy of the tail
        // steps by the count, and tail takes the value of the copy.  The
        // consumer end proves the one writer, so the publish loads nothing
        // from tail.  The count is at least one after the test of zero, and
        // at most room.  So the check of the step folds where the extent of
        // out bounds room by CAPACITY, as the batch of the drain stage does,
        // and the publish holds no compare (the wide counter of
        // fixy/Mutation.h).
        ring.tail_.advance_sole_writer_by_wide_step(prove_tail_writer_(), consumer.state_.own_tail,
                                                    ::fixy::WideStep<CAPACITY>::check_within_bound(count));
        CRUCIBLE_POST(count, count <= room);
        // The count came from a 32-bit difference, so it fits the result.
        return static_cast<uint32_t>(count);
    }

    // Writes the entry and its four parallel slots into the reserved slot,
    // and warms the slot of the next append.
    [[gnu::hot]] CRUCIBLE_INLINE void fill_slot_(Reservation const& reserved, const Entry& e, MetaIndex meta_start,
                                                 ScopeHash scope_hash, CallsiteHash callsite_hash,
                                                 OpDepth depth) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        const uint64_t h = reserved.head_;
        const RingSlot slot = mask_slot_(h);
        entries_.set(slot, e);
        meta_starts_.set(slot, meta_start);
        scope_hashes_.set(slot, scope_hash);
        callsite_hashes_.set(slot, callsite_hash);
        depths_.set(slot, depth);

        // Warm the destination of the following append while the caller is
        // still busy with its own post-append work.  One line of the depths
        // holds the depths of 32 slots, so the prefetch of that line warms
        // the next 31 appends too.
        const RingSlot next_slot = mask_slot_(h + 1);
        constexpr ::fixy::PrefetchAccess write_access = ::fixy::PrefetchAccess::write;
        ::fixy::prefetch<write_access, tracering_hw::kPrefetchLocality>(entries_.at(next_slot));
        ::fixy::prefetch<write_access, tracering_hw::kPrefetchLocality>(meta_starts_.at(next_slot));
        ::fixy::prefetch<write_access, tracering_hw::kPrefetchLocality>(scope_hashes_.at(next_slot));
        ::fixy::prefetch<write_access, tracering_hw::kPrefetchLocality>(callsite_hashes_.at(next_slot));
        ::fixy::prefetch<write_access, tracering_hw::kPrefetchLocality>(depths_.at(next_slot));
    }
};

// The first rung of the recording trust ladder. An adapter that builds an
// Entry out of values a foreign runtime supplied mints this tag.  The only
// way from it into Validated is the door of the ladder
// (crucible/ValidatedOp.h), which holds the key of the keyed tag. A
// recording entry point takes the second tag, so the adapter reaches one
// through the checks of that door, and by no other route.
//
// There is deliberately no counterpart that mints the second tag directly.
// One existed, and an adapter calling it was a way of skipping the checks
// while reading like a certification.
//
// The tag holds a Ref of the sealed entry, so no code writes the entry while
// the tag or an op of it lives.  An entry that the caller can still write
// gets no tag.  O(1).
[[nodiscard]] CRUCIBLE_INLINE constexpr TraceRing::FromPytorchEntry mint_ffi_entry(TraceRing::SealedEntry const& entry
                                                                                   CRUCIBLE_LIFETIMEBOUND) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::vessel_trust::FromPytorch>(::fixy::erase_brand(::fixy::to(entry)));
}

// The tagged Ref names the sealed entry.  A temporary sealed entry ends at the
// end of the statement, and this twin refuses it.
TraceRing::FromPytorchEntry mint_ffi_entry(TraceRing::SealedEntry const&&) = delete(
    "the tag names the sealed entry, and a temporary sealed entry ends at the end of the statement: seal the "
    "entry under a name");

}  // namespace crucible
