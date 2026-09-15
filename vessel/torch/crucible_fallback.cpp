// Crucible native dispatcher fallback for DispatchKey::Crucible.
//
// Registers a boxed fallback that intercepts every ATen op when the key
// is enabled in TLS.  Reads CrucibleState to get the Vigil handle, extracts
// tensor metadata from the IValue stack, and feeds it to Vigil::dispatch_op().
//
// This is the C++ counterpart of crucible_mode.py (TorchDispatchMode).
// Where Python adds ~30-50us/op overhead, this path adds ~100ns/op.
//
// Build requirements:
//   - PyTorch fork with DispatchKey::Crucible + CrucibleState TLS
//   - Crucible headers (Vigil.h, TraceRing.h, MerkleDag.h, Types.h)
//
// Loaded via torch.ops.load_library() -- the TORCH_LIBRARY_IMPL registration
// fires on dlopen, no explicit init needed.

#include <c10/core/CrucibleState.h>

#include <ATen/core/dispatch/Dispatcher.h>
#include <ATen/core/ivalue.h>
#include <ATen/core/stack.h>
#include <torch/library.h>
#include <torch/csrc/autograd/function.h>
#include <torch/csrc/autograd/variable.h>

#include <crucible/MerkleDag.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <crucible/Vigil.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

#include "vessel_api_typed.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace {

// =====================================================================
// FNV-1a 64-bit
//
// Must produce identical hashes to vessel_api.cpp's crucible_hash_string
// and crucible_hash_shapes.  Same offset basis, same prime, same byte
// order -- hash("aten::mm.default") here == hash("aten::mm.default") in
// the Python ctypes path.
// =====================================================================

static constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
static constexpr uint64_t FNV_PRIME = 0x100000001b3ULL;

[[nodiscard]] static uint64_t fnv1a_bytes(const void* data, size_t len, uint64_t h = FNV_OFFSET) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

[[nodiscard]] static uint64_t fnv1a_str(const char* s, size_t len) { return fnv1a_bytes(s, len); }

// =====================================================================
// Extraction result: strong-typed intermediate from IValue stack
//
// Separates "what we extracted from PyTorch" from "how we feed Vigil."
// Every field has a semantic type -- no raw uint16_t counts.
// =====================================================================

// Number of tensor metas extracted from one op's args/returns.
struct MetaCount {
    uint16_t inputs = 0;
    uint16_t outputs = 0;

    [[nodiscard]] uint32_t total() const { return static_cast<uint32_t>(inputs) + outputs; }
};

// Up to 5 scalar arguments, bitcast to int64_t.
struct ScalarArgs {
    int64_t values[5]{};
    uint16_t count = 0;

    void push(int64_t v) {
        if (count < 5) values[count++] = v;
    }
};

// =====================================================================
// Schema hash cache
//
// Thread-local direct-mapped cache: OperatorHandle address -> SchemaHash.
// After warmup (first iteration), every op is a cache hit: one pointer
// comparison + one array index.  Zero contention (thread-local).
//
// 2048 slots x 16B = 32KB -- fits in L1d.  With >>4 shift for pointer
// alignment (OperatorHandle >=16B), collision rate is near zero for
// typical models (<500 unique ops).
// =====================================================================

// Per-op cached info: schema hash + is_mutable flag.
// SchemaHash (16B slot) + is_mutable parallel array (1B) = 34KB total.
// Fits comfortably in L1d (48KB Zen 4). The parallel array avoids
// bloating slots to 24B which would spill L1d.
//
// `key` carries the c10::OperatorHandle pointer that PyTorch handed
// to us across the boxed-fallback ABI.  GAPS-096 wraps it in
// `Tagged<const void*, source::External>` to thread the FFI
// provenance into the type system: the pointer is owned by ATen's
// schema registry, never freed for the program's lifetime, and treated
// as identity-only here (compared, never dereferenced).  The wrapper
// is regime-1 EBO collapse, so the slot stays at 16B and the cache
// stays L1-resident.  The alias keeps that spelling in one place, so the
// declaration and the assignment below cannot drift apart, and states the
// `crucible::fixy::` form used by vessel_api_typed.h and TensorMeta.h --
// `crucible::safety::` re-exports the same types, and picking one of the
// two is the point.
using ExternalOpKey = crucible::fixy::wrap::Tagged<const void*, crucible::fixy::tags::source::External>;

struct SchemaHashSlot {
    ExternalOpKey key{nullptr};
    crucible::SchemaHash hash;
};

static_assert(sizeof(SchemaHashSlot) == 16, "SchemaHashSlot must remain 16B for the L1d cache budget — "
                                            "Tagged<const void*, source::External> is regime-1 EBO collapse "
                                            "so its sizeof equals sizeof(const void*).");

static constexpr uint32_t SCHEMA_CACHE_CAP = 2048;
static constexpr uint32_t SCHEMA_CACHE_MASK = SCHEMA_CACHE_CAP - 1;
static thread_local SchemaHashSlot schema_cache[SCHEMA_CACHE_CAP]{};
static thread_local bool schema_is_mutable[SCHEMA_CACHE_CAP]{};

struct SchemaInfo {
    crucible::SchemaHash hash;
    bool is_mutable = false;
};

[[nodiscard]] static SchemaInfo get_schema_info(const c10::OperatorHandle& op, const c10::FunctionSchema& schema) {
    const auto idx = (reinterpret_cast<uintptr_t>(&op) >> 4) & SCHEMA_CACHE_MASK;
    auto& slot = schema_cache[idx];
    if (slot.key.value() == &op) [[likely]]
        return {slot.hash, schema_is_mutable[idx]};

    // Cache miss: compute FNV-1a over "namespace::name.overload".
    const auto& name = op.operator_name();
    uint64_t h = FNV_OFFSET;
    for (char c : name.name) {
        h ^= static_cast<uint8_t>(c);
        h *= FNV_PRIME;
    }
    if (!name.overload_name.empty()) {
        h ^= static_cast<uint8_t>('.');
        h *= FNV_PRIME;
        for (char c : name.overload_name) {
            h ^= static_cast<uint8_t>(c);
            h *= FNV_PRIME;
        }
    }

    const auto schema_hash = crucible::SchemaHash{h};

    // Register name for visualization / diagnostics.
    // "aten::mm.default" -> SchemaTable for op name lookup.
    std::string full_name = name.name;
    if (!name.overload_name.empty()) {
        full_name += '.';
        full_name += name.overload_name;
    }
    // PyTorch's Operator schema is trusted by source — compiled into
    // the libtorch binary.  Construct Sanitized directly.
    auto& schema_table = crucible::global_schema_table();
    if (!schema_table.is_sealed()) {
        auto schema_table_view = schema_table.mint_mutable_view();
        crucible::register_schema_name(schema_table_view, schema_hash,
                                       crucible::SchemaTable::SanitizedName{full_name.c_str()});
    }

    // Authoritative mutability from schema alias annotations.
    // Catches both in-place ops (add_.Tensor: self(a!)) and out= variants
    // (add.out: out(a!)). FunctionSchema::is_mutable() checks if any
    // argument has AliasInfo with isWrite() == true.
    const bool mutable_op = schema.is_mutable();

    // Re-tag at the FFI source: the OperatorHandle pointer just
    // crossed the boxed-fallback boundary, so it carries source::External
    // until something downstream proves otherwise.  Construction is
    // explicit per Tagged's API; the wrapper is move-assigned
    // into the slot at zero runtime cost.
    slot.key = ExternalOpKey{&op};
    slot.hash = schema_hash;
    schema_is_mutable[idx] = mutable_op;
    return {schema_hash, mutable_op};
}

// =====================================================================
// Training phase TLS
//
// Set by Python controller to distinguish forward/backward/optimizer.
// Lives in the dispatch lib (no PyTorch patch needed). The fallback
// reads this and packs it into op_flags bits 2-3.
// =====================================================================

static thread_local uint8_t s_training_phase = 0;

// =====================================================================
// Shape hash
//
// FNV-1a over (ndim, sizes[0..ndim-1]) per input tensor.
// Identical to vessel_api.cpp crucible_hash_shapes().
// =====================================================================

// Must produce identical hashes to vessel_api.cpp::crucible_hash_shapes().
// Chain: for each tensor, fold ndim (1 byte) then sizes (ndim * 8 bytes)
// into a single continuous FNV-1a accumulator.
[[nodiscard]] static crucible::ShapeHash compute_shape_hash(const crucible::TensorMeta* metas, uint16_t n_inputs) {
    uint64_t h = FNV_OFFSET;
    for (uint16_t i = 0; i < n_inputs; i++) {
        // Hash ndim as separator (1 byte), continuing the chain.
        h = fnv1a_bytes(&metas[i].ndim, 1, h);
        // Hash sizes[0..ndim-1], continuing the chain.  `sizes` is a
        // TensorDimArray, whose lanes stay plain int64_t precisely so a bulk
        // reader can take the block without a copy; `raw_data()` is that
        // reader.  Only the write path goes through the refined TensorDim.
        const uint32_t nbytes = static_cast<uint32_t>(metas[i].ndim) * static_cast<uint32_t>(sizeof(int64_t));
        h = fnv1a_bytes(metas[i].sizes.raw_data(), nbytes, h);
    }
    return crucible::ShapeHash{h};
}

// =====================================================================
// Tensor metadata extraction
//
// Fill all 168B of crucible::TensorMeta from an at::Tensor.
// Handles strided and non-strided layouts.
//
// Fields filled:
//   Core (144B): sizes[8], strides[8], data_ptr, ndim, dtype,
//                device_type, device_idx, layout, requires_grad,
//                flags, output_nr
//   Extended (24B): storage_offset, version, storage_nbytes, grad_fn_hash
// =====================================================================

static void fill_meta(crucible::TensorMeta& meta, const at::Tensor& t) {
    meta = {};  // zero-init (InitSafe -- NSDMI defaults)
    if (!t.defined()) return;

    // -- Core fields --------------------------------------------------

    const auto ndim = static_cast<uint8_t>(std::min(t.dim(), static_cast<int64_t>(8)));
    meta.ndim = ndim;

    const auto sizes = t.sizes();
    for (uint8_t d = 0; d < ndim; d++)
        meta.sizes[d] = ::crucible::tensor_dim(sizes[d]);

    const bool strided = (t.layout() == c10::Layout::Strided);
    if (strided) {
        const auto strides = t.strides();
        for (uint8_t d = 0; d < ndim; d++)
            meta.strides[d] = ::crucible::tensor_dim(strides[d]);
        // The storage address crosses the ATen boundary here, so it enters
        // as source::External: hashed and compared as an opaque cookie,
        // never dereferenced, until some later validator retags it.
        meta.data_ptr = ::crucible::external_data_ptr(t.data_ptr());
    }

    // #161: c10 and crucible enums mirror ordinals by design
    // (Types.h documents the invariant).  `std::bit_cast` makes the
    // ordinal-reinterpretation explicit — the previous
    // `static_cast<crucible::X>(static_cast<int8_t>(c10_value))` was
    // a double narrowing conversion that silently truncated any future
    // c10 enum value escaping int8_t range and looked like an
    // ordinary value conversion.  bit_cast is pure bit reinterpretation;
    // sizeof-mismatch is a compile error, not a runtime surprise.
    //
    // Static-assert the size and every mirrored ordinal so a c10 renumber —
    // past int8_t overflow or into a previously-hole ordinal — fails the
    // build instead of silently corrupting tensor metadata.
    static_assert(sizeof(c10::ScalarType) == sizeof(crucible::ScalarType));
    static_assert(sizeof(c10::DeviceType) == sizeof(crucible::DeviceType));
    static_assert(sizeof(c10::Layout) == sizeof(crucible::Layout));

    // Every scalar type Crucible names, checked one by one.  A single
    // sampled ordinal did not witness the invariant the bit_cast rests on:
    // it only witnessed one lane of it.  The per-type message names the
    // type that drifted, which a folded check could not.
#define CRUCIBLE_MIRROR_SCALAR(name)                                                                             \
    static_assert(static_cast<int8_t>(c10::ScalarType::name) == static_cast<int8_t>(crucible::ScalarType::name), \
                  "c10::ScalarType::" #name " ordinal drifted from the crucible mirror")
    CRUCIBLE_MIRROR_SCALAR(Byte);
    CRUCIBLE_MIRROR_SCALAR(Char);
    CRUCIBLE_MIRROR_SCALAR(Short);
    CRUCIBLE_MIRROR_SCALAR(Int);
    CRUCIBLE_MIRROR_SCALAR(Long);
    CRUCIBLE_MIRROR_SCALAR(Half);
    CRUCIBLE_MIRROR_SCALAR(Float);
    CRUCIBLE_MIRROR_SCALAR(Double);
    CRUCIBLE_MIRROR_SCALAR(ComplexHalf);
    CRUCIBLE_MIRROR_SCALAR(ComplexFloat);
    CRUCIBLE_MIRROR_SCALAR(ComplexDouble);
    CRUCIBLE_MIRROR_SCALAR(Bool);
    CRUCIBLE_MIRROR_SCALAR(BFloat16);
    CRUCIBLE_MIRROR_SCALAR(Float8_e5m2);
    CRUCIBLE_MIRROR_SCALAR(Float8_e4m3fn);
    CRUCIBLE_MIRROR_SCALAR(Float8_e5m2fnuz);
    CRUCIBLE_MIRROR_SCALAR(Float8_e4m3fnuz);
#undef CRUCIBLE_MIRROR_SCALAR

    // Undefined is the one deliberate divergence, so it must not be
    // bit_cast.  c10 appends Undefined after the last scalar type, which
    // puts it at a large positive ordinal; Crucible uses -1 as a sentinel
    // so that "no dtype" sorts outside the value range instead of inside
    // it.  Asserting the two equal was false, and it is the assert that
    // was wrong, not the enums.  Assert the divergence instead, plus the
    // property that makes the explicit map below total: every c10 ordinal
    // is non-negative, so the -1 sentinel can never collide with one.
    static_assert(static_cast<int8_t>(crucible::ScalarType::Undefined) < 0,
                  "the crucible Undefined sentinel must stay negative so it cannot alias a c10 ordinal");
    static_assert(static_cast<int16_t>(c10::ScalarType::Undefined) >= 0,
                  "c10 scalar ordinals must stay non-negative for the sentinel to be disjoint");
    static_assert(static_cast<int16_t>(c10::ScalarType::Undefined)
                      != static_cast<int16_t>(crucible::ScalarType::Undefined),
                  "if c10 ever adopts -1 for Undefined, drop the explicit map below and bit_cast it");

    static_assert(static_cast<int8_t>(c10::DeviceType::CUDA) == static_cast<int8_t>(crucible::DeviceType::CUDA),
                  "c10::DeviceType::CUDA ordinal drifted from crucible mirror");
    static_assert(static_cast<int8_t>(c10::Layout::Strided) == static_cast<int8_t>(crucible::Layout::Strided),
                  "c10::Layout::Strided ordinal drifted from crucible mirror");

    // The mirror is exact for every named type, so bit_cast carries them.
    // Undefined is mapped, because its two ordinals differ by design.
    //
    // A dtype that c10 names and Crucible does not — a quantized type, a
    // narrow integer width, one of the newer FP8 variants — still
    // bit_casts to an ordinal that matches no crucible enumerator.  The
    // enum has a fixed underlying type, so the value is well-defined
    // rather than UB, and it round-trips through the trace unchanged.
    // Widening the mirror is a Types.h change, which this file does not own.
    const auto scalar_type = t.scalar_type();
    meta.dtype = (scalar_type == c10::ScalarType::Undefined) ? crucible::ScalarType::Undefined
                                                             : std::bit_cast<crucible::ScalarType>(scalar_type);
    meta.device_type = std::bit_cast<crucible::DeviceType>(t.device().type());
    // c10::DeviceIndex is already int8_t (c10/core/Device.h).  Direct
    // copy on the present-branch; literal -1 on the absent-branch.
    meta.device_idx = t.device().has_index() ? t.device().index() : int8_t{-1};
    meta.layout = std::bit_cast<crucible::Layout>(t.layout());

    // -- Extended fields (autograd + storage) --------------------------

    meta.requires_grad = t.requires_grad();

    uint8_t flags = 0;
    if (t.is_leaf()) flags |= crucible::meta_flags::IS_LEAF;
    if (strided && t.is_contiguous()) flags |= crucible::meta_flags::IS_CONTIGUOUS;
    if (t.is_neg()) flags |= crucible::meta_flags::IS_NEG;
    if (t.is_conj()) flags |= crucible::meta_flags::IS_CONJ;

    auto* impl = t.unsafeGetTensorImpl();
    auto* am = impl->autograd_meta();
    if (am) {
        auto* node = torch::autograd::impl::grad_fn_unsafe(t);
        if (node) {
            flags |= crucible::meta_flags::HAS_GRAD_FN;
            const auto& gfn_name = node->name();
            // The value derives from an autograd node name, which is local
            // to one process, so it carries hash_family::FamilyB and must
            // never key anything that outlives the run.
            meta.grad_fn_hash = ::crucible::grad_fn_hash(fnv1a_str(gfn_name.data(), gfn_name.size()));
        }
        meta.output_nr = static_cast<uint8_t>(torch::autograd::impl::get_autograd_meta(t)->output_nr_ & 0xFF);
    }

    // View detection: check autograd is_view_ flag first (catches expand,
    // as_strided, narrow etc. that share storage base and zero offset).
    // Fall back to data_ptr != storage_base and storage_offset != 0 checks
    // for non-autograd views.
    if (am && static_cast<torch::autograd::AutogradMeta*>(am)->is_view_) flags |= crucible::meta_flags::IS_VIEW;

    if (strided && impl->has_storage()) {
        auto* storage_base = impl->storage().data_ptr().get();
        if (storage_base != nullptr && t.data_ptr() != static_cast<char*>(storage_base)) {
            flags |= crucible::meta_flags::IS_VIEW;
        }
        meta.storage_nbytes = static_cast<uint32_t>(impl->storage().nbytes() & 0xFFFFFFFF);
    }
    if (strided && t.storage_offset() != 0) {
        flags |= crucible::meta_flags::IS_VIEW;
        meta.storage_offset = t.storage_offset();
    }

    meta.flags = flags;

    // c10's version counter is already uint32_t (TensorImpl.h), so the old
    // mask-and-narrow was a no-op the compiler rejects as a useless cast.
    // Assert the width instead: if a future c10 widens it, the build fails
    // here rather than silently truncating a version into a false match.
    static_assert(std::is_same_v<decltype(impl->version_counter().current_version()), uint32_t>,
                  "c10 version counter width drifted — re-check the TensorMeta.version narrowing");
    meta.version = impl->version_counter().current_version();
}

// =====================================================================
// Scalar extraction
//
// Bitcast non-tensor scalars to int64_t for TraceRing::Entry.
// =====================================================================

[[nodiscard]] static int64_t scalar_to_int64(const c10::IValue& iv) {
    if (iv.isInt()) return iv.toInt();
    if (iv.isBool()) return iv.toBool() ? 1 : 0;
    if (iv.isDouble()) return std::bit_cast<int64_t>(iv.toDouble());
    return 0;
}

// =====================================================================
// Stack extraction: pull TensorMeta + ScalarArgs from IValue stack
//
// Returns MetaCount (how many input metas were written).
// Output metas are appended AFTER redispatch (need actual results).
// =====================================================================

static constexpr uint32_t MAX_INLINE_METAS = 32;

struct ExtractionResult {
    MetaCount counts;
    ScalarArgs scalars;
};

[[nodiscard]] static ExtractionResult extract_inputs(const torch::jit::Stack& stack, size_t args_begin, size_t num_args,
                                                     crucible::TensorMeta* metas) {
    ExtractionResult r{};

    for (size_t i = 0; i < num_args; i++) {
        const auto& iv = stack[args_begin + i];

        if (iv.isTensor()) {
            const auto& t = iv.toTensor();
            if (t.defined() && r.counts.inputs < MAX_INLINE_METAS) {
                fill_meta(metas[r.counts.inputs], t);
                r.counts.inputs++;
            }
        } else if (iv.isTensorList()) {
            // Same proxy hazard as the OptionalTensorList branch below: the
            // c10::List iterator dereferences to a proxy whose conversion
            // returns by value, so `const at::Tensor&` binds to a temporary
            // and -Wdangling-reference fires. Take the element by value.
            for (const auto ref : iv.toTensorList()) {
                const at::Tensor t = ref;
                if (t.defined() && r.counts.inputs < MAX_INLINE_METAS) {
                    fill_meta(metas[r.counts.inputs], t);
                    r.counts.inputs++;
                }
            }
        } else if (iv.isOptionalTensorList()) {
            // c10::List iterator dereferences to a proxy object (like
            // vector<bool>). Binding `const auto&` to it triggers
            // -Wrange-loop-bind-reference. Use value to avoid the warning.
            for (const auto ref : iv.toOptionalTensorList()) {
                const auto opt = static_cast<std::optional<at::Tensor>>(ref);
                if (opt.has_value() && opt->defined() && r.counts.inputs < MAX_INLINE_METAS) {
                    fill_meta(metas[r.counts.inputs], *opt);
                    r.counts.inputs++;
                }
            }
        } else if (iv.isInt() || iv.isDouble() || iv.isBool()) {
            r.scalars.push(scalar_to_int64(iv));
        } else if (iv.isIntList()) {
            // IntList args (e.g., size=[3,4] in reshape, padding=[1,2] in conv).
            // Pack first elements into scalar slots (up to 5 total).
            for (int64_t elem : iv.toIntList()) {
                r.scalars.push(elem);
            }
        } else if (iv.isDoubleList()) {
            for (double elem : iv.toDoubleList()) {
                r.scalars.push(std::bit_cast<int64_t>(elem));
            }
        }
    }

    return r;
}

static void extract_outputs(const torch::jit::Stack& stack, size_t num_returns, crucible::TensorMeta* metas,
                            MetaCount& counts) {
    if (num_returns == 0 || stack.size() < num_returns) return;
    const auto rets_begin = stack.size() - num_returns;

    for (size_t i = 0; i < num_returns; i++) {
        const auto& iv = stack[rets_begin + i];

        if (iv.isTensor()) {
            const auto& t = iv.toTensor();
            if (t.defined() && counts.total() < MAX_INLINE_METAS) {
                fill_meta(metas[counts.total()], t);
                counts.outputs++;
            }
        } else if (iv.isTensorList()) {
            // Proxy-by-value, as on the input side.
            for (const auto ref : iv.toTensorList()) {
                const at::Tensor t = ref;
                if (t.defined() && counts.total() < MAX_INLINE_METAS) {
                    fill_meta(metas[counts.total()], t);
                    counts.outputs++;
                }
            }
        }
    }
}

// =====================================================================
// The fallback
// =====================================================================

static const auto AFTER_CRUCIBLE_KEYSET =
    c10::DispatchKeySet(c10::DispatchKeySet::FULL_AFTER, c10::DispatchKey::Crucible);

void crucibleFallback(const c10::OperatorHandle& op, c10::DispatchKeySet dispatch_keys, torch::jit::Stack* stack) {
    auto& state = c10::CrucibleState::get_tls_state();

    // -- Fast path: INACTIVE -- pure passthrough, zero work -----------
    if (state.mode() == c10::CrucibleMode::INACTIVE) [[likely]] {
        op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);
        return;
    }

    // -- Get Vigil from TLS context -----------------------------------
    //
    // state.context() crosses the same ABI boundary as CrucibleHandle —
    // a `void*` registered by the Vessel-side Python binding into
    // PyTorch's CrucibleState TLS.  Route through `as_vigil_typed` so
    // the TLS-stored Vigil pointer carries `source::ABIBoundary`
    // provenance just like a freshly-handed-in CrucibleHandle does.
    // The plausibility-check inside the typed helper is debug-only and
    // skipped when state.context() is null (caller branches below).
    void* tls_ctx = state.context();
    if (!tls_ctx) [[unlikely]] {
        op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);
        return;
    }
    auto vigil_typed = crucible::vessel::as_vigil_typed(static_cast<CrucibleHandle>(tls_ctx));
    auto* vigil = vigil_typed.value();

    // -- Foreign thread: execute, do not record -----------------------
    //
    // The ring behind Vigil is single-producer and the recorded op order is
    // the trace's identity: it fixes the region content hash, the memory
    // plan and the replay order.  Two threads appending to it produce a
    // torn ring, and two threads appending to any ring produce an op order
    // that depends on how they interleaved, which is a different trace on
    // every run.
    //
    // The autograd engine of this runtime runs a backward pass on a worker
    // thread of its own, one per device.  GraphTask captures the caller's
    // at::ThreadLocalState and the worker restores it before each node, and
    // c10::impl::LocalDispatchKeySet is a member of that state.  So the
    // Crucible key travels to the worker thread and this handler fires
    // there, for every backward op of a model on an accelerator.
    //
    // Whether c10::CrucibleState travels the same way is a property of the
    // fork this file builds against.  If it does not, the worker reads
    // INACTIVE and returns at the mode branch above, and this branch is
    // never reached.  If it does, this branch is what keeps the worker off
    // the ring.  Either way the outcome below is the one that holds.
    //
    // A foreign thread executes its op eagerly and leaves the ring alone.
    // The trace is then short by the ops of that thread, which is a trace
    // that is incomplete and reproducible.  Recording them would make it
    // complete and irreproducible, and the pipeline behind it — content
    // addressing, the memory plan, bit-exact replay — is built on the
    // reproducibility rather than on the completeness.
    //
    // Vigil refuses a second producer on its own and ends the process when
    // it sees one.  That guard stays as the last resort for a caller that
    // does not ask first.  This is the asking.
    if (!vigil->is_producer_thread()) [[unlikely]] {
        op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);
        return;
    }

    const auto& schema = op.schema();

    // -- Extract input tensor metadata + scalars ----------------------
    const auto num_args = schema.arguments().size();
    // Guard: schema.arguments().size() > stack->size() should never happen
    // with a well-formed dispatcher, but protect against underflow (UB).
    if (num_args > stack->size()) [[unlikely]] {
        op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);
        return;
    }
    const auto args_begin = stack->size() - num_args;

    crucible::TensorMeta inline_metas[MAX_INLINE_METAS]{};
    auto [counts, scalars] = extract_inputs(*stack, args_begin, num_args, inline_metas);

    // -- Compute hashes + mutability -----------------------------------
    const auto [schema_hash, is_mutable] = get_schema_info(op, schema);
    const auto shape_hash = compute_shape_hash(inline_metas, counts.inputs);

    // -- Execute eagerly (Tier 1: always redispatch) ------------------
    op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);

    // -- Extract output tensor metadata -------------------------------
    extract_outputs(*stack, schema.returns().size(), inline_metas, counts);

    // -- Build TraceRing::Entry ---------------------------------------
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = schema_hash;
    entry.shape_hash = shape_hash;
    entry.num_inputs = counts.inputs;
    entry.num_outputs = counts.outputs;
    entry.num_scalar_args = scalars.count;

    // Pack op_flags: 5 bits of per-op context.
    uint8_t flags = 0;
    if (c10::InferenceMode::is_enabled()) flags |= crucible::op_flag::INFERENCE_MODE;
    if (c10::GradMode::is_enabled()) flags |= crucible::op_flag::GRAD_ENABLED;
    if (is_mutable) flags |= crucible::op_flag::IS_MUTABLE;
    flags |= (s_training_phase & 0x3) << crucible::op_flag::PHASE_SHIFT;
    if (dispatch_keys.has(c10::DispatchKey::Python)) flags |= crucible::op_flag::TORCH_FUNCTION;
    entry.op_flags = flags;

    for (uint16_t s = 0; s < scalars.count; s++)
        entry.scalar_values[s] = scalars.values[s];

    // -- Scope hash from TLS (set by Python module hooks) -------------
    const auto scope_hash = crucible::ScopeHash{state.scope_hash()};

    // -- Dispatch to Vigil --------------------------------------------
    //
    // COMPILED path: checks guards (~2ns), ignores metas.
    // RECORDING path: appends to ring + MetaLog (~15ns), uses metas.
    // Entry was built by this function from the ATen Stack + schema:
    // every field (schema_hash, shape_hash, counts, scalar_values,
    // op_flags, grad_enabled) comes from either the dispatcher-trusted
    // schema or the live c10 query surface.  Vouch at the typed boundary.
    //
    // dispatch_op_pure<>() (FOUND-I19): the row-typed facade pinning the
    // PyTorch fallback handler as a `Pure` caller — the ATen dispatcher
    // hands control here on the foreground producer thread, with no I/O,
    // Block, Bg, Init, Test, or Alloc effect in scope.  Migrating from
    // dispatch_op() to dispatch_op_pure<>() is zero-cost at runtime
    // (thin forwarder, default CallerRow = Row<>) and gives the
    // compile-time guarantee that this foreground hot path cannot
    // silently drift into a non-Pure context.
    (void)vigil->dispatch_op_pure(crucible::vouch(entry), inline_metas, counts.total(), scope_hash);
}

// =====================================================================
// Registration
//
// One line: catches every op in every namespace.  Fires on dlopen.
// =====================================================================

TORCH_LIBRARY_IMPL(_, Crucible, m) { m.fallback(torch::CppFunction::makeFromBoxedFunction<&crucibleFallback>()); }

// Profiler ops: passthrough (do not record).
// These are injected by PyTorch's profiling infrastructure and would
// pollute the trace with zero-tensor ops that break iteration detection.
TORCH_LIBRARY_IMPL(profiler, Crucible, m) {
    m.impl("_record_function_enter_new", torch::CppFunction::makeFallthrough());
    m.impl("_record_function_exit", torch::CppFunction::makeFallthrough());
}

// =====================================================================
// Tracing completeness notes
//
// What IS captured by DispatchKey::Crucible fallback:
//   [x] Forward pass ATen ops (mm, conv2d, relu, etc.)
//   [~] Backward/autograd decomposed ops (mm, addmm, threshold_backward, etc.)
//       The autograd engine decomposes backward into native ATen ops that
//       all go through the dispatcher. DispatchKey::Crucible is above
//       AutogradCPU/AutogradCUDA in the priority table, so this handler
//       fires on them.
//
//       Whether they are RECORDED depends on which thread runs them, and
//       that is a property of the device rather than of the op:
//
//         - A backward pass over CPU tensors runs on the thread that called
//           backward(), because that thread serves the engine's CPU ready
//           queue itself. It is the recording thread, so these ops are
//           recorded.
//         - A backward pass over accelerator tensors runs on the engine's
//           per-device worker thread. GraphTask carries the caller's
//           at::ThreadLocalState to it, and LocalDispatchKeySet is part of
//           that state, so the Crucible key arrives and this handler fires
//           — on a thread that is not the recording thread. Those ops
//           execute eagerly and are not recorded, either because
//           c10::CrucibleState did not travel and the mode reads INACTIVE,
//           or because the foreign-thread branch in crucibleFallback turns
//           them away.
//
//       So the recorded trace of an accelerator model is forward plus
//       optimizer, with the backward window absent from it. That window is
//       a gap in the trace, not a corruption of it: the op order that is
//       recorded is still exactly the order one thread produced.
//
//       Closing the gap is not a matter of admitting the worker thread to
//       the ring. Two threads have no defined emission order, so a trace
//       that contains both is a different trace on every run. It needs a
//       recording path that gives the backward window its own single
//       producer, which is a separate piece of work.
//   [x] Optimizer ops (SGD: add_, mul_, addcdiv_; Adam: mul_, add_, etc.)
//       optimizer.step() wraps ops in torch.no_grad(), but ops still dispatch
//       through Crucible. no_grad() only affects autograd key, not Crucible.
//   [x] Loss function ops (nll_loss, cross_entropy decomposed, mse_loss, etc.)
//   [x] In-place ops (add_, relu_, copy_, etc.) — same dispatch path
//   [x] View ops (reshape, transpose, expand, slice, etc.) — same dispatch
//   [x] Gradient accumulation ops (add_ on .grad tensors)
//
// What IS filtered (passthrough, not recorded):
//   [x] profiler::_record_function_enter_new/exit — bookkeeping noise
//
// Also captured (no special handling required):
//   [x] aten::_foreach_* ops (fused optimizer ops like _foreach_add_,
//       _foreach_mul_) — go through the ATen dispatcher. TensorList args
//       are unpacked by extract_inputs() isTensorList() handler.
//   [x] aten::empty, aten::zeros, aten::ones — allocation ops that produce
//       fresh tensors. Recorded for correct DFG birth tracking.
//   [x] aten::t, aten::reshape, aten::view — view/alias ops. Recorded for
//       ALIAS edge construction and stride change tracking.
//   [x] aten::lift_fresh — compiler-inserted materialization op. Mostly a
//       no-op in eager mode. Recorded (harmless in iteration signature).
//   [x] AccumulateGrad — NOT an ATen op (autograd Node), but the resulting
//       add_/copy_ on .grad tensors IS dispatched and recorded.
//
// What requires attention for future phases:
//   [ ] aten::detach, aten::alias — metadata-only ops that create no
//       computation. Currently recorded (contributes to op count accuracy)
//       but could be filtered for cleaner traces. Keep for now: they carry
//       data_ptr for correct dataflow edge construction.
//   [ ] Communication ops (all_reduce, broadcast, etc.) — ProcessGroup ops
//       do NOT go through the ATen dispatcher. They use a separate C++
//       class hierarchy (ProcessGroupNCCL, etc.). To capture them, we need
//       either: (a) custom hooks in ProcessGroup, or (b) intercept the
//       collective ops that ARE dispatched (c10d::allreduce_, etc.).
//       See CLAUDE.md L13 Distribution for the planned approach.
//   [ ] Custom autograd Functions (torch.autograd.Function) — the forward()
//       contains ATen ops that ARE dispatched. The backward() also contains
//       ATen ops that ARE dispatched. But the Function call itself is not
//       an ATen op. The ops inside both forward/backward are fully captured.
//   [ ] torch.compile / Inductor — if the model is compiled with
//       torch.compile, Inductor fuses ops into triton kernels. These fused
//       ops bypass the ATen dispatcher entirely. Crucible and torch.compile
//       are mutually exclusive execution strategies.
// =====================================================================

}  // anonymous namespace

// =====================================================================
// Exported TLS accessors (called from Python via ctypes or pybind11)
//
// libcrucible_dispatch.so links against libc10 (which defines the
// CrucibleState thread_local).  Python calls these after dlopen to
// set the Vigil handle and mode without additional fork modifications.
// =====================================================================

extern "C" {

// Prototypes first. These are the whole C ABI of libcrucible_dispatch.so,
// and crucible_native.py binds every one of them by name through ctypes.
// Nothing compiles against a header, so without this block each definition
// is its own first declaration and -Wmissing-declarations fires on all of
// them. Stating the ABI once, in one place, is what silences it: a symbol
// Python calls but this block does not name is a symbol nobody declared.
CRUCIBLE_API void crucible_dispatch_set_tls_mode(uint8_t mode);
CRUCIBLE_API void crucible_dispatch_set_tls_context(void* ctx);
CRUCIBLE_API uint8_t crucible_dispatch_get_tls_mode();
CRUCIBLE_API void* crucible_dispatch_get_tls_context();
CRUCIBLE_API void crucible_dispatch_set_tls_scope(uint64_t scope_hash);
CRUCIBLE_API uint64_t crucible_dispatch_get_tls_scope();
CRUCIBLE_API void crucible_dispatch_set_training_phase(uint8_t phase);
CRUCIBLE_API uint8_t crucible_dispatch_get_training_phase();
CRUCIBLE_API uint32_t crucible_dispatch_schema_count();
CRUCIBLE_API int crucible_dispatch_schema_entry(uint32_t i, uint64_t* out_hash, const char** out_name);

CRUCIBLE_API void crucible_dispatch_set_tls_mode(uint8_t mode) {
    c10::CrucibleState::get_tls_state().set_mode(static_cast<c10::CrucibleMode>(mode));
}

CRUCIBLE_API void crucible_dispatch_set_tls_context(void* ctx) { c10::CrucibleState::get_tls_state().set_context(ctx); }

CRUCIBLE_API uint8_t crucible_dispatch_get_tls_mode() {
    return static_cast<uint8_t>(c10::CrucibleState::get_tls_state().mode());
}

CRUCIBLE_API void* crucible_dispatch_get_tls_context() { return c10::CrucibleState::get_tls_state().context(); }

CRUCIBLE_API void crucible_dispatch_set_tls_scope(uint64_t scope_hash) {
    c10::CrucibleState::get_tls_state().set_scope_hash(scope_hash);
}

CRUCIBLE_API uint64_t crucible_dispatch_get_tls_scope() { return c10::CrucibleState::get_tls_state().scope_hash(); }

// ── Training phase TLS ─────────────────────────────────────────────
//
// Thread-local training phase, set by Python controller to distinguish
// forward/backward/optimizer passes. Packed into op_flags bits 2-3.
// Lives in the dispatch lib — no PyTorch patch needed.

CRUCIBLE_API void crucible_dispatch_set_training_phase(uint8_t phase) { s_training_phase = phase & 0x3; }

CRUCIBLE_API uint8_t crucible_dispatch_get_training_phase() { return s_training_phase; }

// ── Schema table accessors ──────────────────────────────────────────
//
// libcrucible_dispatch.so has its own copy of global_schema_table()
// (inline static local, one per .so).  The vessel lib (vessel_api.cpp)
// has a DIFFERENT copy.  To export correct .crtrace files, Python must
// copy schema names from this table to the vessel lib's table before
// calling crucible_export_crtrace().
//
// These accessors expose the dispatch lib's schema table for that copy.

CRUCIBLE_API uint32_t crucible_dispatch_schema_count() { return crucible::global_schema_table().count(); }

// Get the schema hash and name for the i-th entry.
// Returns 0 if i >= count.  Writes hash and name pointer.
CRUCIBLE_API int crucible_dispatch_schema_entry(uint32_t i, uint64_t* out_hash, const char** out_name) {
    const auto& table = crucible::global_schema_table();
    if (i >= table.count()) return 0;
    if (out_hash) *out_hash = table.entries[i].hash.raw();
    if (out_name) *out_name = table.entries[i].name;
    return 1;
}

}  // extern "C"
