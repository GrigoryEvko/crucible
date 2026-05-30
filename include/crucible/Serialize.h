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
#include <crucible/PoolAllocator.h>  // kMaxPoolBytes: the init() pre this load gate mirrors
#include <crucible/fixy/Source.h>   // FIXY-U-096t: tags::source::* provenance
#include <crucible/fixy/Wrap.h>     // FIXY-U-096t: Tagged via the fixy umbrella

#include <concepts>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible {

static constexpr uint32_t CDAG_MAGIC   = 0x43444147u; // 'GDAG' LE
using CdagFormatVersion = fixy::wrap::Tagged<uint32_t, fixy::tags::source::FormatVersion>;
using ExternalCdagVersion = fixy::wrap::Tagged<uint32_t, fixy::tags::source::External>;
using LoadedRegionNode = fixy::wrap::Tagged<RegionNode*, fixy::tags::source::Loaded>;
static_assert(sizeof(LoadedRegionNode) == sizeof(RegionNode*));
static_assert(std::is_trivially_copy_constructible_v<LoadedRegionNode>);
static constexpr CdagFormatVersion CDAG_VERSION{9u};   // v9 (FOUND-057): ContentHash folds num_scalar_args + iterates all scalars (no 5-clamp); v8 hashes invalid

[[nodiscard]] constexpr bool cdag_version_matches(
    ExternalCdagVersion disk_version) noexcept
{
    return disk_version.value() == CDAG_VERSION.value();
}

// Hard caps on header-declared counts.  Real traces top out around
// 10^5 ops / 10 inputs per op; the extra order of magnitude is slack.
// Reject adversarial headers up front so we don't attempt TB-scale
// allocations before discovering the body is truncated.
static constexpr uint32_t CDAG_MAX_OPS          = 1u << 22;   // 4 M ops
// SLOTS tracks the allocator's runtime cap, NOT the family's 4 M slack: a
// loaded plan materializes the replay pool via PoolAllocator::init(), whose
// pre(in_range(num_slots, 0, kMaxNumSlots)) is `[[assume]]` under release
// semantic=ignore.  Making the deserialize cap EQUAL init's cap (by deriving
// it here) means "deserializes ⇒ safe to init" holds by construction — a
// looser wire cap (formerly 4 M) let num_slots in (kMaxNumSlots, 4 M] pass
// deserialize yet `[[assume]]`-violate init on a large enough corrupt Cipher.
static constexpr uint32_t CDAG_MAX_SLOTS        = ::crucible::PoolAllocator::kMaxNumSlots;
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

    // Read a T and HARD-gate it against a deserialize predicate, failing
    // closed (sets `ok = false`) on a malformed value.
    //
    // This is the release-safe companion to the typed Refined<> gates that
    // follow it.  A fixy::wrap::Refined<Pred, T> constructed directly from a
    // wire byte enforces `pre(Pred(v))` only under contract semantic
    // enforce/observe (Debug / default presets).  Under the release preset
    // (-DNDEBUG, semantic=ignore) that pre clause collapses to
    // `[[assume(Pred(v))]]` — NO runtime branch — so an untrusted byte that
    // violates the predicate becomes a false promise fed to the optimizer
    // (downstream `default: std::unreachable()` UB, corrupted content-hash
    // identity, etc.).  Trust-boundary reads must therefore reject malformed
    // input through a real runtime branch BEFORE constructing the Refined.
    //
    // `read_gated` is that branch: it reads the byte through the existing
    // truncation-checked path, evaluates `pred` unconditionally (the
    // predicate is a plain constexpr callable, never a contract clause), and
    // sets `ok = false` on failure — the same soft-fail channel a truncated
    // read uses, which every deserialize entry point already turns into a
    // nullptr/LoadedRegionNode{nullptr} return.
    //
    // On FAILURE it returns `valid_substitute` (a caller-supplied value the
    // predicate is known to accept — `T{0}` for every deserialize predicate,
    // since 0 is the first enumerator / a valid count) RATHER than the
    // malformed byte.  This is deliberate: the immediate downstream caller
    // feeds the result into a Refined<Pred, T> ctor, whose `pre(Pred(v))`
    // would itself ABORT on a malformed value under semantic=enforce
    // (Debug).  Substituting a predicate-valid sentinel makes the rejection
    // UNIFORM — a clean nullptr return via `ok=false` in BOTH Debug and
    // release, rather than abort-in-Debug / silent-pass-in-release.  The
    // substituted value is never observed: `ok=false` forces the caller to
    // discard the whole parse.
    //
    // On a well-formed value the returned byte is identical to `r<T>()`, so
    // the success-path bytes and behaviour (DetSafe bit-stable replay) are
    // unchanged — the gate only ADDS rejection of malformed input.
    template <typename T, typename Pred>
    [[nodiscard]] T read_gated(Pred pred, T valid_substitute = T{0}) {
        const T v = r<T>();
        // Only judge bytes we actually read; a prior truncation already
        // set ok=false and v is the zero default — don't double-report.
        if (ok && !pred(v)) [[unlikely]] { ok = false; return valid_substitute; }
        return v;
    }

    // Bytes remaining from the cursor.
    [[nodiscard]] size_t remaining() const noexcept {
        return (pos <= len) ? (len - pos) : 0;
    }

    // Pre-flight check for array-of-T deserialization: returns true iff
    // the reader has at least n * sizeof(T) bytes remaining AND the
    // product is computable without overflow.
    //
    // Use before allocating an arena buffer sized for `n` elements of
    // type T: adversarial headers claiming counts near CDAG_MAX_* with
    // a truncated body would otherwise grow the arena by
    // n * sizeof(T) bytes before the per-element read discovers EOF —
    // up to ~2 GB on num_ops=4M TraceEntry arrays.  With this check
    // the arena is left untouched; the caller can bail cleanly.
    template <typename T>
    [[nodiscard]] bool has_remaining(size_t n) const noexcept {
        if (n == 0) return true;
        // Guard the multiply.  SIZE_MAX / sizeof(T) is the largest n
        // for which n * sizeof(T) doesn't wrap on size_t.  Any header-
        // declared count exceeding CDAG_MAX_* is already rejected, but
        // the multiply check is a belt-and-braces defence for types T
        // whose sizeof may grow (e.g. TraceEntry extensions).
        if (n > SIZE_MAX / sizeof(T)) return false;
        const size_t need = n * sizeof(T);
        return pos <= len && (len - pos) >= need;
    }
};

// Write TensorMeta with process-local fields zeroed. data_ptr is a
// runtime address; grad_fn_hash is a Family-B autograd identity. Neither
// is meaningful after reload and neither may enter persistent bytes.
// Pad bytes are written as zero for deterministic serialization.
//
// WRAP-Serialize-5 #1014: the two "MUST be zero" write sites pin their
// invariant at the type level via fixy::wrap::Refined<fixy::wrap::is_zero, ...>.
// A future refactor that accidentally feeds m.data_ptr (or any non-zero
// expression) into either constructor contract-fires immediately at the
// construction site — the violation surfaces as a clean Refined-ctor
// failure rather than as silent wire-format corruption that breaks
// DetSafe bit-stable replay.  Regime-1 EBO collapses the wrapper to
// sizeof(uint64_t), so write_meta still emits exactly the same 8 bytes.
inline void write_meta(Writer& w, const TensorMeta& m) {
    w.write_bytes(m.sizes.raw_data(),   sizeof(m.sizes));
    w.write_bytes(m.strides.raw_data(), sizeof(m.strides));
    // data_ptr → always 0 on disk (runtime address, meaningless persisted).
    const fixy::wrap::Refined<fixy::wrap::is_zero, std::uint64_t> zero_ptr{
        std::uint64_t{0}};
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
    // grad_fn_hash → always 0 on disk (Family-B process-local identity).
    const fixy::wrap::Refined<fixy::wrap::is_zero, std::uint64_t> zero_grad_fn_hash{
        std::uint64_t{0}};
    w.w(zero_grad_fn_hash.value());
}

// Read TensorMeta: process-local fields are always null/zero after
// deserialization, even if older or corrupt bytes carry non-zero values.
inline TensorMeta read_meta(Reader& r) {
    TensorMeta m{};
    for (uint8_t d = 0; d < kMaxTensorNDim; ++d) {
        m.sizes[d] = tensor_dim(r.r<int64_t>());
    }
    for (uint8_t d = 0; d < kMaxTensorNDim; ++d) {
        m.strides[d] = tensor_dim(r.r<int64_t>());
    }
    (void)r.r<uint64_t>(); // data_ptr (discarded)
    m.data_ptr   = external_data_ptr(nullptr);
    // ── PROD-WRAP-5 (#534) — typed widening at deserialize boundary ──
    // The byte on disk could be in [9, 255] under corruption or
    // version skew (a uint8_t carries no inherent bound, but ndim is
    // structurally bounded by sizes[8]/strides[8] = kMaxTensorNDim =
    // 8).  `read_gated` is the HARD (non-contract) runtime branch that
    // fails closed (sets r.ok=false → deserialize_region returns
    // LoadedRegionNode{nullptr}) in BOTH Debug and release: ValidNDim's
    // ctor pre-clause alone collapses to `[[assume]]` under the release
    // preset (-DNDEBUG, semantic=ignore), so it cannot reject a corrupt
    // byte there.  The Refined ctor still runs as a typed defense-in-depth
    // re-check — its pre clause holds by construction because read_gated
    // already established the bound.  TraceLoader's ndim > 8 guard is a
    // further independent layer.
    m.ndim       = make_ndim(ValidNDim{r.read_gated<uint8_t>(
                       ::crucible::fixy::wrap::bounded_above<kMaxTensorNDim>)});
    // dtype gate (sibling of #534/#892): a corrupt or skewed byte outside
    // ScalarType's sparse enumerator set (e.g. 14) would otherwise reach
    // element_size()'s `default: std::unreachable()` as UB.  read_gated
    // rejects it at deserialize entry in release too (the ValidScalarType
    // ctor's pre clause is `[[assume]]`-only under -DNDEBUG).
    m.dtype      = make_scalar_type(ValidScalarType{
                       r.read_gated<int8_t>(valid_scalar_type)});
    // device_type gate (sibling of the dtype gate): boundary-validate the
    // untrusted byte — device_type feeds the content hash (node identity),
    // so a corrupt/skewed value silently corrupts it.  Fail-closed rather
    // than admit a node with a corrupted hash.  (Not a UB fix — DeviceType
    // has no std::unreachable consumer, unlike ScalarType.)  Hard-gated via
    // read_gated so release rejects it (ctor pre is `[[assume]]`-only).
    m.device_type = make_device_type(ValidDeviceType{
                        r.read_gated<int8_t>(valid_device_type)});
    m.device_idx  = r.r<int8_t>();
    // layout gate (last read_meta enum): boundary-validate the untrusted
    // byte — layout feeds the content hash (node identity), so a corrupt
    // value silently corrupts it.  Fail-closed.  (Not a UB fix — Layout
    // has no std::unreachable consumer.)  Hard-gated via read_gated so
    // release rejects it (ctor pre is `[[assume]]`-only).
    m.layout          = make_layout(ValidLayout{
                            r.read_gated<int8_t>(valid_layout)});
    m.requires_grad   = r.r<bool>();
    m.flags           = r.r<uint8_t>();
    m.output_nr       = r.r<uint8_t>();
    m.storage_offset  = r.r<int64_t>();
    m.version         = r.r<uint32_t>();
    m.storage_nbytes  = r.r<uint32_t>();
    (void)r.r<uint64_t>(); // grad_fn_hash (Family-B, discarded)
    m.grad_fn_hash    = grad_fn_hash(0);
    return m;
}

// Write the common CDAG header (32B).
inline void write_header(Writer& w, TraceNodeKind kind,
                         MerkleHash merkle_hash, ContentHash content_hash) {
    w.w(CDAG_MAGIC);
    w.w(CDAG_VERSION.value());
    w.w(std::to_underlying(kind));
    const uint8_t pad7[7] = {};
    w.write_bytes(pad7, 7);
    w.w(merkle_hash.raw());
    w.w(content_hash.raw());
}

struct Header {
    uint32_t      magic = 0;
    ExternalCdagVersion version{0};
    TraceNodeKind kind{};            // strong-typed (was raw uint8_t)
    MerkleHash    merkle_hash;
    ContentHash   content_hash;
};

inline Header read_header(Reader& r) {
    Header h{};
    h.magic   = r.r<uint32_t>();
    h.version = ExternalCdagVersion{r.r<uint32_t>()};
    // ── WRAP-Serialize-6 (#1015) — typed widening at deserialize boundary ──
    // The byte on disk could be in [4, 255] under corruption or version
    // skew.  read_gated is the HARD (non-contract) branch that fails
    // closed (sets r.ok=false → the read_header callers' `if (!r.ok)
    // return nullptr` fires) in BOTH Debug and release: ValidTraceNodeKindRaw's
    // ctor pre-clause alone collapses to `[[assume]]` under the release
    // preset (-DNDEBUG, semantic=ignore) and cannot reject a corrupt byte
    // there.  The Refined ctor still runs as a typed defense-in-depth
    // re-check (its pre clause holds by construction).
    h.kind    = make_trace_node_kind(
                    ValidTraceNodeKindRaw{r.read_gated<uint8_t>(
                        ::crucible::fixy::wrap::bounded_above<
                            static_cast<uint8_t>(TraceNodeKind::TERMINAL)>)});
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
    // #942 WRAP-MerkleDag-6: variant_id is fixy::wrap::Monotonic<uint32_t>
    // (regime-2 collapse to sizeof(uint32_t)=4B; on-disk format
    // unchanged).  .get() projects the underlying uint32_t for the
    // raw byte writer.
    w.w(region->variant_id.get());

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
// Returns a Loaded-tagged null pointer on parse error or version mismatch.
// All structures are arena-allocated; data_ptr is always null.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline LoadedRegionNode deserialize_region(
    effects::Alloc                a,
    std::span<const uint8_t> buf,
    Arena&                   arena)
{
    using namespace detail_ser;
    Reader r{.buf = buf.data(), .pos = 0, .len = buf.size()};

    const Header hdr = read_header(r);
    if (!r.ok
        || hdr.magic   != CDAG_MAGIC
        || !cdag_version_matches(hdr.version)
        || hdr.kind    != TraceNodeKind::REGION) {
        return LoadedRegionNode{nullptr};
    }

    const uint32_t   num_ops         = r.r<uint32_t>();
    if (num_ops > CDAG_MAX_OPS) return LoadedRegionNode{nullptr};
    const SchemaHash first_op_schema = SchemaHash{r.r<uint64_t>()};
    const float      measured_ms     = r.r<float>();
    const uint32_t   variant_id      = r.r<uint32_t>();

    // MemoryPlan
    MemoryPlan* plan    = nullptr;
    const bool has_plan = r.r<bool>();
    if (has_plan) {
        plan                   = arena.alloc_obj<MemoryPlan>(a);
        plan->pool_bytes        = r.r<uint64_t>();
        // pool_bytes feeds PoolAllocator::init()'s
        // pre(in_range(pool_bytes, 0, kMaxPoolBytes)).  Under release
        // semantic=ignore that clause is `[[assume]]`, so a corrupt or
        // version-skewed Cipher byte with pool_bytes > kMaxPoolBytes
        // (256 GB) reaches init as `[[assume]]`-UB — and the body then
        // feeds the value to aligned_alloc.  Reject at the load boundary
        // against the SAME constant init checks (single source of truth),
        // companion to the num_slots / num_external bounds below.  The
        // bound is inclusive (in_range is closed [0, kMaxPoolBytes]), so
        // only a strictly-greater value is rejected.
        if (plan->pool_bytes > ::crucible::PoolAllocator::kMaxPoolBytes) [[unlikely]] {
            return LoadedRegionNode{nullptr};
        }
        plan->num_slots         = r.r<uint32_t>();
        if (plan->num_slots > CDAG_MAX_SLOTS) return LoadedRegionNode{nullptr};
        plan->num_external      = r.r<uint32_t>();
        // External slots are a subset of total slots (num_external counts
        // how many of num_slots are external), so num_external <= num_slots
        // is a structural invariant — and it is PoolAllocator::init()'s
        // `pre(plan->num_external <= plan->num_slots)`.  A corrupt or
        // version-skewed Cipher byte could deliver num_external > num_slots;
        // unchecked, that reaches the init precondition where, under
        // semantic=ignore, the violated clause is `[[assume]]` UB.  Reject
        // at the boundary, companion to the num_slots bound above.
        if (plan->num_external > plan->num_slots) return LoadedRegionNode{nullptr};
        // device_type gate (reuses ValidDeviceType): same boundary-
        // hardening as the read_meta gate — a corrupt/version-skewed
        // byte would otherwise enter pool selection (== DeviceType::CPU)
        // unchecked.  Completes DeviceType deserialize coverage across
        // both boundaries (TensorMeta + MemoryPlan).  Hard-gated via
        // read_gated so release rejects it too (the ValidDeviceType ctor
        // pre is `[[assume]]`-only under -DNDEBUG); r.ok=false on a
        // malformed byte propagates to the `if (!r.ok)` return below.
        plan->device_type       = make_device_type(ValidDeviceType{
                                      r.read_gated<int8_t>(valid_device_type)});
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
            if (r.pos + slot_bytes > r.len) return LoadedRegionNode{nullptr};
            plan->slots = arena.alloc_array<TensorSlot>(a, plan->num_slots);
            for (uint32_t s = 0; s < plan->num_slots; s++) {
                r.read_bytes(&plan->slots[s], sizeof(TensorSlot));
            }
        } else {
            plan->slots = nullptr;
        }
    }

    // TraceEntries.  Each entry is a variable-size record (input/output
    // metas + scalar args), but the minimum per-entry wire cost is the
    // fixed header (hashes + counts + flags ≈ 40 bytes), which we can
    // pre-flight cheaply.  Adversarial num_ops=CDAG_MAX_OPS (4M) with a
    // truncated body would otherwise grow the arena by 4M *
    // sizeof(TraceEntry) (~2 GB) before the first read_meta failure.
    constexpr size_t kTraceEntryMinWireBytes = 40;
    if (num_ops > 0 &&
        r.remaining() < static_cast<size_t>(num_ops) * kTraceEntryMinWireBytes)
    {
        return LoadedRegionNode{nullptr};
    }
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
        if (te.num_inputs       > CDAG_MAX_INPUTS)      return LoadedRegionNode{nullptr};
        if (te.num_outputs      > CDAG_MAX_OUTPUTS)     return LoadedRegionNode{nullptr};
        if (te.num_scalar_args  > CDAG_MAX_SCALAR_ARGS) return LoadedRegionNode{nullptr};
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
        // Validated uint8_t → CKernelId widening (#892 WRAP-CKernel-4).
        // A corrupted Cipher file or version-skew can deliver a byte
        // in [NUM_KERNELS, 255]; the explicit bound check returns
        // nullptr (matches the deserialize-error policy used for
        // num_inputs / num_outputs / num_scalar_args above), and the
        // checked Refined ctor stands as a defense-in-depth re-check
        // — the bound is established by the if-return, so the ctor's
        // pre clause holds and never aborts on this path.  The neg-
        // compile fixtures pin the structural guarantee at the type
        // level: a constexpr ValidCKernelIdRaw{>= NUM_KERNELS} is
        // ill-formed regardless of what callers do.
        {
            const uint8_t raw_kernel_id = r.r<uint8_t>();
            if (raw_kernel_id >= static_cast<uint8_t>(
                    CKernelId::NUM_KERNELS)) [[unlikely]] {
                return LoadedRegionNode{nullptr};
            }
            te.kernel_id = make_ckernel_id(
                ValidCKernelIdRaw{raw_kernel_id});
        }

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
            const SlotId sid = SlotId{r.r<uint32_t>()};
            // A loaded slot_id indexes ReplayEngine::slot_table_, which
            // holds exactly the replay pool's num_slots void* entries.
            // The replay hot path (input_ptr / output_ptr) guards ONLY
            // sid.is_valid() (the none-sentinel), NOT sid.raw() <
            // num_slots — by design that bound is a construction-time
            // invariant the hot path trusts as [[assume]] (single-MOV
            // slot_table_[sid.raw()]).  For a plan-BEARING region the
            // replay pool is materialized from THIS plan, so every valid
            // slot_id must be a real index (< plan->num_slots); the
            // planner (and external-slot numbering, which shares the
            // [0,num_slots) index space) guarantees this in trusted flow.
            // A corrupt/version-skewed Cipher could deliver sid.raw() >=
            // num_slots with is_valid() true, reaching slot_table_[sid
            // .raw()] = OUT-OF-BOUNDS heap read.  Reject at the load
            // boundary, companion to the num_slots / num_external bounds
            // above; same fail-closed deserialize-error policy.  A plan-
            // LESS region carries opaque slot_ids that bind no pool until
            // one is supplied elsewhere, so it has no bound to enforce
            // here (and is not executable without that pool).
            if (plan != nullptr && sid.is_valid()
                && sid.raw() >= plan->num_slots) [[unlikely]] {
                return LoadedRegionNode{nullptr};
            }
            te.input_slot_ids[j] = sid;
        }

        te.output_slot_ids = (te.num_outputs > 0)
            ? arena.alloc_array<SlotId>(a, te.num_outputs) : nullptr;
        for (uint16_t j = 0; j < te.num_outputs; j++) {
            const SlotId sid = SlotId{r.r<uint32_t>()};
            // Same plan-bearing OOB-index guard as input_slot_ids above:
            // an untrusted out-of-range slot_id would reach ReplayEngine
            // output_ptr's slot_table_[sid.raw()] OOB read.  Plan-less
            // regions are exempt (no pool to bound against).
            if (plan != nullptr && sid.is_valid()
                && sid.raw() >= plan->num_slots) [[unlikely]] {
                return LoadedRegionNode{nullptr};
            }
            te.output_slot_ids[j] = sid;
        }
    }

    if (!r.ok) return LoadedRegionNode{nullptr};

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
    // #942 WRAP-MerkleDag-6: variant_id is fixy::wrap::Monotonic<uint32_t>.
    // The field was already default-constructed to {0u} by the
    // RegionNode{} placement-new above; re-establish the invariant
    // from the disk-supplied value via std::construct_at so the
    // wrapper's Monotonic ctor runs on the new value.  Bypassing
    // .advance() is deliberate here — the disk value may be 0 (region
    // never had a variant selected when serialized), which advance's
    // outer set_variant gate would reject (CONTRACT-106 non-zero).
    // Same construct_at re-anchor pattern as CKernelTable::clear() and
    // IterationDetector::reset() — load from a known floor on the
    // wrapper's terms.
    std::construct_at(&node->variant_id,
                      RegionNode::VariantCounter{variant_id});
    node->plan            = plan;
    // node->compiled is a PublishOnce<CompiledKernel> — default-
    // constructed nullptr is the correct "not yet published" state.
    // No explicit store needed.
    return LoadedRegionNode{node};
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

template <typename Resolve>
    requires std::is_invocable_r_v<TraceNode*, Resolve&, MerkleHash>
[[nodiscard]] inline BranchNode* deserialize_branch(
    effects::Alloc                                 a,
    std::span<const uint8_t>                  buf,
    Arena&                                    arena CRUCIBLE_LIFETIMEBOUND,
    Resolve&&                                 resolve)
{
    Resolve resolver = std::forward<Resolve>(resolve);
    using namespace detail_ser;
    Reader r{.buf = buf.data(), .pos = 0, .len = buf.size()};

    const Header hdr = read_header(r);
    if (!r.ok
        || hdr.magic   != CDAG_MAGIC
        || !cdag_version_matches(hdr.version)
        || hdr.kind    != TraceNodeKind::BRANCH) {
        return nullptr;
    }
    // hdr.content_hash holds the continuation's merkle_hash (see serialize_branch)

    Guard guard{};
    r.read_bytes(&guard, sizeof(Guard));

    const uint32_t num_arms = r.r<uint32_t>();
    if (num_arms > CDAG_MAX_BRANCH_ARMS) return nullptr;

    // Pre-flight: each arm is 16 bytes on the wire (int64 value +
    // MerkleHash).  Reject truncated bodies before allocating the arms
    // array — an adversarial num_arms near CDAG_MAX_BRANCH_ARMS (64 K)
    // with a short body would otherwise grow the arena by 64 K * 16 B
    // (1 MB) uselessly.  The check runs once, O(1).  Use explicit wire
    // byte count instead of sizeof(Arm) — wire and in-memory layouts
    // happen to coincide at 16 bytes today, but in-memory Arm ends up
    // storing a TraceNode* (not a MerkleHash) post-resolve, so tying
    // the check to the wire cost decouples from struct changes.
    constexpr size_t kArmWireBytes = sizeof(int64_t) + sizeof(uint64_t);
    if (num_arms > 0 &&
        r.remaining() < static_cast<size_t>(num_arms) * kArmWireBytes)
    {
        return nullptr;
    }

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
            node->arms[i].target         = resolver(target_h);
        }
    } else {
        node->arms = nullptr;
    }

    return r.ok ? node : nullptr;
}

[[nodiscard]] inline BranchNode* deserialize_branch(
    effects::Alloc                            a,
    std::span<const uint8_t>                  buf,
    Arena&                                    arena CRUCIBLE_LIFETIMEBOUND,
    std::nullptr_t) {
    return deserialize_branch(
        a, buf, arena, [](MerkleHash) noexcept -> TraceNode* {
            return nullptr;
        });
}

} // namespace crucible
