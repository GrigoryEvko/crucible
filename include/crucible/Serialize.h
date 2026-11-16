#pragma once

// The wire format is a 32-byte header followed by a flat payload. The header
// is a 4-byte magic, a 4-byte version, a 1-byte node kind, 7 zero pad bytes,
// an 8-byte merkle hash and an 8-byte content hash.
//
// Nothing in the format is a raw pointer, so a loaded image is independent of
// where it was written. A tensor's data pointer is written as zero, and a
// branch arm's target is written as the target's merkle hash and resolved
// through a caller-supplied lookup at load time.

#include <crucible/CKernelId.h>
#include <crucible/LoadError.h>
#include <crucible/MerkleDag.h>
#include <crucible/PoolAllocator.h>
#include <crucible/TraceRing.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible {

// The four bytes spell "CDAG" most significant first. A little-endian host
// stores them in the opposite order, so a hex dump of the file starts with
// 47 41 44 43, which prints as "GADC". Both spellings have been written down
// wrongly before; the value is what the reader compares, not the spelling.
static constexpr uint32_t CDAG_MAGIC = 0x43444147u;
using CdagFormatVersion = ::fixy::Tagged<uint32_t, ::fixy::tags::source::FormatVersion>;
using ExternalCdagVersion = ::fixy::Tagged<uint32_t, ::fixy::tags::source::External>;
// Loaded is an earned tag.  deserialize_region builds the node from Cipher
// bytes, so the node is Replayed first.  It becomes Loaded only after the
// fold of its operations matches the content hash that the image records,
// which is the discharge of the Replayed to Loaded edge.  The node is in the
// arena of the load, and a loaded value always holds a node, so the tag holds
// a Ref and not an address that can be null.
using LoadedRegionNode = ::fixy::Tagged<::fixy::Ref<RegionNode>, ::fixy::tags::source::Loaded>;
// 10 (2026-09-15): the content-hash fold changed mixer.  Every region on
// disk carries a content hash the current code no longer computes, so a
// version-9 file would deserialize into regions whose hashes disagree with
// their own contents and whose KernelCache entries key on nothing.  The
// bump makes cdag_version_matches reject those files outright.
// 11 (2026-11-13): the storage size of a tensor has 64 bits on the wire, so
// a storage of 4 GiB or more keeps its size.
// 12 (2026-11-16): each op has its nesting depth on the wire, after its kernel
// id, so a region of the store keeps the ops that a lowering takes.
// 13 (2026-11-16): each op has the tags of its scalars on the wire, after its
// depth, and the content hash folds them.
static constexpr CdagFormatVersion CDAG_VERSION =
    ::fixy::mint_tagged<::fixy::tags::source::FormatVersion>(uint32_t{13});

[[nodiscard]] constexpr bool cdag_version_matches(ExternalCdagVersion disk_version) noexcept {
    return disk_version.value() == CDAG_VERSION.value();
}

// Ceilings on the counts a header may declare, an order of magnitude above
// anything a real trace reaches. A header is rejected against them before a
// single byte of the body is trusted, so a fabricated count cannot provoke a
// terabyte-scale allocation that only fails once the body turns out truncated.
static constexpr uint32_t CDAG_MAX_OPS = 1u << 22;
// The slot ceiling is the allocator's own, not a slack figure: a loaded plan
// goes straight into the allocator, whose matching precondition is only an
// assumption in a release build. Deriving the two from one constant makes
// "it deserialised" imply "it is safe to initialise" by construction, where a
// looser wire ceiling would admit counts the allocator cannot serve.
static constexpr uint32_t CDAG_MAX_SLOTS = ::crucible::PoolAllocator::kMaxNumSlots;
static constexpr uint16_t CDAG_MAX_INPUTS = 1024;
static constexpr uint16_t CDAG_MAX_OUTPUTS = 1024;
static constexpr uint16_t CDAG_MAX_SCALAR_ARGS = 256;
static constexpr uint32_t CDAG_MAX_BRANCH_ARMS = 1u << 16;

namespace detail_ser {

// The writer of an image.  It writes each value as its bytes, the low byte
// first, which is the layout that the reader reads.  A write past the end of
// the image clears has_room, and the serialize function then gives zero.
struct Writer {
    ::fixy::View<std::byte> image{};
    size_t pos = 0;
    bool has_room = true;

    // One unsigned word as its sizeof(Word) bytes.  Complexity:
    // O(sizeof(Word)).
    template <::fixy::UnsignedInteger Word>
    void write_word(Word word) {
        image.template window<sizeof(Word)>(pos).match(
            [this, word](::fixy::View<std::byte, sizeof(Word)> bytes) noexcept {
                for (const auto index : bytes.indices()) {
                    bytes.at(index) = static_cast<std::byte>(static_cast<uint8_t>(word >> (8 * index.raw())));
                }
                pos += sizeof(Word);
            },
            [this] noexcept { has_room = false; });
    }

    // An integer, a bool, an enumerator or a float.  An enumerator gives its
    // underlying integer, and a bool gives one byte of 0 or 1.  A signed
    // integer and a float give the bits of the unsigned word of their size.
    template <typename T>
    void w(T value) {
        if constexpr (std::is_enum_v<T>) {
            w(std::to_underlying(value));
        } else if constexpr (std::is_same_v<T, bool>) {
            write_word(static_cast<uint8_t>(value ? 1 : 0));
        } else if constexpr (std::is_same_v<T, float>) {
            write_word(::fixy::bit_cast<uint32_t>(value));
        } else if constexpr (std::is_signed_v<T>) {
            write_word(::fixy::bit_cast<std::make_unsigned_t<T>>(value));
        } else {
            write_word(value);
        }
    }

    // N bytes of zero, which is the one value of a pad byte.
    template <size_t N>
    void write_zero_pad() {
        for (size_t index = 0; index < N; ++index) {
            write_word(uint8_t{0});
        }
    }
};

// The reader of an image.  It reads each value from its bytes, the low byte
// first, as the writer laid them out on a little-endian host.  A read past
// the end of the image records the reason truncated and gives zero, and the
// parse that follows it is discarded.
struct Reader {
    ::fixy::View<const std::byte> image{};
    size_t pos = 0;
    // The first reason that a read refused the image, or none.  A later
    // refusal keeps the first reason, so the loader reports the first fault.
    // After a refusal the reads go on and give zeros or substitutes, and a
    // check of such a value would name a wrong reason.  So each refusal of
    // the loader reports this reason when it holds one.
    ::fixy::Option<LoadError> refusal{};

    void refuse(LoadError reason) noexcept {
        if (refusal.is_none()) refusal = ::fixy::Option<LoadError>::some(reason);
    }

    // The recorded reason, or reason when the reads refused nothing.
    [[nodiscard]] LoadError reason_or(LoadError reason) const noexcept {
        return ::fixy::Option<LoadError>{refusal}.value_or(reason);
    }

    // The next sizeof(Word) bytes as one unsigned word.  Complexity:
    // O(sizeof(Word)).
    template <::fixy::UnsignedInteger Word>
    [[nodiscard]] Word read_word() {
        return image.template window<sizeof(Word)>(pos).match(
            [this](::fixy::View<const std::byte, sizeof(Word)> bytes) noexcept {
                Word word = 0;
                for (const auto index : bytes.indices()) {
                    word = static_cast<Word>(word | (static_cast<Word>(bytes.at(index)) << (8 * index.raw())));
                }
                pos += sizeof(Word);
                return word;
            },
            [this] noexcept {
                refuse(LoadError::truncated);
                return Word{0};
            });
    }

    // An integer or a float of the image.  A signed integer and a float take
    // the bits of the unsigned word of their size.
    template <typename T>
    [[nodiscard]] T r() {
        if constexpr (std::is_same_v<T, float>) {
            return ::fixy::bit_cast<float>(read_word<uint32_t>());
        } else if constexpr (std::is_signed_v<T>) {
            return ::fixy::bit_cast<T>(read_word<std::make_unsigned_t<T>>());
        } else {
            return read_word<T>();
        }
    }

    // The real runtime branch behind every validated read in this file.
    //
    // A refinement type constructed straight from a wire byte is not enough
    // on its own. Its precondition is a genuine check only while contracts
    // are enforced. In a release build it degrades to a promise handed to the
    // optimiser, and a byte that breaks the predicate then becomes a lie the
    // optimiser is entitled to act on. So untrusted input is rejected here
    // first, by a plain predicate call that no build configuration removes,
    // and the refinement that follows it is a second, typed check that holds
    // by construction.
    //
    // A rejected read yields the substitute rather than the offending value.
    // The caller passes the result to a refinement constructor, and handing
    // that constructor a value its own precondition rejects would abort while
    // contracts are enforced and pass silently otherwise. Returning a value
    // the predicate accepts makes the outcome the same either way: the failure
    // travels as the refusal field_out_of_range, which every entry point
    // turns into its error, and the substituted value is discarded with the
    // rest of the parse. A value that passes is returned unchanged, so the
    // bytes a good image produces are untouched.
    //
    // Every value is judged, also after a refusal. An earlier refused read
    // leaves the bytes that follow it real, so a read after it returns a
    // wire byte, and the refinement that receives it assumes its predicate in
    // a release build. Each caller passes a substitute that its predicate
    // accepts.
    template <typename T, typename Pred>
    [[nodiscard]] T read_gated(Pred pred, T valid_substitute = T{0}) {
        const T v = r<T>();
        if (!pred(v)) [[unlikely]] {
            refuse(LoadError::field_out_of_range);
            return valid_substitute;
        }
        return v;
    }

    // A bool takes one byte, and only 0 and 1 are values of it.  Copying any
    // other byte into a bool makes a value that the program cannot read, so
    // the byte is judged as an integer first.
    [[nodiscard]] bool read_bool() {
        return read_gated<uint8_t>([](uint8_t byte) noexcept { return byte <= 1; }) != 0;
    }

    // An enumerator of E.  fixy::enum_from gives the enumerator of a byte,
    // so a new enumerator needs no edit here.  A byte that names no
    // enumerator is refused, and none reaches a switch.  The caller gives an
    // enumerator as the substitute of a refused byte.
    template <::fixy::Enumerated E>
    [[nodiscard]] E read_enumerator(E valid_substitute) {
        return ::fixy::enum_from<E>(r<std::underlying_type_t<E>>())
            .match([](E value) noexcept { return value; },
                   [this, valid_substitute] noexcept {
                       refuse(LoadError::field_out_of_range);
                       return valid_substitute;
                   });
    }

    // Pad bytes carry no value.  A writer sets them to zero, so a non-zero
    // byte means the image is not one that a writer produced.  The reader
    // stores no pad byte: the pad of each object that it builds is zero from
    // the construction of the object.
    template <size_t N>
    void read_zero_pad() {
        for (size_t index = 0; index < N; ++index) {
            if (r<uint8_t>() != 0) [[unlikely]]
                refuse(LoadError::field_out_of_range);
        }
    }

    [[nodiscard]] size_t remaining() const noexcept { return (pos <= image.size()) ? (image.size() - pos) : 0; }

    // Ask before growing the arena for count elements of element_bytes each.
    // A header can claim a count near the ceiling with a truncated body, and
    // without this the arena grows by the full amount before the first
    // element read discovers the end of the buffer, leaving space only a
    // detach can reclaim.  A product that wraps has no room.
    [[nodiscard]] bool has_remaining(size_t count, size_t element_bytes) const noexcept {
        return ::foundation::core::checked_mul(count, element_bytes)
            .match([this](size_t need) noexcept { return remaining() >= need; }, [] noexcept { return false; });
    }
};

// Two fields are process-local and must never reach persisted bytes: the data
// pointer is a runtime address, and the gradient-function hash is an identity
// only this process holds. Both are written as zero, and each zero is a
// constant refined to be zero, so a later edit that feeds the live field in
// does not compile, where it would otherwise produce an image whose bytes
// differ between runs. Pad bytes are zero for the same reason.
inline void write_meta(Writer& w, const TensorMeta& m) {
    for (uint8_t d = 0; d < kMaxTensorNDim; ++d) {
        w.w(m.sizes[d].value());
    }
    for (uint8_t d = 0; d < kMaxTensorNDim; ++d) {
        w.w(m.strides[d].value());
    }
    constexpr auto zero_ptr = ::fixy::mint_refined<::fixy::is_zero>(std::uint64_t{0});
    w.w(zero_ptr.value());
    w.w(m.ndim);
    w.w(m.dtype);
    w.w(m.device_type);
    w.w(m.device_idx);
    w.w(m.layout);
    w.w(m.requires_grad);
    w.w(m.flags);
    w.w(m.output_nr);
    w.w(m.storage_offset);
    w.w(m.version);
    w.w(m.storage_nbytes);
    constexpr auto zero_grad_fn_hash = ::fixy::mint_refined<::fixy::is_zero>(std::uint64_t{0});
    w.w(zero_grad_fn_hash.value());
}

// The two process-local fields are reset here whatever the bytes hold, so an
// older or corrupt image cannot resurrect an address from another process.
inline TensorMeta read_meta(Reader& r) {
    TensorMeta m{};
    // A dimension past the extent bound is refused here.  tensor_dim states
    // the bound as a contract, which aborts on a byte from outside.
    for (uint8_t d = 0; d < kMaxTensorNDim; ++d) {
        m.sizes[d] = tensor_dim(r.read_gated<int64_t>(::fixy::bounded_above<kMaxTensorDimExtent>));
    }
    for (uint8_t d = 0; d < kMaxTensorNDim; ++d) {
        m.strides[d] = tensor_dim(r.read_gated<int64_t>(::fixy::bounded_above<kMaxTensorDimExtent>));
    }
    (void)r.r<uint64_t>();  // the persisted data pointer, deliberately dropped
    m.data_ptr = external_data_ptr(nullptr);
    // The rank is bounded by the fixed width of the size and stride arrays.
    m.ndim = make_ndim(::fixy::mint_refined<::fixy::bounded_above<kMaxTensorNDim>>(
        r.read_gated<uint8_t>(::fixy::bounded_above<kMaxTensorNDim>)));
    // The scalar-type enumerators are sparse, and a value outside the set
    // reaches a switch whose default is marked unreachable.
    m.dtype = make_scalar_type(::fixy::mint_refined<valid_scalar_type>(r.read_gated<int8_t>(valid_scalar_type)));
    // The device type and the layout both feed the content hash, which is the
    // node's identity, so an unrecognised value would not fail loudly. It
    // would produce a node whose identity silently disagrees with the one that
    // was written.
    m.device_type = make_device_type(::fixy::mint_refined<valid_device_type>(r.read_gated<int8_t>(valid_device_type)));
    m.device_idx = r.r<int8_t>();
    m.layout = make_layout(::fixy::mint_refined<valid_layout>(r.read_gated<int8_t>(valid_layout)));
    m.requires_grad = r.read_bool();
    m.flags = r.r<uint8_t>();
    m.output_nr = r.r<uint8_t>();
    m.storage_offset = r.r<int64_t>();
    m.version = r.r<uint32_t>();
    m.storage_nbytes = r.r<uint64_t>();
    (void)r.r<uint64_t>();  // the persisted gradient-function hash, dropped
    m.grad_fn_hash = grad_fn_hash(0);
    return m;
}

// A plan slot, field by field in declaration order.  The 40 bytes on the
// wire are the bytes of the struct, which has no implicit padding, and the
// pad fields go out as zero.
inline void write_slot(Writer& w, const TensorSlot& slot) {
    w.w(slot.offset_bytes);
    w.w(slot.nbytes);
    w.w(slot.birth_op.raw());
    w.w(slot.death_op.raw());
    w.w(slot.dtype);
    w.w(slot.device_type);
    w.w(slot.device_idx);
    w.w(slot.layout);
    w.w(static_cast<uint8_t>(slot.is_external ? 1 : 0));
    w.write_zero_pad<sizeof(slot.pad)>();
    w.w(slot.slot_id.raw());
    w.write_zero_pad<sizeof(slot.pad2)>();
}

// The reader of write_slot.  Each enum, the bool and each pad byte is judged
// before it becomes a field, so a slot holds only values of its field types.
inline TensorSlot read_slot(Reader& r) {
    TensorSlot slot{};
    slot.offset_bytes = r.r<uint64_t>();
    slot.nbytes = r.r<uint64_t>();
    slot.birth_op = OpIndex{r.r<uint32_t>()};
    slot.death_op = OpIndex{r.r<uint32_t>()};
    slot.dtype = make_scalar_type(::fixy::mint_refined<valid_scalar_type>(r.read_gated<int8_t>(valid_scalar_type)));
    slot.device_type =
        make_device_type(::fixy::mint_refined<valid_device_type>(r.read_gated<int8_t>(valid_device_type)));
    slot.device_idx = r.r<int8_t>();
    slot.layout = make_layout(::fixy::mint_refined<valid_layout>(r.read_gated<int8_t>(valid_layout)));
    slot.is_external = r.read_bool();
    r.read_zero_pad<sizeof(slot.pad)>();
    slot.slot_id = SlotId{r.r<uint32_t>()};
    r.read_zero_pad<sizeof(slot.pad2)>();
    return slot;
}

inline void write_guard(Writer& w, const Guard& guard) {
    w.w(guard.kind);
    w.write_zero_pad<sizeof(guard.pad)>();
    w.w(guard.op_index.raw());
    w.w(guard.arg_index);
    w.w(guard.dim_index);
}

// A guard kind outside the enumeration would reach the replay switch on the
// kind, so the kind is read as an enumerator.
inline Guard read_guard(Reader& r) {
    Guard guard{};
    guard.kind = r.read_enumerator<Guard::Kind>(Guard::Kind::SHAPE_DIM);
    r.read_zero_pad<sizeof(guard.pad)>();
    guard.op_index = OpIndex{r.r<uint32_t>()};
    guard.arg_index = r.r<uint16_t>();
    guard.dim_index = r.r<uint16_t>();
    return guard;
}

inline constexpr size_t kTensorSlotWireBytes = 40;

// The zero bytes between the node kind and the merkle hash of a header.
inline constexpr size_t kHeaderPadBytes = 7;

inline void write_header(Writer& w, TraceNodeKind kind, MerkleHash merkle_hash, ContentHash content_hash) {
    w.w(CDAG_MAGIC);
    w.w(CDAG_VERSION.value());
    w.w(std::to_underlying(kind));
    w.write_zero_pad<kHeaderPadBytes>();
    w.w(merkle_hash.raw());
    w.w(content_hash.raw());
}

struct Header {
    uint32_t magic = 0;
    ExternalCdagVersion version{};
    TraceNodeKind kind{};
    MerkleHash merkle_hash;
    ContentHash content_hash;
};

inline Header read_header(Reader& r) {
    Header h{};
    h.magic = r.r<uint32_t>();
    h.version = ::fixy::mint_tagged<::fixy::tags::source::External>(r.r<uint32_t>());
    h.kind = make_trace_node_kind(
        ::fixy::mint_refined<kValidTraceNodeKindBound>(r.read_gated<uint8_t>(kValidTraceNodeKindBound)));
    r.read_zero_pad<kHeaderPadBytes>();
    h.merkle_hash = MerkleHash{r.r<uint64_t>()};
    h.content_hash = ContentHash{r.r<uint64_t>()};
    return h;
}

// The bytes of a header: the magic, the version, the kind byte, the pad, the
// merkle hash and the content hash.
inline constexpr size_t kHeaderWireBytes = 4 + 4 + 1 + kHeaderPadBytes + 8 + 8;

// The first fault of a header that read_header read, in the order of its
// fields, or none.  An image shorter than a header is truncated, and a
// different magic number or version comes before a bad byte of the kind or
// the pad, so a file of another format reports its format.  The kind of the
// loader comes last.
[[nodiscard]] inline ::fixy::Option<LoadError> header_fault(Reader const& r, Header const& hdr, TraceNodeKind kind) {
    if (r.image.size() < kHeaderWireBytes) return ::fixy::some(LoadError::truncated);
    if (hdr.magic != CDAG_MAGIC) return ::fixy::some(LoadError::bad_magic);
    if (!cdag_version_matches(hdr.version)) return ::fixy::some(LoadError::version_mismatch);
    if (r.refusal.is_some()) return r.refusal;
    if (hdr.kind != kind) return ::fixy::some(LoadError::wrong_kind);
    return ::fixy::none;
}

}  // namespace detail_ser

// The region that serialize_region reads.  A borrow is never null.
using SerializedRegion = ::fixy::BorrowedRef<const RegionNode>;

// The branch that serialize_branch reads.  A borrow is never null.
using SerializedBranch = ::fixy::BorrowedRef<const BranchNode>;

// Writes the image of the region into the first bytes of image.  Returns the
// byte count written, or zero if image was too small.  Each entry of the
// region carries its own tensor metadata, so the function reads no metadata
// log.
CRUCIBLE_COLD [[nodiscard]] inline size_t serialize_region(SerializedRegion region, ::fixy::View<std::byte> image) {
    using namespace detail_ser;
    Writer w{.image = image};

    write_header(w, TraceNodeKind::REGION, region->merkle_hash, region->content_hash);

    w.w(region->num_ops());
    w.w(region->first_op_schema.raw());
    w.w(region->measured_ms);
    w.w(region->variant_id.get());

    // The writes of the image are byte writes, and the compiler cannot prove
    // that they leave the link of the region as it is.  The copy of the link
    // keeps one test of the plan.
    const PlanLink plan_link = region->plan;
    w.w(plan_link.is_some());
    for (const ::fixy::Ref<MemoryPlan>& plan_ref : plan_link) {
        const MemoryPlan& plan = *plan_ref;
        w.w(plan.pool_bytes);
        w.w(plan.num_slots());
        w.w(plan.num_external);
        w.w(plan.device_type);
        w.w(plan.device_idx);
        w.write_zero_pad<sizeof(plan.pad0)>();
        w.w(plan.device_capability);
        w.w(plan.rank);
        w.w(plan.world_size);
        for (const TensorSlot& slot : plan.slots) {
            write_slot(w, slot);
        }
    }

    for (const TraceEntry& te : region->ops) {
        w.w(te.schema_hash.raw());
        w.w(te.shape_hash.raw());
        w.w(te.scope_hash.raw());
        w.w(te.callsite_hash.raw());
        w.w(te.num_inputs());
        w.w(te.num_outputs());
        w.w(te.num_scalar_args());
        w.w(te.grad_enabled);
        // One byte, laid out the same way the recording ring lays it out.
        {
            uint8_t flags = 0;
            if (te.inference_mode) flags |= op_flag::INFERENCE_MODE;
            if (te.is_mutable) flags |= op_flag::IS_MUTABLE;
            flags |= (static_cast<uint8_t>(te.training_phase) & 0x3) << op_flag::PHASE_SHIFT;
            if (te.torch_function) flags |= op_flag::TORCH_FUNCTION;
            w.w(flags);
        }
        w.w(std::to_underlying(te.kernel_id));
        w.w(te.depth.raw());
        w.w(te.scalar_types);

        // Each array of the entry holds its count.
        for (const TensorMeta& meta : te.input_metas()) {
            write_meta(w, meta);
        }
        for (const TensorMeta& meta : te.output_metas()) {
            write_meta(w, meta);
        }
        for (const int64_t value : te.scalar_args()) {
            w.w(value);
        }
        for (const OpIndex producer : te.input_trace_indices()) {
            w.w(producer.raw());
        }
        for (const SlotId slot_id : te.input_slot_ids()) {
            w.w(slot_id.raw());
        }
        for (const SlotId slot_id : te.output_slot_ids()) {
            w.w(slot_id.raw());
        }
    }

    return w.has_room ? w.pos : 0;
}

// Gives the region of the image, or the reason that refused the image: a
// parse error, a version mismatch, a count or a plan past its bound, or a
// content hash that the operations do not fold to.  The reason is the first
// fault of the image.  Every structure it builds lives in the arena.
CRUCIBLE_COLD [[nodiscard]] inline ::fixy::Result<LoadedRegionNode, LoadError>
deserialize_region(::foundation::effects::Alloc a, ::fixy::View<const std::byte> image, ::fixy::BumpArena& arena) {
    using namespace detail_ser;
    Reader r{.image = image};

    const Header hdr = read_header(r);
    for (const LoadError fault : header_fault(r, hdr, TraceNodeKind::REGION))
        return ::fixy::err(fault);

    const uint32_t num_ops = r.r<uint32_t>();
    if (num_ops > CDAG_MAX_OPS) return ::fixy::err(r.reason_or(LoadError::count_above_limit));
    const SchemaHash first_op_schema = SchemaHash{r.r<uint64_t>()};
    // RegionNode::set_measured_ms keeps the time finite and not negative.
    // The comparison also refuses a NaN.
    const float measured_ms =
        r.read_gated<float>([](float ms) noexcept { return ms >= 0.0f && ms <= ::fixy::max_of<float>; }, 0.0f);
    const uint32_t variant_id = r.r<uint32_t>();

    // The plan of the region, and the count of its slots.  A region with no
    // plan binds to no slot table, and its count stays zero.
    ::fixy::Option<::fixy::Ref<MemoryPlan>> plan{};
    uint32_t num_slots = 0;
    const bool has_plan = r.read_bool();
    if (has_plan) {
        const ::fixy::Ref<MemoryPlan> loaded_plan = arena.make<MemoryPlan>(a);
        plan = ::fixy::Option<::fixy::Ref<MemoryPlan>>::some(loaded_plan);
        loaded_plan->pool_bytes = r.r<uint64_t>();
        // The next three checks all guard the same thing: a loaded plan goes
        // on to initialise the replay pool, whose own preconditions are only
        // assumptions in a release build. Each bound is taken from the
        // allocator's own constant, or is the structural relation the
        // allocator assumes, so a value that survives here is one the
        // allocator can serve. The pool size bound is inclusive, so only a
        // strictly larger value is refused.
        if (loaded_plan->pool_bytes > ::crucible::PoolAllocator::kMaxPoolBytes) [[unlikely]] {
            return ::fixy::err(r.reason_or(LoadError::plan_unservable));
        }
        num_slots = r.r<uint32_t>();
        if (num_slots > CDAG_MAX_SLOTS) return ::fixy::err(r.reason_or(LoadError::count_above_limit));
        loaded_plan->num_external = r.r<uint32_t>();
        // The external slots are a subset of the slots, so their count cannot
        // exceed the total.
        if (loaded_plan->num_external > num_slots) return ::fixy::err(r.reason_or(LoadError::plan_unservable));
        // An unrecognised device type would reach pool selection unchecked.
        loaded_plan->device_type =
            make_device_type(::fixy::mint_refined<valid_device_type>(r.read_gated<int8_t>(valid_device_type)));
        loaded_plan->device_idx = r.r<int8_t>();
        r.read_zero_pad<sizeof(loaded_plan->pad0)>();
        loaded_plan->device_capability = r.r<uint64_t>();
        loaded_plan->rank = r.r<int32_t>();
        loaded_plan->world_size = r.r<int32_t>();
        if (!r.has_remaining(num_slots, kTensorSlotWireBytes)) {
            return ::fixy::err(r.reason_or(LoadError::truncated));
        }
        loaded_plan->slots = arena.array<TensorSlot>(a, num_slots, TensorSlot{});
        for (TensorSlot& slot : loaded_plan->slots) {
            slot = read_slot(r);
            // The pool allocator aborts on a slot it cannot serve, so a
            // loaded plan holds only slots that it can serve.
            if (!::crucible::PoolAllocator::can_serve_slot(slot, loaded_plan->pool_bytes)) [[unlikely]] {
                return ::fixy::err(r.reason_or(LoadError::plan_unservable));
            }
        }
    }

    // An entry is variable-length, but its fixed part of hashes, counts and
    // flags gives a floor per entry, which is enough to reject a declared op
    // count that the remaining bytes cannot possibly hold.
    constexpr size_t kTraceEntryMinWireBytes = 40;
    if (!r.has_remaining(num_ops, kTraceEntryMinWireBytes)) {
        return ::fixy::err(r.reason_or(LoadError::truncated));
    }
    const ::fixy::View<TraceEntry> ops = arena.array<TraceEntry>(a, num_ops, TraceEntry{});

    for (TraceEntry& te : ops) {
        te.schema_hash = SchemaHash{r.r<uint64_t>()};
        te.shape_hash = ShapeHash{r.r<uint64_t>()};
        te.scope_hash = ScopeHash{r.r<uint64_t>()};
        te.callsite_hash = CallsiteHash{r.r<uint64_t>()};
        const uint16_t num_inputs = r.r<uint16_t>();
        const uint16_t num_outputs = r.r<uint16_t>();
        const uint16_t num_scalar_args = r.r<uint16_t>();
        const bool is_count_above_limit =
            num_inputs > CDAG_MAX_INPUTS || num_outputs > CDAG_MAX_OUTPUTS || num_scalar_args > CDAG_MAX_SCALAR_ARGS;
        if (is_count_above_limit) return ::fixy::err(r.reason_or(LoadError::count_above_limit));
        te.grad_enabled = r.read_bool();
        {
            // The writer sets only these bits, so a byte with another bit
            // set is not an image that the writer produced.
            constexpr uint8_t kWrittenFlags =
                op_flag::INFERENCE_MODE | op_flag::IS_MUTABLE | op_flag::PHASE_MASK | op_flag::TORCH_FUNCTION;
            const uint8_t flags =
                r.read_gated<uint8_t>([](uint8_t byte) noexcept { return (byte & ~kWrittenFlags) == 0; });
            te.inference_mode = (flags & op_flag::INFERENCE_MODE) != 0;
            te.is_mutable = (flags & op_flag::IS_MUTABLE) != 0;
            te.training_phase = static_cast<TrainingPhase>((flags & op_flag::PHASE_MASK) >> op_flag::PHASE_SHIFT);
            te.torch_function = (flags & op_flag::TORCH_FUNCTION) != 0;
        }
        // The explicit bound is what rejects an out-of-range byte in every
        // build. The typed widening that follows it then holds by
        // construction.
        {
            const uint8_t raw_kernel_id = r.r<uint8_t>();
            if (raw_kernel_id >= static_cast<uint8_t>(CKernelId::NUM_KERNELS)) [[unlikely]] {
                return ::fixy::err(r.reason_or(LoadError::field_out_of_range));
            }
            te.kernel_id = make_ckernel_id(::fixy::mint_refined<kValidCKernelIdBound>(raw_kernel_id));
        }
        // Each value of the word is a depth (OpDepth).
        te.depth = OpDepth{r.r<uint16_t>()};
        // Each value of two bits is a tag, and the content hash folds only the
        // bits of the slots below the count.
        te.scalar_types = r.r<uint16_t>();

        const ::fixy::View<TensorMeta> input_metas = arena.array<TensorMeta>(a, num_inputs, TensorMeta{});
        for (TensorMeta& meta : input_metas) {
            meta = read_meta(r);
        }

        const ::fixy::View<TensorMeta> output_metas = arena.array<TensorMeta>(a, num_outputs, TensorMeta{});
        for (TensorMeta& meta : output_metas) {
            meta = read_meta(r);
        }

        const ::fixy::View<int64_t> scalar_args = arena.array<int64_t>(a, num_scalar_args, int64_t{0});
        for (int64_t& value : scalar_args) {
            value = r.r<int64_t>();
        }
        te.set_scalar_args(scalar_args);

        const ::fixy::View<OpIndex> producers = arena.array<OpIndex>(a, num_inputs, OpIndex{});
        for (OpIndex& producer : producers) {
            producer = OpIndex{r.r<uint32_t>()};
        }

        const ::fixy::View<SlotId> input_slot_ids = arena.array<SlotId>(a, num_inputs, SlotId{});
        for (SlotId& slot_id : input_slot_ids) {
            const SlotId sid = SlotId{r.r<uint32_t>()};
            // A slot id indexes the replay slot table, and replay checks only
            // that the id is not the none sentinel: the upper bound is a
            // construction-time invariant it assumes rather than tests, so
            // indexing with an out-of-range id reads past the table. When the
            // region carries a plan the table is built from that plan, so the
            // bound is known here and enforced here. A region without a plan
            // binds to no table and cannot execute until one is supplied
            // elsewhere, so there is no bound to enforce for it.
            if (has_plan && sid.is_valid() && sid.raw() >= num_slots) [[unlikely]] {
                return ::fixy::err(r.reason_or(LoadError::slot_out_of_range));
            }
            slot_id = sid;
        }
        te.set_inputs(input_metas, producers, input_slot_ids);

        const ::fixy::View<SlotId> output_slot_ids = arena.array<SlotId>(a, num_outputs, SlotId{});
        for (SlotId& slot_id : output_slot_ids) {
            const SlotId sid = SlotId{r.r<uint32_t>()};
            // The same bound as for the input slot ids above.
            if (has_plan && sid.is_valid() && sid.raw() >= num_slots) [[unlikely]] {
                return ::fixy::err(r.reason_or(LoadError::slot_out_of_range));
            }
            slot_id = sid;
        }
        te.set_outputs(output_metas, output_slot_ids);
    }

    for (const LoadError refusal : r.refusal)
        return ::fixy::err(refusal);

    // The content hash is the region's identity and the key of its kernels
    // in the cache.  The header states it, and the header comes from outside,
    // so the loader folds the operations again and refuses a region whose
    // stated hash is not the hash of its operations.  A peer that could state
    // any hash could make this region take the kernels of another one.
    //
    // The fold is the one make_region uses with no recipe.  A region hashed
    // under a recipe is refused here, because the image does not carry the
    // recipe and the loader cannot fold it.
    if (compute_content_hash(ops) != hdr.content_hash) [[unlikely]] {
        return ::fixy::err(LoadError::content_hash_mismatch);
    }
    // make_region sets the first schema from the operations, so the field is
    // not free to disagree with them.
    if (first_op_schema != first_op_schema_of(ops)) [[unlikely]] {
        return ::fixy::err(LoadError::first_schema_mismatch);
    }

    const ::fixy::Ref<RegionNode> node = arena.make<RegionNode>(a);
    node->kind = TraceNodeKind::REGION;
    node->merkle_hash = hdr.merkle_hash;
    node->content_hash = hdr.content_hash;
    node->next = ::fixy::none;
    node->ops = ops;
    node->first_op_schema = first_op_schema;
    node->measured_ms = measured_ms;
    // A new node holds the counter at zero, and each value on disk is at
    // least zero, so the advance does not go back.  A value of zero means
    // that no variant was selected when the region was written.
    node->variant_id.advance(variant_id);
    node->plan = plan;
    // The node is rebuilt from Cipher bytes, and the fold above matched the
    // content hash that the image records, so it crosses the edge to Loaded.
    return ::fixy::mint_tagged<::fixy::tags::source::Replayed>(node).retag<::fixy::tags::source::Loaded>();
}

// Writes the image of the branch into the first bytes of image.  Returns the
// byte count written, or zero if image was too small.
[[nodiscard]] inline size_t serialize_branch(SerializedBranch branch, ::fixy::View<std::byte> image) {
    using namespace detail_ser;
    Writer w{.image = image};

    // A branch node repurposes the header's content-hash field to hold the
    // merkle hash of the continuation, the suffix the arms merge back into.
    const MerkleHash cont_hash = branch->next.match(
        [](::fixy::Ref<TraceNode> const& continuation) noexcept { return continuation->merkle_hash; },
        []() noexcept { return MerkleHash{}; });
    write_header(w, TraceNodeKind::BRANCH, branch->merkle_hash, ContentHash{cont_hash.raw()});

    write_guard(w, branch->guard);

    w.w(branch->num_arms());
    for (const BranchNode::Arm& arm : branch->arms) {
        w.w(arm.value);
        // An arm with no target writes the hash zero.
        const uint64_t target_hash =
            arm.target.match([](::fixy::Ref<TraceNode> const& target) noexcept { return target->merkle_hash.raw(); },
                             []() noexcept { return uint64_t{0}; });
        w.w(target_hash);
    }

    return w.has_room ? w.pos : 0;
}

// The resolver turns each persisted merkle hash back into a link to a node,
// or into an empty link.  Gives the branch, or the reason that refused the
// image: a parse error, a version mismatch, a count past its bound, or arms
// out of the order of their values.
template <typename Resolve>
    requires std::is_invocable_r_v<TraceLink, Resolve&, MerkleHash>
[[nodiscard]] inline ::fixy::Result<::fixy::Ref<BranchNode>, LoadError>
deserialize_branch(::foundation::effects::Alloc a, ::fixy::View<const std::byte> image,
                   ::fixy::BumpArena& arena CRUCIBLE_LIFETIMEBOUND, Resolve&& resolve) {
    Resolve resolver = std::forward<Resolve>(resolve);
    using namespace detail_ser;
    Reader r{.image = image};

    const Header hdr = read_header(r);
    for (const LoadError fault : header_fault(r, hdr, TraceNodeKind::BRANCH))
        return ::fixy::err(fault);
    // The header's content-hash field holds the continuation's merkle hash.

    const Guard guard = read_guard(r);

    const uint32_t num_arms = r.r<uint32_t>();
    if (num_arms > CDAG_MAX_BRANCH_ARMS) return ::fixy::err(r.reason_or(LoadError::count_above_limit));

    // Reject a truncated body before growing the arena for the arms. The wire
    // cost is spelled out rather than taken from the in-memory arm: the two
    // sizes coincide today, but the in-memory arm holds a resolved link
    // where the wire holds a hash, so only one of them is the file's business.
    constexpr size_t kArmWireBytes = sizeof(int64_t) + sizeof(uint64_t);
    if (!r.has_remaining(num_arms, kArmWireBytes)) {
        return ::fixy::err(r.reason_or(LoadError::truncated));
    }

    const ::fixy::Ref<BranchNode> node = arena.make<BranchNode>(a);
    node->kind = TraceNodeKind::BRANCH;
    node->merkle_hash = hdr.merkle_hash;
    node->next = ::fixy::none;  // the caller resolves the continuation separately
    node->guard = guard;
    node->arms = arena.array<BranchNode::Arm>(a, num_arms, BranchNode::Arm{});
    for (BranchNode::Arm& arm : node->arms) {
        arm.value = r.r<int64_t>();
        const MerkleHash target_h = MerkleHash{r.r<uint64_t>()};
        arm.target = resolver(target_h);
    }

    // Replay finds an arm by binary search on the value, so an image with
    // unsorted arms would route a guard outcome to the wrong target.
    for (const LoadError refusal : r.refusal)
        return ::fixy::err(refusal);
    if (!node->are_arms_sorted_by_value()) return ::fixy::err(LoadError::arms_unsorted);
    return node;
}

// The branch with no resolver: the target of each arm stays empty, and the
// caller resolves the targets later.
[[nodiscard]] inline ::fixy::Result<::fixy::Ref<BranchNode>, LoadError>
deserialize_branch(::foundation::effects::Alloc a, ::fixy::View<const std::byte> image,
                   ::fixy::BumpArena& arena CRUCIBLE_LIFETIMEBOUND) {
    return deserialize_branch(a, image, arena, [](MerkleHash) noexcept { return TraceLink{}; });
}

// The twin of the two deserialize_branch overloads above.  The node is in
// the arena, and a temporary arena releases it at the end of the statement.
// The deduced parameters hold the other arguments, and no parameter of the
// twin has a type of the standard library.
template <class Buffer, class... Rest>
void deserialize_branch(::foundation::effects::Alloc, Buffer&&, ::fixy::BumpArena&&, Rest&&...) =
    delete("a temporary arena releases the node at the end of the statement. Use an arena that has a name");

}  // namespace crucible
