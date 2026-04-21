#pragma once

// Binary serialization for Merkle DAG nodes.
//
// Wire format: CDAG magic (4B) + version (4B) + kind (1B) + pad (7B)
//              + merkle_hash (8B) + content_hash (8B) + flat payload
//
// Position-independent: no raw pointers in the wire format.
// TensorMeta.data_ptr is written as 0 (runtime address, meaningless persisted).
// BranchNode arm targets are encoded as merkle_hash references, resolved on load
// via a caller-supplied callback.
//
// Zero external dependencies: C++26 standard library only.

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <utility>

namespace crucible {

static constexpr uint32_t CDAG_MAGIC   = 0x43444147u; // 'GDAG' LE
static constexpr uint32_t CDAG_VERSION = 8u;           // v8: Guard::hash reflection-based (full-field fold incl. pad); v7 hashes invalid

// Hard caps on header-declared counts.  Real traces top out around
// 10^5 ops / 10 inputs per op; the extra order of magnitude is slack.
// Reject adversarial headers up front so we don't attempt TB-scale
// allocations before discovering the body is truncated.
static constexpr uint32_t CDAG_MAX_OPS          = 1u << 22;   // 4 M ops
static constexpr uint32_t CDAG_MAX_SLOTS        = 1u << 22;
static constexpr uint16_t CDAG_MAX_INPUTS       = 1024;
static constexpr uint16_t CDAG_MAX_OUTPUTS      = 1024;
static constexpr uint16_t CDAG_MAX_SCALAR_ARGS  = 256;
static constexpr uint32_t CDAG_MAX_BRANCH_ARMS  = 1u << 16;   // 64 K arms

// ═══════════════════════════════════════════════════════════════════
// Internal Writer/Reader — linear cursor with overflow detection.
// ═══════════════════════════════════════════════════════════════════

namespace detail_ser {

struct Writer {
    uint8_t* buf = nullptr;
    size_t   pos = 0;
    size_t   max = 0;
    bool     ok = true;

    void write_bytes(const void* src, size_t n) {
        if (pos + n > max) { ok = false; return; }
        std::memcpy(buf + pos, src, n);
        pos += n;
    }

    template <typename T>
    void w(const T& v) { write_bytes(&v, sizeof(T)); }
};

struct Reader {
    const uint8_t* buf = nullptr;
    size_t         pos = 0;
    size_t         len = 0;
    bool           ok = true;

    void read_bytes(void* dst, size_t n) {
        if (pos + n > len) { ok = false; return; }
        std::memcpy(dst, buf + pos, n);
        pos += n;
    }

    template <typename T>
    [[nodiscard]] T r() { T v{}; read_bytes(&v, sizeof(T)); return v; }
};

// Write TensorMeta with data_ptr zeroed (runtime address is not meaningful).
// Pad bytes are written as zero for deterministic serialization.
inline void write_meta(Writer& w, const TensorMeta& m) {
    w.write_bytes(m.sizes,   sizeof(m.sizes));
    w.write_bytes(m.strides, sizeof(m.strides));
    // data_ptr → always 0 on disk
    const uint64_t zero_ptr = 0;
    w.w(zero_ptr);
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
    w.w(m.grad_fn_hash);
}

// Read TensorMeta: data_ptr is always null after deserialization.
inline TensorMeta read_meta(Reader& r) {
    TensorMeta m{};
    r.read_bytes(m.sizes,   sizeof(m.sizes));
    r.read_bytes(m.strides, sizeof(m.strides));
    (void)r.r<uint64_t>(); // data_ptr (discarded)
    m.data_ptr   = nullptr;
    m.ndim       = r.r<uint8_t>();
    m.dtype      = r.r<ScalarType>();
    m.device_type = r.r<DeviceType>();
    m.device_idx  = r.r<int8_t>();
    m.layout          = r.r<Layout>();
    m.requires_grad   = r.r<bool>();
    m.flags           = r.r<uint8_t>();
    m.output_nr       = r.r<uint8_t>();
    m.storage_offset  = r.r<int64_t>();
    m.version         = r.r<uint32_t>();
    m.storage_nbytes  = r.r<uint32_t>();
    m.grad_fn_hash    = r.r<uint64_t>();
    return m;
}

// Write the common CDAG header (32B).
inline void write_header(Writer& w, TraceNodeKind kind,
                         MerkleHash merkle_hash, ContentHash content_hash) {
    w.w(CDAG_MAGIC);
    w.w(CDAG_VERSION);
    w.w(std::to_underlying(kind));
    const uint8_t pad7[7] = {};
    w.write_bytes(pad7, 7);
    w.w(merkle_hash.raw());
    w.w(content_hash.raw());
}

struct Header {
    uint32_t      magic = 0;
    uint32_t      version = 0;
    TraceNodeKind kind{};            // strong-typed (was raw uint8_t)
    MerkleHash    merkle_hash;
    ContentHash   content_hash;
};

inline Header read_header(Reader& r) {
    Header h{};
    h.magic   = r.r<uint32_t>();
    h.version = r.r<uint32_t>();
    h.kind    = static_cast<TraceNodeKind>(r.r<uint8_t>());
    uint8_t pad7[7]{};
    r.read_bytes(pad7, 7);
    h.merkle_hash  = MerkleHash{r.r<uint64_t>()};
    h.content_hash = ContentHash{r.r<uint64_t>()};
    return h;
}

} // namespace detail_ser

// ═══════════════════════════════════════════════════════════════════
// serialize_region
// Returns bytes written, or 0 on buffer overflow.
// meta_log is reserved for future use (metas already inlined in TraceEntry).
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline size_t serialize_region(
    const RegionNode* region,
    const MetaLog*    /*meta_log*/,
    std::span<uint8_t> buf)
{
    using namespace detail_ser;
    Writer w{.buf = buf.data(), .pos = 0, .max = buf.size()};

    write_header(w, TraceNodeKind::REGION,
                 region->merkle_hash, region->content_hash);

    // Region fixed fields
    w.w(region->num_ops);
    w.w(region->first_op_schema.raw());
    w.w(region->measured_ms);
    w.w(region->variant_id);

    // MemoryPlan (optional)
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
        // TensorSlot is 40B with no pointers — verbatim.
        for (uint32_t s = 0; s < plan->num_slots; s++) {
            w.write_bytes(&plan->slots[s], sizeof(TensorSlot));
        }
    }

    // TraceEntries
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
        // Pack all op_flags into one byte (same layout as TraceRing::Entry::op_flags).
        {
            uint8_t flags = 0;
            if (te.inference_mode) flags |= op_flag::INFERENCE_MODE;
            if (te.is_mutable)     flags |= op_flag::IS_MUTABLE;
            flags |= (static_cast<uint8_t>(te.training_phase) & 0x3)
                     << op_flag::PHASE_SHIFT;
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
            const uint32_t idx = te.input_trace_indices
                ? te.input_trace_indices[j].raw() : UINT32_MAX;
            w.w(idx);
        }
        for (uint16_t j = 0; j < te.num_inputs; j++) {
            const uint32_t sid = te.input_slot_ids
                ? te.input_slot_ids[j].raw() : UINT32_MAX;
            w.w(sid);
        }
        for (uint16_t j = 0; j < te.num_outputs; j++) {
            const uint32_t sid = te.output_slot_ids
                ? te.output_slot_ids[j].raw() : UINT32_MAX;
            w.w(sid);
        }
    }

    return w.ok ? w.pos : 0;
}

// ═══════════════════════════════════════════════════════════════════
// deserialize_region
// Returns nullptr on parse error or version mismatch.
// All structures are arena-allocated; data_ptr is always null.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline RegionNode* deserialize_region(
    fx::Alloc                a,
    std::span<const uint8_t> buf,
    Arena&                   arena)
{
    using namespace detail_ser;
    Reader r{.buf = buf.data(), .pos = 0, .len = buf.size()};

    const Header hdr = read_header(r);
    if (!r.ok
        || hdr.magic   != CDAG_MAGIC
        || hdr.version != CDAG_VERSION
        || hdr.kind    != TraceNodeKind::REGION) {
        return nullptr;
    }

    const uint32_t   num_ops         = r.r<uint32_t>();
    if (num_ops > CDAG_MAX_OPS) return nullptr;
    const SchemaHash first_op_schema = SchemaHash{r.r<uint64_t>()};
    const float      measured_ms     = r.r<float>();
    const uint32_t   variant_id      = r.r<uint32_t>();

    // MemoryPlan
    MemoryPlan* plan    = nullptr;
    const bool has_plan = r.r<bool>();
    if (has_plan) {
        plan                   = arena.alloc_obj<MemoryPlan>(a);
        plan->pool_bytes        = r.r<uint64_t>();
        plan->num_slots         = r.r<uint32_t>();
        if (plan->num_slots > CDAG_MAX_SLOTS) return nullptr;
        plan->num_external      = r.r<uint32_t>();
        plan->device_type       = r.r<DeviceType>();
        plan->device_idx        = r.r<int8_t>();
        r.read_bytes(plan->pad0, sizeof(plan->pad0));
        plan->device_capability = r.r<uint64_t>();
        plan->rank              = r.r<int32_t>();
        plan->world_size        = r.r<int32_t>();
        if (plan->num_slots > 0) {
            // Pre-flight size check: before allocating num_slots * sizeof
            // TensorSlot bytes in the arena, verify the reader has that
            // many bytes remaining.  Adversarial input claiming
            // num_slots = CDAG_MAX_SLOTS with a short buffer would otherwise
            // succeed at alloc_array (grows the arena) then fail read-by-
            // read — wasting arena space that only detach() can reclaim.
            // Multiplication bound: num_slots ≤ CDAG_MAX_SLOTS; sizeof
            // TensorSlot is a small compile-time constant; product fits
            // uint64_t.
            const uint64_t slot_bytes =
                static_cast<uint64_t>(plan->num_slots) * sizeof(TensorSlot);
            if (r.pos + slot_bytes > r.len) return nullptr;
            plan->slots = arena.alloc_array<TensorSlot>(a, plan->num_slots);
            for (uint32_t s = 0; s < plan->num_slots; s++) {
                r.read_bytes(&plan->slots[s], sizeof(TensorSlot));
            }
        } else {
            plan->slots = nullptr;
        }
    }

    // TraceEntries
    TraceEntry* ops = (num_ops > 0)
        ? arena.alloc_array<TraceEntry>(a, num_ops) : nullptr;

    for (uint32_t i = 0; i < num_ops; i++) {
        TraceEntry& te      = ops[i];
        te.schema_hash      = SchemaHash{r.r<uint64_t>()};
        te.shape_hash       = ShapeHash{r.r<uint64_t>()};
        te.scope_hash       = ScopeHash{r.r<uint64_t>()};
        te.callsite_hash    = CallsiteHash{r.r<uint64_t>()};
        te.num_inputs       = r.r<uint16_t>();
        te.num_outputs      = r.r<uint16_t>();
        te.num_scalar_args  = r.r<uint16_t>();
        if (te.num_inputs       > CDAG_MAX_INPUTS)      return nullptr;
        if (te.num_outputs      > CDAG_MAX_OUTPUTS)     return nullptr;
        if (te.num_scalar_args  > CDAG_MAX_SCALAR_ARGS) return nullptr;
        te.grad_enabled     = r.r<bool>();
        // Unpack op_flags byte (same layout as TraceRing::Entry::op_flags).
        {
            const uint8_t flags = r.r<uint8_t>();
            te.inference_mode  = (flags & op_flag::INFERENCE_MODE) != 0;
            te.is_mutable      = (flags & op_flag::IS_MUTABLE) != 0;
            te.training_phase  = static_cast<TrainingPhase>(
                (flags & op_flag::PHASE_MASK) >> op_flag::PHASE_SHIFT);
            te.torch_function  = (flags & op_flag::TORCH_FUNCTION) != 0;
        }
        te.kernel_id        = static_cast<CKernelId>(r.r<uint8_t>());

        te.input_metas = (te.num_inputs > 0)
            ? arena.alloc_array<TensorMeta>(a, te.num_inputs) : nullptr;
        for (uint16_t j = 0; j < te.num_inputs; j++) {
            te.input_metas[j] = read_meta(r);
        }

        te.output_metas = (te.num_outputs > 0)
            ? arena.alloc_array<TensorMeta>(a, te.num_outputs) : nullptr;
        for (uint16_t j = 0; j < te.num_outputs; j++) {
            te.output_metas[j] = read_meta(r);
        }

        te.scalar_args = (te.num_scalar_args > 0)
            ? arena.alloc_array<int64_t>(a, te.num_scalar_args) : nullptr;
        for (uint16_t j = 0; j < te.num_scalar_args; j++) {
            te.scalar_args[j] = r.r<int64_t>();
        }

        te.input_trace_indices = (te.num_inputs > 0)
            ? arena.alloc_array<OpIndex>(a, te.num_inputs) : nullptr;
        for (uint16_t j = 0; j < te.num_inputs; j++) {
            te.input_trace_indices[j] = OpIndex{r.r<uint32_t>()};
        }

        te.input_slot_ids = (te.num_inputs > 0)
            ? arena.alloc_array<SlotId>(a, te.num_inputs) : nullptr;
        for (uint16_t j = 0; j < te.num_inputs; j++) {
            te.input_slot_ids[j] = SlotId{r.r<uint32_t>()};
        }

        te.output_slot_ids = (te.num_outputs > 0)
            ? arena.alloc_array<SlotId>(a, te.num_outputs) : nullptr;
        for (uint16_t j = 0; j < te.num_outputs; j++) {
            te.output_slot_ids[j] = SlotId{r.r<uint32_t>()};
        }
    }

    if (!r.ok) return nullptr;

    // Construct RegionNode in arena (atomic field requires placement new).
    auto* node = new (arena.alloc_obj<RegionNode>(a)) RegionNode{};
    node->kind            = TraceNodeKind::REGION;
    node->merkle_hash     = hdr.merkle_hash;
    node->content_hash    = hdr.content_hash;
    node->next            = nullptr;
    node->ops             = ops;
    node->num_ops         = num_ops;
    node->first_op_schema = first_op_schema;
    node->measured_ms     = measured_ms;
    node->variant_id      = variant_id;
    node->plan            = plan;
    node->compiled.store(nullptr, std::memory_order_release);
    return node;
}

// ═══════════════════════════════════════════════════════════════════
// serialize_branch
// Encodes arm targets as merkle_hash references (resolved on load).
// Returns bytes written, or 0 on overflow.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline size_t serialize_branch(
    const BranchNode* branch,
    std::span<uint8_t> buf)
{
    using namespace detail_ser;
    Writer w{.buf = buf.data(), .pos = 0, .max = buf.size()};

    // For BRANCH nodes, the second 8B slot in the header stores the
    // continuation's merkle_hash (shared suffix after all arms merge).
    // Note: content_hash slot is repurposed for continuation merkle_hash.
    const MerkleHash cont_hash = branch->next ? branch->next->merkle_hash : MerkleHash{};
    write_header(w, TraceNodeKind::BRANCH, branch->merkle_hash,
                 ContentHash{cont_hash.raw()});

    // Guard (12B verbatim — no pointers)
    w.write_bytes(&branch->guard, sizeof(Guard));

    // Arms: value + target merkle_hash
    w.w(branch->num_arms);
    for (uint32_t i = 0; i < branch->num_arms; i++) {
        w.w(branch->arms[i].value);
        const uint64_t target_hash = branch->arms[i].target
            ? branch->arms[i].target->merkle_hash.raw() : uint64_t{0};
        w.w(target_hash);
    }

    return w.ok ? w.pos : 0;
}

// ═══════════════════════════════════════════════════════════════════
// deserialize_branch
// resolve(merkle_hash) → TraceNode* reconstructs pointer targets.
// Returns nullptr on parse error.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline BranchNode* deserialize_branch(
    fx::Alloc                                 a,
    std::span<const uint8_t>                  buf,
    Arena&                                    arena CRUCIBLE_LIFETIMEBOUND,
    std::function<TraceNode*(MerkleHash)>     resolve)
{
    using namespace detail_ser;
    Reader r{.buf = buf.data(), .pos = 0, .len = buf.size()};

    const Header hdr = read_header(r);
    if (!r.ok
        || hdr.magic   != CDAG_MAGIC
        || hdr.version != CDAG_VERSION
        || hdr.kind    != TraceNodeKind::BRANCH) {
        return nullptr;
    }
    // hdr.content_hash holds the continuation's merkle_hash (see serialize_branch)

    Guard guard{};
    r.read_bytes(&guard, sizeof(Guard));

    const uint32_t num_arms = r.r<uint32_t>();
    if (num_arms > CDAG_MAX_BRANCH_ARMS) return nullptr;

    auto* node = arena.alloc_obj<BranchNode>(a);
    ::new (node) BranchNode{};
    node->kind        = TraceNodeKind::BRANCH;
    node->merkle_hash = hdr.merkle_hash;
    node->next        = nullptr; // caller resolves continuation separately
    node->guard       = guard;
    node->num_arms    = num_arms;
    node->pad1        = 0;

    if (num_arms > 0) {
        node->arms = arena.alloc_array<BranchNode::Arm>(a, num_arms);
        for (uint32_t i = 0; i < num_arms; i++) {
            node->arms[i].value          = r.r<int64_t>();
            const MerkleHash target_h    = MerkleHash{r.r<uint64_t>()};
            node->arms[i].target         = resolve ? resolve(target_h) : nullptr;
        }
    } else {
        node->arms = nullptr;
    }

    return r.ok ? node : nullptr;
}

} // namespace crucible
