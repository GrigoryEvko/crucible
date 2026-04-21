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
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <crucible/Vigil.h>

#include <bit>
#include <cstdint>
#include <cstring>

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
static constexpr uint64_t FNV_PRIME  = 0x100000001b3ULL;

[[nodiscard]] static uint64_t fnv1a_bytes(const void* data, size_t len,
                                           uint64_t h = FNV_OFFSET) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

[[nodiscard]] static uint64_t fnv1a_str(const char* s, size_t len) {
    return fnv1a_bytes(s, len);
}

// =====================================================================
// Extraction result: strong-typed intermediate from IValue stack
//
// Separates "what we extracted from PyTorch" from "how we feed Vigil."
// Every field has a semantic type -- no raw uint16_t counts.
// =====================================================================

// Number of tensor metas extracted from one op's args/returns.
struct MetaCount {
    uint16_t inputs  = 0;
    uint16_t outputs = 0;

    [[nodiscard]] uint32_t total() const {
        return static_cast<uint32_t>(inputs) + outputs;
    }
};

// Up to 5 scalar arguments, bitcast to int64_t.
struct ScalarArgs {
    int64_t  values[5]{};
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
struct SchemaHashSlot {
    const void*          key  = nullptr;
    crucible::SchemaHash hash;
};

static constexpr uint32_t SCHEMA_CACHE_CAP  = 2048;
static constexpr uint32_t SCHEMA_CACHE_MASK = SCHEMA_CACHE_CAP - 1;
static thread_local SchemaHashSlot schema_cache[SCHEMA_CACHE_CAP]{};
static thread_local bool schema_is_mutable[SCHEMA_CACHE_CAP]{};

struct SchemaInfo {
    crucible::SchemaHash hash;
    bool is_mutable = false;
};

[[nodiscard]] static SchemaInfo get_schema_info(
    const c10::OperatorHandle& op,
    const c10::FunctionSchema& schema)
{
    const auto idx = (reinterpret_cast<uintptr_t>(&op) >> 4)
                     & SCHEMA_CACHE_MASK;
    auto& slot = schema_cache[idx];
    if (slot.key == &op) [[likely]]
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
    crucible::register_schema_name(schema_hash,
        crucible::SchemaTable::SanitizedName{full_name.c_str()});

    // Authoritative mutability from schema alias annotations.
    // Catches both in-place ops (add_.Tensor: self(a!)) and out= variants
    // (add.out: out(a!)). FunctionSchema::is_mutable() checks if any
    // argument has AliasInfo with isWrite() == true.
    const bool mutable_op = schema.is_mutable();

    slot.key  = &op;
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
[[nodiscard]] static crucible::ShapeHash compute_shape_hash(
    const crucible::TensorMeta* metas, uint16_t n_inputs)
{
    uint64_t h = FNV_OFFSET;
    for (uint16_t i = 0; i < n_inputs; i++) {
        // Hash ndim as separator (1 byte), continuing the chain.
        h = fnv1a_bytes(&metas[i].ndim, 1, h);
        // Hash sizes[0..ndim-1], continuing the chain.
        const uint32_t nbytes = metas[i].ndim * sizeof(int64_t);
        h = fnv1a_bytes(metas[i].sizes, nbytes, h);
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

    const auto ndim = static_cast<uint8_t>(
        std::min(t.dim(), static_cast<int64_t>(8)));
    meta.ndim = ndim;

    const auto sizes = t.sizes();
    for (uint8_t d = 0; d < ndim; d++)
        meta.sizes[d] = sizes[d];

    const bool strided = (t.layout() == c10::Layout::Strided);
    if (strided) {
        const auto strides = t.strides();
        for (uint8_t d = 0; d < ndim; d++)
            meta.strides[d] = strides[d];
        meta.data_ptr = t.data_ptr();
    }

    meta.dtype = static_cast<crucible::ScalarType>(
        static_cast<int8_t>(t.scalar_type()));
    meta.device_type = static_cast<crucible::DeviceType>(
        static_cast<int8_t>(t.device().type()));
    meta.device_idx = static_cast<int8_t>(
        t.device().has_index() ? t.device().index() : -1);
    meta.layout = static_cast<crucible::Layout>(
        static_cast<int8_t>(t.layout()));

    // -- Extended fields (autograd + storage) --------------------------

    meta.requires_grad = t.requires_grad();

    uint8_t flags = 0;
    if (t.is_leaf())                        flags |= crucible::meta_flags::IS_LEAF;
    if (strided && t.is_contiguous())       flags |= crucible::meta_flags::IS_CONTIGUOUS;
    if (t.is_neg())                         flags |= crucible::meta_flags::IS_NEG;
    if (t.is_conj())                        flags |= crucible::meta_flags::IS_CONJ;

    auto* impl = t.unsafeGetTensorImpl();
    auto* am   = impl->autograd_meta();
    if (am) {
        auto* node = torch::autograd::impl::grad_fn_unsafe(t);
        if (node) {
            flags |= crucible::meta_flags::HAS_GRAD_FN;
            const auto& gfn_name = node->name();
            meta.grad_fn_hash = fnv1a_str(gfn_name.data(), gfn_name.size());
        }
        meta.output_nr = static_cast<uint8_t>(
            torch::autograd::impl::get_autograd_meta(t)->output_nr_ & 0xFF);
    }

    // View detection: check autograd is_view_ flag first (catches expand,
    // as_strided, narrow etc. that share storage base and zero offset).
    // Fall back to data_ptr != storage_base and storage_offset != 0 checks
    // for non-autograd views.
    if (am && static_cast<torch::autograd::AutogradMeta*>(am)->is_view_)
        flags |= crucible::meta_flags::IS_VIEW;

    if (strided && impl->has_storage()) {
        auto* storage_base = impl->storage().data_ptr().get();
        if (storage_base != nullptr &&
            t.data_ptr() != static_cast<char*>(storage_base)) {
            flags |= crucible::meta_flags::IS_VIEW;
        }
        meta.storage_nbytes = static_cast<uint32_t>(
            impl->storage().nbytes() & 0xFFFFFFFF);
    }
    if (strided && t.storage_offset() != 0) {
        flags |= crucible::meta_flags::IS_VIEW;
        meta.storage_offset = t.storage_offset();
    }

    meta.flags = flags;

    meta.version = static_cast<uint32_t>(
        impl->version_counter().current_version() & 0xFFFFFFFF);
}

// =====================================================================
// Scalar extraction
//
// Bitcast non-tensor scalars to int64_t for TraceRing::Entry.
// =====================================================================

[[nodiscard]] static int64_t scalar_to_int64(const c10::IValue& iv) {
    if (iv.isInt())    return iv.toInt();
    if (iv.isBool())   return iv.toBool() ? 1 : 0;
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
    MetaCount  counts;
    ScalarArgs scalars;
};

[[nodiscard]] static ExtractionResult extract_inputs(
    const torch::jit::Stack& stack,
    size_t args_begin,
    size_t num_args,
    crucible::TensorMeta* metas)
{
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
            for (const at::Tensor& t : iv.toTensorList()) {
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
                if (opt.has_value() && opt->defined()
                    && r.counts.inputs < MAX_INLINE_METAS) {
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

static void extract_outputs(
    const torch::jit::Stack& stack,
    size_t num_returns,
    crucible::TensorMeta* metas,
    MetaCount& counts)
{
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
            for (const at::Tensor& t : iv.toTensorList()) {
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

static const auto AFTER_CRUCIBLE_KEYSET = c10::DispatchKeySet(
    c10::DispatchKeySet::FULL_AFTER, c10::DispatchKey::Crucible);

void crucibleFallback(
    const c10::OperatorHandle& op,
    c10::DispatchKeySet dispatch_keys,
    torch::jit::Stack* stack)
{
    auto& state = c10::CrucibleState::get_tls_state();

    // -- Fast path: INACTIVE -- pure passthrough, zero work -----------
    if (state.mode() == c10::CrucibleMode::INACTIVE) [[likely]] {
        op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);
        return;
    }

    // -- Get Vigil from TLS context -----------------------------------
    auto* vigil = static_cast<crucible::Vigil*>(state.context());
    if (!vigil) [[unlikely]] {
        op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);
        return;
    }

    const auto& schema = op.schema();

    // -- Extract input tensor metadata + scalars ----------------------
    const auto num_args   = schema.arguments().size();
    // Guard: schema.arguments().size() > stack->size() should never happen
    // with a well-formed dispatcher, but protect against underflow (UB).
    if (num_args > stack->size()) [[unlikely]] {
        op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);
        return;
    }
    const auto args_begin = stack->size() - num_args;

    crucible::TensorMeta inline_metas[MAX_INLINE_METAS]{};
    auto [counts, scalars] = extract_inputs(
        *stack, args_begin, num_args, inline_metas);

    // -- Compute hashes + mutability -----------------------------------
    const auto [schema_hash, is_mutable] = get_schema_info(op, schema);
    const auto shape_hash = compute_shape_hash(inline_metas, counts.inputs);

    // -- Execute eagerly (Tier 1: always redispatch) ------------------
    op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);

    // -- Extract output tensor metadata -------------------------------
    extract_outputs(*stack, schema.returns().size(), inline_metas, counts);

    // -- Build TraceRing::Entry ---------------------------------------
    crucible::TraceRing::Entry entry{};
    entry.schema_hash     = schema_hash;
    entry.shape_hash      = shape_hash;
    entry.num_inputs      = counts.inputs;
    entry.num_outputs     = counts.outputs;
    entry.num_scalar_args = scalars.count;
    entry.grad_enabled    = c10::GradMode::is_enabled();

    // Pack op_flags: 5 bits of per-op context.
    uint8_t flags = 0;
    if (c10::InferenceMode::is_enabled())
        flags |= crucible::op_flag::INFERENCE_MODE;
    if (is_mutable)
        flags |= crucible::op_flag::IS_MUTABLE;
    flags |= (s_training_phase & 0x3) << crucible::op_flag::PHASE_SHIFT;
    if (dispatch_keys.has(c10::DispatchKey::Python))
        flags |= crucible::op_flag::TORCH_FUNCTION;
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
    (void)vigil->dispatch_op(crucible::vouch(entry), inline_metas, counts.total(),
                             scope_hash);
}

// =====================================================================
// Registration
//
// One line: catches every op in every namespace.  Fires on dlopen.
// =====================================================================

TORCH_LIBRARY_IMPL(_, Crucible, m) {
    m.fallback(torch::CppFunction::makeFromBoxedFunction<&crucibleFallback>());
}

// Profiler ops: passthrough (do not record).
// These are injected by PyTorch's profiling infrastructure and would
// pollute the trace with zero-tensor ops that break iteration detection.
TORCH_LIBRARY_IMPL(profiler, Crucible, m) {
    m.impl("_record_function_enter_new",
           torch::CppFunction::makeFallthrough());
    m.impl("_record_function_exit",
           torch::CppFunction::makeFallthrough());
}

// =====================================================================
// Tracing completeness notes
//
// What IS captured by DispatchKey::Crucible fallback:
//   [x] Forward pass ATen ops (mm, conv2d, relu, etc.)
//   [x] Backward/autograd decomposed ops (mm, addmm, threshold_backward, etc.)
//       The autograd engine decomposes backward into native ATen ops that
//       all go through the dispatcher. DispatchKey::Crucible is above
//       AutogradCPU/AutogradCUDA in the priority table, so we see them.
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

} // anonymous namespace

// =====================================================================
// Exported TLS accessors (called from Python via ctypes or pybind11)
//
// libcrucible_dispatch.so links against libc10 (which defines the
// CrucibleState thread_local).  Python calls these after dlopen to
// set the Vigil handle and mode without additional fork modifications.
// =====================================================================

extern "C" {

CRUCIBLE_API void crucible_dispatch_set_tls_mode(uint8_t mode) {
    c10::CrucibleState::get_tls_state().set_mode(
        static_cast<c10::CrucibleMode>(mode));
}

CRUCIBLE_API void crucible_dispatch_set_tls_context(void* ctx) {
    c10::CrucibleState::get_tls_state().set_context(ctx);
}

CRUCIBLE_API uint8_t crucible_dispatch_get_tls_mode() {
    return static_cast<uint8_t>(
        c10::CrucibleState::get_tls_state().mode());
}

CRUCIBLE_API void* crucible_dispatch_get_tls_context() {
    return c10::CrucibleState::get_tls_state().context();
}

CRUCIBLE_API void crucible_dispatch_set_tls_scope(uint64_t scope_hash) {
    c10::CrucibleState::get_tls_state().set_scope_hash(scope_hash);
}

CRUCIBLE_API uint64_t crucible_dispatch_get_tls_scope() {
    return c10::CrucibleState::get_tls_state().scope_hash();
}

// ── Training phase TLS ─────────────────────────────────────────────
//
// Thread-local training phase, set by Python controller to distinguish
// forward/backward/optimizer passes. Packed into op_flags bits 2-3.
// Lives in the dispatch lib — no PyTorch patch needed.

CRUCIBLE_API void crucible_dispatch_set_training_phase(uint8_t phase) {
    s_training_phase = phase & 0x3;
}

CRUCIBLE_API uint8_t crucible_dispatch_get_training_phase() {
    return s_training_phase;
}

// ── Schema table accessors ──────────────────────────────────────────
//
// libcrucible_dispatch.so has its own copy of global_schema_table()
// (inline static local, one per .so).  The vessel lib (vessel_api.cpp)
// has a DIFFERENT copy.  To export correct .crtrace files, Python must
// copy schema names from this table to the vessel lib's table before
// calling crucible_export_crtrace().
//
// These accessors expose the dispatch lib's schema table for that copy.

CRUCIBLE_API uint32_t crucible_dispatch_schema_count() {
    return crucible::global_schema_table().count();
}

// Get the schema hash and name for the i-th entry.
// Returns 0 if i >= count.  Writes hash and name pointer.
CRUCIBLE_API int crucible_dispatch_schema_entry(
    uint32_t i, uint64_t* out_hash, const char** out_name)
{
    const auto& table = crucible::global_schema_table();
    if (i >= table.count()) return 0;
    if (out_hash) *out_hash = table.entries[i].hash.raw();
    if (out_name) *out_name = table.entries[i].name;
    return 1;
}

} // extern "C"
