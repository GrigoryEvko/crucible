#pragma once

// The wire format is a 32-byte header followed by a flat payload. The header
// is a 4-byte magic, a 4-byte version, a 1-byte node kind, 7 zero pad bytes,
// an 8-byte merkle hash and an 8-byte content hash.
//
// Nothing in the format is a raw pointer, so a loaded image is independent of
// where it was written. A tensor's data pointer is written as zero, and a
// branch arm's target is written as the target's merkle hash and resolved
// through a caller-supplied lookup at load time.

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/PoolAllocator.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

#include <concepts>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible {

// The four bytes spell "CDAG" most significant first. A little-endian host
// stores them in the opposite order, so a hex dump of the file starts with
// 47 41 44 43, which prints as "GADC". Both spellings have been written down
// wrongly before; the value is what the reader compares, not the spelling.
static constexpr uint32_t CDAG_MAGIC = 0x43444147u;
using CdagFormatVersion = fixy::wrap::Tagged<uint32_t, fixy::tags::source::FormatVersion>;
using ExternalCdagVersion = fixy::wrap::Tagged<uint32_t, fixy::tags::source::External>;
using LoadedRegionNode = fixy::wrap::Tagged<RegionNode*, fixy::tags::source::Loaded>;
static_assert(sizeof(LoadedRegionNode) == sizeof(RegionNode*));
static_assert(std::is_trivially_copy_constructible_v<LoadedRegionNode>);
// 10 (2026-09-15): the content-hash fold changed mixer.  Every region on
// disk carries a content hash the current code no longer computes, so a
// version-9 file would deserialize into regions whose hashes disagree with
// their own contents and whose KernelCache entries key on nothing.  The
// bump makes cdag_version_matches reject those files outright.
static constexpr CdagFormatVersion CDAG_VERSION{10u};

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

struct Writer {
    uint8_t* buf = nullptr;
    size_t pos = 0;
    size_t max = 0;
    bool ok = true;

    void write_bytes(const void* src, size_t n) {
        if (pos + n > max) {
            ok = false;
            return;
        }
        std::memcpy(buf + pos, src, n);
        pos += n;
    }

    template <typename T>
    void w(const T& v) {
        write_bytes(&v, sizeof(T));
    }
};

struct Reader {
    const uint8_t* buf = nullptr;
    size_t pos = 0;
    size_t len = 0;
    bool ok = true;

    void read_bytes(void* dst, size_t n) {
        if (pos + n > len) {
            ok = false;
            return;
        }
        std::memcpy(dst, buf + pos, n);
        pos += n;
    }

    template <typename T>
    [[nodiscard]] T r() {
        T v{};
        read_bytes(&v, sizeof(T));
        return v;
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
    // travels through ok, which every entry point already turns into a null
    // return, and the substituted value is discarded with the rest of the
    // parse. A value that passes is returned unchanged, so the bytes a good
    // image produces are untouched.
    template <typename T, typename Pred>
    [[nodiscard]] T read_gated(Pred pred, T valid_substitute = T{0}) {
        const T v = r<T>();
        // Judge only a byte that was actually read. After a truncation the
        // value is the zero default and the failure is already recorded.
        if (ok && !pred(v)) [[unlikely]] {
            ok = false;
            return valid_substitute;
        }
        return v;
    }

    [[nodiscard]] size_t remaining() const noexcept { return (pos <= len) ? (len - pos) : 0; }

    // Ask before growing the arena for n elements. A header can claim a count
    // near the ceiling with a truncated body, and without this the arena grows
    // by the full amount before the first element read discovers the end of
    // the buffer, leaving space only a detach can reclaim.
    template <typename T>
    [[nodiscard]] bool has_remaining(size_t n) const noexcept {
        if (n == 0) return true;
        // Largest n for which the product below does not wrap.
        if (n > SIZE_MAX / sizeof(T)) return false;
        const size_t need = n * sizeof(T);
        return pos <= len && (len - pos) >= need;
    }
};

// Two fields are process-local and must never reach persisted bytes: the data
// pointer is a runtime address, and the gradient-function hash is an identity
// only this process holds. Both are written as zero, and the zero is routed
// through a type that rejects anything else, so a later edit that feeds the
// live field in fails at the write instead of quietly producing an image whose
// bytes differ between runs. Pad bytes are zero for the same reason.
inline void write_meta(Writer& w, const TensorMeta& m) {
    w.write_bytes(m.sizes.raw_data(), sizeof(m.sizes));
    w.write_bytes(m.strides.raw_data(), sizeof(m.strides));
    const fixy::wrap::Refined<fixy::wrap::is_zero, std::uint64_t> zero_ptr{std::uint64_t{0}};
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
    const fixy::wrap::Refined<fixy::wrap::is_zero, std::uint64_t> zero_grad_fn_hash{std::uint64_t{0}};
    w.w(zero_grad_fn_hash.value());
}

// The two process-local fields are reset here whatever the bytes hold, so an
// older or corrupt image cannot resurrect an address from another process.
inline TensorMeta read_meta(Reader& r) {
    TensorMeta m{};
    for (uint8_t d = 0; d < kMaxTensorNDim; ++d) {
        m.sizes[d] = tensor_dim(r.r<int64_t>());
    }
    for (uint8_t d = 0; d < kMaxTensorNDim; ++d) {
        m.strides[d] = tensor_dim(r.r<int64_t>());
    }
    (void)r.r<uint64_t>();  // the persisted data pointer, deliberately dropped
    m.data_ptr = external_data_ptr(nullptr);
    // The rank is bounded by the fixed width of the size and stride arrays.
    m.ndim = make_ndim(ValidNDim{r.read_gated<uint8_t>(::crucible::fixy::wrap::bounded_above<kMaxTensorNDim>)});
    // The scalar-type enumerators are sparse, and a value outside the set
    // reaches a switch whose default is marked unreachable.
    m.dtype = make_scalar_type(ValidScalarType{r.read_gated<int8_t>(valid_scalar_type)});
    // The device type and the layout both feed the content hash, which is the
    // node's identity, so an unrecognised value would not fail loudly. It
    // would produce a node whose identity silently disagrees with the one that
    // was written.
    m.device_type = make_device_type(ValidDeviceType{r.read_gated<int8_t>(valid_device_type)});
    m.device_idx = r.r<int8_t>();
    m.layout = make_layout(ValidLayout{r.read_gated<int8_t>(valid_layout)});
    m.requires_grad = r.r<bool>();
    m.flags = r.r<uint8_t>();
    m.output_nr = r.r<uint8_t>();
    m.storage_offset = r.r<int64_t>();
    m.version = r.r<uint32_t>();
    m.storage_nbytes = r.r<uint32_t>();
    (void)r.r<uint64_t>();  // the persisted gradient-function hash, dropped
    m.grad_fn_hash = grad_fn_hash(0);
    return m;
}

inline void write_header(Writer& w, TraceNodeKind kind, MerkleHash merkle_hash, ContentHash content_hash) {
    w.w(CDAG_MAGIC);
    w.w(CDAG_VERSION.value());
    w.w(std::to_underlying(kind));
    const uint8_t pad7[7] = {};
    w.write_bytes(pad7, 7);
    w.w(merkle_hash.raw());
    w.w(content_hash.raw());
}

struct Header {
    uint32_t magic = 0;
    ExternalCdagVersion version{0};
    TraceNodeKind kind{};
    MerkleHash merkle_hash;
    ContentHash content_hash;
};

inline Header read_header(Reader& r) {
    Header h{};
    h.magic = r.r<uint32_t>();
    h.version = ExternalCdagVersion{r.r<uint32_t>()};
    h.kind = make_trace_node_kind(ValidTraceNodeKindRaw{
        r.read_gated<uint8_t>(::crucible::fixy::wrap::bounded_above<static_cast<uint8_t>(TraceNodeKind::TERMINAL)>)});
    uint8_t pad7[7]{};
    r.read_bytes(pad7, 7);
    h.merkle_hash = MerkleHash{r.r<uint64_t>()};
    h.content_hash = ContentHash{r.r<uint64_t>()};
    return h;
}

}  // namespace detail_ser

// Returns the byte count written, or zero if the buffer was too small. The
// meta log is unused: each entry already carries its own tensor metadata.
[[nodiscard]] inline size_t serialize_region(const RegionNode* region, const MetaLog* /*meta_log*/,
                                             std::span<uint8_t> buf) {
    using namespace detail_ser;
    Writer w{.buf = buf.data(), .pos = 0, .max = buf.size()};

    write_header(w, TraceNodeKind::REGION, region->merkle_hash, region->content_hash);

    w.w(region->num_ops);
    w.w(region->first_op_schema.raw());
    w.w(region->measured_ms);
    w.w(region->variant_id.get());

    const bool has_plan = (region->plan != nullptr);
    w.w(has_plan);
    if (has_plan) {
        const MemoryPlan* plan = region->plan;
        w.w(plan->pool_bytes);
        w.w(plan->num_slots);
        w.w(plan->num_external);
        w.w(plan->device_type);
        w.w(plan->device_idx);
        w.write_bytes(plan->pad0, sizeof(plan->pad0));
        w.w(plan->device_capability);
        w.w(plan->rank);
        w.w(plan->world_size);
        // A slot holds no pointers, so it goes to disk verbatim.
        for (uint32_t s = 0; s < plan->num_slots; s++) {
            w.write_bytes(&plan->slots[s], sizeof(TensorSlot));
        }
    }

    for (uint32_t i = 0; i < region->num_ops; i++) {
        const TraceEntry& te = region->ops[i];

        w.w(te.schema_hash.raw());
        w.w(te.shape_hash.raw());
        w.w(te.scope_hash.raw());
        w.w(te.callsite_hash.raw());
        w.w(te.num_inputs);
        w.w(te.num_outputs);
        w.w(te.num_scalar_args);
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

        for (uint16_t j = 0; j < te.num_inputs; j++) {
            write_meta(w, te.input_metas ? te.input_metas[j] : TensorMeta{});
        }
        for (uint16_t j = 0; j < te.num_outputs; j++) {
            write_meta(w, te.output_metas ? te.output_metas[j] : TensorMeta{});
        }
        for (uint16_t j = 0; j < te.num_scalar_args; j++) {
            const int64_t val = (te.scalar_args) ? te.scalar_args[j] : int64_t{0};
            w.w(val);
        }
        for (uint16_t j = 0; j < te.num_inputs; j++) {
            const uint32_t idx = te.input_trace_indices ? te.input_trace_indices[j].raw() : UINT32_MAX;
            w.w(idx);
        }
        for (uint16_t j = 0; j < te.num_inputs; j++) {
            const uint32_t sid = te.input_slot_ids ? te.input_slot_ids[j].raw() : UINT32_MAX;
            w.w(sid);
        }
        for (uint16_t j = 0; j < te.num_outputs; j++) {
            const uint32_t sid = te.output_slot_ids ? te.output_slot_ids[j].raw() : UINT32_MAX;
            w.w(sid);
        }
    }

    return w.ok ? w.pos : 0;
}

// Returns a tagged null pointer on a parse error or a version mismatch. Every
// structure it builds lives in the arena.
[[nodiscard]] inline LoadedRegionNode deserialize_region(effects::Alloc a, std::span<const uint8_t> buf, Arena& arena) {
    using namespace detail_ser;
    Reader r{.buf = buf.data(), .pos = 0, .len = buf.size()};

    const Header hdr = read_header(r);
    if (!r.ok || hdr.magic != CDAG_MAGIC || !cdag_version_matches(hdr.version) || hdr.kind != TraceNodeKind::REGION) {
        return LoadedRegionNode{nullptr};
    }

    const uint32_t num_ops = r.r<uint32_t>();
    if (num_ops > CDAG_MAX_OPS) return LoadedRegionNode{nullptr};
    const SchemaHash first_op_schema = SchemaHash{r.r<uint64_t>()};
    const float measured_ms = r.r<float>();
    const uint32_t variant_id = r.r<uint32_t>();

    MemoryPlan* plan = nullptr;
    const bool has_plan = r.r<bool>();
    if (has_plan) {
        plan = arena.alloc_obj<MemoryPlan>(a);
        plan->pool_bytes = r.r<uint64_t>();
        // The next three checks all guard the same thing: a loaded plan goes
        // on to initialise the replay pool, whose own preconditions are only
        // assumptions in a release build. Each bound is taken from the
        // allocator's own constant, or is the structural relation the
        // allocator assumes, so a value that survives here is one the
        // allocator can serve. The pool size bound is inclusive, so only a
        // strictly larger value is refused.
        if (plan->pool_bytes > ::crucible::PoolAllocator::kMaxPoolBytes) [[unlikely]] {
            return LoadedRegionNode{nullptr};
        }
        plan->num_slots = r.r<uint32_t>();
        if (plan->num_slots > CDAG_MAX_SLOTS) return LoadedRegionNode{nullptr};
        plan->num_external = r.r<uint32_t>();
        // The external slots are a subset of the slots, so their count cannot
        // exceed the total.
        if (plan->num_external > plan->num_slots) return LoadedRegionNode{nullptr};
        // An unrecognised device type would reach pool selection unchecked.
        plan->device_type = make_device_type(ValidDeviceType{r.read_gated<int8_t>(valid_device_type)});
        plan->device_idx = r.r<int8_t>();
        r.read_bytes(plan->pad0, sizeof(plan->pad0));
        plan->device_capability = r.r<uint64_t>();
        plan->rank = r.r<int32_t>();
        plan->world_size = r.r<int32_t>();
        if (plan->num_slots > 0) {
            // The product cannot overflow: the count is already bounded by the
            // slot ceiling and the element size is a small constant.
            const uint64_t slot_bytes = static_cast<uint64_t>(plan->num_slots) * sizeof(TensorSlot);
            if (r.pos + slot_bytes > r.len) return LoadedRegionNode{nullptr};
            plan->slots = arena.alloc_array<TensorSlot>(a, plan->num_slots);
            for (uint32_t s = 0; s < plan->num_slots; s++) {
                r.read_bytes(&plan->slots[s], sizeof(TensorSlot));
            }
        } else {
            plan->slots = nullptr;
        }
    }

    // An entry is variable-length, but its fixed part of hashes, counts and
    // flags gives a floor per entry, which is enough to reject a declared op
    // count that the remaining bytes cannot possibly hold.
    constexpr size_t kTraceEntryMinWireBytes = 40;
    if (num_ops > 0 && r.remaining() < static_cast<size_t>(num_ops) * kTraceEntryMinWireBytes) {
        return LoadedRegionNode{nullptr};
    }
    TraceEntry* ops = (num_ops > 0) ? arena.alloc_array<TraceEntry>(a, num_ops) : nullptr;

    for (uint32_t i = 0; i < num_ops; i++) {
        TraceEntry& te = ops[i];
        te.schema_hash = SchemaHash{r.r<uint64_t>()};
        te.shape_hash = ShapeHash{r.r<uint64_t>()};
        te.scope_hash = ScopeHash{r.r<uint64_t>()};
        te.callsite_hash = CallsiteHash{r.r<uint64_t>()};
        te.num_inputs = r.r<uint16_t>();
        te.num_outputs = r.r<uint16_t>();
        te.num_scalar_args = r.r<uint16_t>();
        if (te.num_inputs > CDAG_MAX_INPUTS) return LoadedRegionNode{nullptr};
        if (te.num_outputs > CDAG_MAX_OUTPUTS) return LoadedRegionNode{nullptr};
        if (te.num_scalar_args > CDAG_MAX_SCALAR_ARGS) return LoadedRegionNode{nullptr};
        te.grad_enabled = r.r<bool>();
        {
            const uint8_t flags = r.r<uint8_t>();
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
                return LoadedRegionNode{nullptr};
            }
            te.kernel_id = make_ckernel_id(ValidCKernelIdRaw{raw_kernel_id});
        }

        te.input_metas = (te.num_inputs > 0) ? arena.alloc_array<TensorMeta>(a, te.num_inputs) : nullptr;
        for (uint16_t j = 0; j < te.num_inputs; j++) {
            te.input_metas[j] = read_meta(r);
        }

        te.output_metas = (te.num_outputs > 0) ? arena.alloc_array<TensorMeta>(a, te.num_outputs) : nullptr;
        for (uint16_t j = 0; j < te.num_outputs; j++) {
            te.output_metas[j] = read_meta(r);
        }

        te.scalar_args = (te.num_scalar_args > 0) ? arena.alloc_array<int64_t>(a, te.num_scalar_args) : nullptr;
        for (uint16_t j = 0; j < te.num_scalar_args; j++) {
            te.scalar_args[j] = r.r<int64_t>();
        }

        te.input_trace_indices = (te.num_inputs > 0) ? arena.alloc_array<OpIndex>(a, te.num_inputs) : nullptr;
        for (uint16_t j = 0; j < te.num_inputs; j++) {
            te.input_trace_indices[j] = OpIndex{r.r<uint32_t>()};
        }

        te.input_slot_ids = (te.num_inputs > 0) ? arena.alloc_array<SlotId>(a, te.num_inputs) : nullptr;
        for (uint16_t j = 0; j < te.num_inputs; j++) {
            const SlotId sid = SlotId{r.r<uint32_t>()};
            // A slot id indexes the replay slot table, and replay checks only
            // that the id is not the none sentinel: the upper bound is a
            // construction-time invariant it assumes rather than tests, so
            // indexing with an out-of-range id reads past the table. When the
            // region carries a plan the table is built from that plan, so the
            // bound is known here and enforced here. A region without a plan
            // binds to no table and cannot execute until one is supplied
            // elsewhere, so there is no bound to enforce for it.
            if (plan != nullptr && sid.is_valid() && sid.raw() >= plan->num_slots) [[unlikely]] {
                return LoadedRegionNode{nullptr};
            }
            te.input_slot_ids[j] = sid;
        }

        te.output_slot_ids = (te.num_outputs > 0) ? arena.alloc_array<SlotId>(a, te.num_outputs) : nullptr;
        for (uint16_t j = 0; j < te.num_outputs; j++) {
            const SlotId sid = SlotId{r.r<uint32_t>()};
            // The same bound as for the input slot ids above.
            if (plan != nullptr && sid.is_valid() && sid.raw() >= plan->num_slots) [[unlikely]] {
                return LoadedRegionNode{nullptr};
            }
            te.output_slot_ids[j] = sid;
        }
    }

    if (!r.ok) return LoadedRegionNode{nullptr};

    // Placement new rather than a plain cast: the node holds an atomic field.
    auto* node = new(arena.alloc_obj<RegionNode>(a)) RegionNode{};
    node->kind = TraceNodeKind::REGION;
    node->merkle_hash = hdr.merkle_hash;
    node->content_hash = hdr.content_hash;
    node->next = nullptr;
    node->ops = ops;
    node->num_ops = num_ops;
    node->first_op_schema = first_op_schema;
    node->measured_ms = measured_ms;
    // The counter is reconstructed rather than advanced. The value on disk may
    // be zero, meaning no variant had been selected when the region was
    // written, and advancing to zero is exactly what the counter forbids.
    std::construct_at(&node->variant_id, RegionNode::VariantCounter{variant_id});
    node->plan = plan;
    return LoadedRegionNode{node};
}

// Returns the byte count written, or zero if the buffer was too small.
[[nodiscard]] inline size_t serialize_branch(const BranchNode* branch, std::span<uint8_t> buf) {
    using namespace detail_ser;
    Writer w{.buf = buf.data(), .pos = 0, .max = buf.size()};

    // A branch node repurposes the header's content-hash field to hold the
    // merkle hash of the continuation, the suffix the arms merge back into.
    const MerkleHash cont_hash = branch->next ? branch->next->merkle_hash : MerkleHash{};
    write_header(w, TraceNodeKind::BRANCH, branch->merkle_hash, ContentHash{cont_hash.raw()});

    // The guard holds no pointers, so it goes to disk verbatim.
    w.write_bytes(&branch->guard, sizeof(Guard));

    w.w(branch->num_arms);
    for (uint32_t i = 0; i < branch->num_arms; i++) {
        w.w(branch->arms[i].value);
        const uint64_t target_hash = branch->arms[i].target ? branch->arms[i].target->merkle_hash.raw() : uint64_t{0};
        w.w(target_hash);
    }

    return w.ok ? w.pos : 0;
}

// The resolver turns each persisted merkle hash back into a node pointer.
// Returns null on a parse error.
template <typename Resolve>
    requires std::is_invocable_r_v<TraceNode*, Resolve&, MerkleHash>
[[nodiscard]] inline BranchNode* deserialize_branch(effects::Alloc a, std::span<const uint8_t> buf,
                                                    Arena& arena CRUCIBLE_LIFETIMEBOUND, Resolve&& resolve) {
    Resolve resolver = std::forward<Resolve>(resolve);
    using namespace detail_ser;
    Reader r{.buf = buf.data(), .pos = 0, .len = buf.size()};

    const Header hdr = read_header(r);
    if (!r.ok || hdr.magic != CDAG_MAGIC || !cdag_version_matches(hdr.version) || hdr.kind != TraceNodeKind::BRANCH) {
        return nullptr;
    }
    // The header's content-hash field holds the continuation's merkle hash.

    Guard guard{};
    r.read_bytes(&guard, sizeof(Guard));

    const uint32_t num_arms = r.r<uint32_t>();
    if (num_arms > CDAG_MAX_BRANCH_ARMS) return nullptr;

    // Reject a truncated body before growing the arena for the arms. The wire
    // cost is spelled out rather than taken from the in-memory arm: the two
    // sizes coincide today, but the in-memory arm holds a resolved pointer
    // where the wire holds a hash, so only one of them is the file's business.
    constexpr size_t kArmWireBytes = sizeof(int64_t) + sizeof(uint64_t);
    if (num_arms > 0 && r.remaining() < static_cast<size_t>(num_arms) * kArmWireBytes) {
        return nullptr;
    }

    auto* node = arena.alloc_obj<BranchNode>(a);
    ::new(node) BranchNode{};
    node->kind = TraceNodeKind::BRANCH;
    node->merkle_hash = hdr.merkle_hash;
    node->next = nullptr;  // the caller resolves the continuation separately
    node->guard = guard;
    node->num_arms = num_arms;
    node->pad1 = 0;

    if (num_arms > 0) {
        node->arms = arena.alloc_array<BranchNode::Arm>(a, num_arms);
        for (uint32_t i = 0; i < num_arms; i++) {
            node->arms[i].value = r.r<int64_t>();
            const MerkleHash target_h = MerkleHash{r.r<uint64_t>()};
            node->arms[i].target = resolver(target_h);
        }
    } else {
        node->arms = nullptr;
    }

    return r.ok ? node : nullptr;
}

[[nodiscard]] inline BranchNode* deserialize_branch(effects::Alloc a, std::span<const uint8_t> buf,
                                                    Arena& arena CRUCIBLE_LIFETIMEBOUND, std::nullptr_t) {
    return deserialize_branch(a, buf, arena, [](MerkleHash) noexcept -> TraceNode* { return nullptr; });
}

}  // namespace crucible
