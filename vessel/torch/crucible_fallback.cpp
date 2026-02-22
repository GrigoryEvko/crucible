// Crucible native dispatcher fallback for DispatchKey::Crucible.
//
// Registers a boxed fallback that intercepts every ATen op when the key
// is enabled in TLS.  Reads CrucibleState to get the Vigil handle, extracts
// tensor metadata from the IValue stack, and feeds it to Vigil::dispatch_op().
//
// This is the C++ counterpart of crucible_mode.py (TorchDispatchMode).
// Where Python adds ~30-50μs/op overhead, this path adds ~100ns/op.
//
// Build requirements:
//   - PyTorch fork with DispatchKey::Crucible + CrucibleState TLS
//   - Crucible headers (Vigil.h, TraceRing.h, MerkleDag.h, Types.h)
//
// Loaded via torch.ops.load_library() — the TORCH_LIBRARY_IMPL registration
// fires on dlopen, no explicit init needed.

#include <c10/core/CrucibleState.h>

#include <ATen/core/dispatch/Dispatcher.h>
#include <ATen/core/ivalue.h>
#include <ATen/core/stack.h>
#include <torch/library.h>

#include <crucible/MerkleDag.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <crucible/Vigil.h>

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

// ── FNV-1a 64-bit ────────────────────────────────────────────────────
//
// Must produce identical hashes to vessel_api.cpp's crucible_hash_string
// and crucible_hash_shapes.  Same offset basis, same prime, same byte
// order — hash("aten::mm.default") here == hash("aten::mm.default") in
// the Python ctypes path.

static constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
static constexpr uint64_t FNV_PRIME  = 0x100000001b3ULL;

// ── Schema hash cache ────────────────────────────────────────────────
//
// Thread-local direct-mapped cache: OperatorHandle address → SchemaHash.
// After warmup (first iteration), every op is a cache hit: one pointer
// comparison + one array index.  Zero contention (thread-local).
//
// 2048 slots × 16B = 32KB — fits in L1d.  With >>4 shift for pointer
// alignment (OperatorHandle ≥16B), collision rate is near zero for
// typical models (<500 unique ops).

struct SchemaHashSlot {
    const void* key = nullptr;
    crucible::SchemaHash hash;
};

static constexpr uint32_t SCHEMA_CACHE_SIZE = 2048;
static constexpr uint32_t SCHEMA_CACHE_MASK = SCHEMA_CACHE_SIZE - 1;
static thread_local SchemaHashSlot schema_cache[SCHEMA_CACHE_SIZE]{};

[[nodiscard]] static crucible::SchemaHash get_schema_hash(
    const c10::OperatorHandle& op)
{
    const auto idx = (reinterpret_cast<uintptr_t>(&op) >> 4)
                     & SCHEMA_CACHE_MASK;
    auto& slot = schema_cache[idx];
    if (slot.key == &op) [[likely]]
        return slot.hash;

    // Cache miss: compute FNV-1a over "namespace::name.overload".
    // Matches Python's func.name() → crucible_hash_string().
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

    slot.key = &op;
    slot.hash = crucible::SchemaHash{h};
    return slot.hash;
}

// ── Shape hash ───────────────────────────────────────────────────────
//
// FNV-1a over (ndim, sizes[0..ndim-1]) per input tensor.
// Identical to vessel_api.cpp crucible_hash_shapes().

[[nodiscard]] static crucible::ShapeHash compute_shape_hash(
    const crucible::TensorMeta* metas, uint16_t n_inputs)
{
    uint64_t h = FNV_OFFSET;
    for (uint16_t i = 0; i < n_inputs; i++) {
        // ndim as 1-byte separator
        h ^= metas[i].ndim;
        h *= FNV_PRIME;
        // sizes[0..ndim-1] as raw bytes
        const auto* p = reinterpret_cast<const uint8_t*>(metas[i].sizes);
        const uint32_t nbytes = metas[i].ndim * sizeof(int64_t);
        for (uint32_t b = 0; b < nbytes; b++) {
            h ^= p[b];
            h *= FNV_PRIME;
        }
    }
    return crucible::ShapeHash{h};
}

// ── Tensor metadata extraction ───────────────────────────────────────
//
// Fill crucible::TensorMeta from an at::Tensor.  Handles strided and
// non-strided layouts (sparse → strides=0, data_ptr=nullptr).

static void fill_meta(crucible::TensorMeta& meta, const at::Tensor& t) {
    meta = {};  // zero-init (InitSafe — NSDMI defaults)
    if (!t.defined()) return;

    const auto ndim = static_cast<uint8_t>(
        std::min(t.dim(), static_cast<int64_t>(8)));
    meta.ndim = ndim;

    const auto sizes = t.sizes();
    for (uint8_t d = 0; d < ndim; d++)
        meta.sizes[d] = sizes[d];

    if (t.layout() == c10::Layout::Strided) {
        const auto strides = t.strides();
        for (uint8_t d = 0; d < ndim; d++)
            meta.strides[d] = strides[d];
        meta.data_ptr = t.data_ptr();
    }
    // Non-strided: strides and data_ptr remain 0/nullptr from zero-init.

    meta.dtype = static_cast<crucible::ScalarType>(
        static_cast<int8_t>(t.scalar_type()));
    meta.device_type = static_cast<crucible::DeviceType>(
        static_cast<int8_t>(t.device().type()));
    meta.device_idx = static_cast<int8_t>(
        t.device().has_index() ? t.device().index() : -1);
    meta.layout = static_cast<crucible::Layout>(
        static_cast<int8_t>(t.layout()));
}

// ── Scalar extraction ────────────────────────────────────────────────
//
// Extract up to 5 non-tensor scalar args from the IValue stack,
// bitcast to int64_t for TraceRing::Entry.scalar_values[].

static int64_t scalar_to_int64(const c10::IValue& iv) {
    if (iv.isInt())    return iv.toInt();
    if (iv.isBool())   return iv.toBool() ? 1 : 0;
    if (iv.isDouble()) return std::bit_cast<int64_t>(iv.toDouble());
    return 0;
}

// ── Inline meta buffer ───────────────────────────────────────────────
//
// Stack-allocated buffer for common case.  32 tensors × 144B = 4608B.
// Covers virtually all ATen ops (most have 2-5 tensors).

static constexpr uint32_t MAX_INLINE_METAS = 32;

// ── The fallback ─────────────────────────────────────────────────────

static const auto AFTER_CRUCIBLE_KEYSET = c10::DispatchKeySet(
    c10::DispatchKeySet::FULL_AFTER, c10::DispatchKey::Crucible);

void crucibleFallback(
    const c10::OperatorHandle& op,
    c10::DispatchKeySet dispatch_keys,
    torch::jit::Stack* stack)
{
    auto& state = c10::CrucibleState::get_tls_state();

    // ── Fast path: INACTIVE — pure passthrough, zero work ────────
    if (state.mode() == c10::CrucibleMode::INACTIVE) [[likely]] {
        op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);
        return;
    }

    // ── Get Vigil from TLS context ───────────────────────────────
    auto* vigil = static_cast<crucible::Vigil*>(state.context());
    if (!vigil) [[unlikely]] {
        op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);
        return;
    }

    const auto& schema = op.schema();

    // ── Extract input tensor metadata ────────────────────────────
    const auto num_args = schema.arguments().size();
    const auto args_begin = stack->size() - num_args;

    crucible::TensorMeta inline_metas[MAX_INLINE_METAS]{};
    uint16_t num_inputs = 0;
    uint16_t num_scalars = 0;
    int64_t scalar_values[5]{};

    for (size_t i = 0; i < num_args; i++) {
        const auto& iv = (*stack)[args_begin + i];

        if (iv.isTensor()) {
            const auto& t = iv.toTensor();
            if (t.defined() && num_inputs < MAX_INLINE_METAS) {
                fill_meta(inline_metas[num_inputs], t);
                num_inputs++;
            }
        } else if (iv.isTensorList()) {
            for (const at::Tensor& t : iv.toTensorList()) {
                if (t.defined() && num_inputs < MAX_INLINE_METAS) {
                    fill_meta(inline_metas[num_inputs], t);
                    num_inputs++;
                }
            }
        } else if (iv.isOptionalTensorList()) {
            for (const auto& ref : iv.toOptionalTensorList()) {
                const auto opt = static_cast<
                    std::optional<at::Tensor>>(ref);
                if (opt.has_value() && opt->defined()
                    && num_inputs < MAX_INLINE_METAS) {
                    fill_meta(inline_metas[num_inputs], *opt);
                    num_inputs++;
                }
            }
        } else if (num_scalars < 5
                   && (iv.isInt() || iv.isDouble() || iv.isBool())) {
            scalar_values[num_scalars++] = scalar_to_int64(iv);
        }
    }

    // ── Compute hashes ───────────────────────────────────────────
    const auto schema_hash = get_schema_hash(op);
    const auto shape_hash  = compute_shape_hash(inline_metas, num_inputs);

    // ── Execute eagerly (Tier 1: always redispatch) ──────────────
    op.redispatchBoxed(dispatch_keys & AFTER_CRUCIBLE_KEYSET, stack);

    // ── Extract output tensor metadata ───────────────────────────
    const auto num_returns = schema.returns().size();
    uint16_t num_outputs = 0;

    if (num_returns > 0 && stack->size() >= num_returns) {
        const auto rets_begin = stack->size() - num_returns;
        for (size_t i = 0; i < num_returns; i++) {
            const auto& iv = (*stack)[rets_begin + i];
            if (iv.isTensor()) {
                const auto& t = iv.toTensor();
                if (t.defined()
                    && (num_inputs + num_outputs) < MAX_INLINE_METAS) {
                    fill_meta(inline_metas[num_inputs + num_outputs], t);
                    num_outputs++;
                }
            } else if (iv.isTensorList()) {
                for (const at::Tensor& t : iv.toTensorList()) {
                    if (t.defined()
                        && (num_inputs + num_outputs) < MAX_INLINE_METAS) {
                        fill_meta(
                            inline_metas[num_inputs + num_outputs], t);
                        num_outputs++;
                    }
                }
            }
        }
    }

    // ── Build TraceRing::Entry ────────────────────────────────────
    crucible::TraceRing::Entry entry{};
    entry.schema_hash    = schema_hash;
    entry.shape_hash     = shape_hash;
    entry.num_inputs     = num_inputs;
    entry.num_outputs    = num_outputs;
    entry.num_scalar_args = num_scalars;
    entry.grad_enabled   = c10::GradMode::is_enabled();
    entry.inference_mode = c10::InferenceMode::is_enabled();

    const uint16_t n = num_scalars < 5 ? num_scalars : 5;
    for (uint16_t s = 0; s < n; s++)
        entry.scalar_values[s] = scalar_values[s];

    // ── Dispatch to Vigil ────────────────────────────────────────
    //
    // COMPILED path: checks guards (~2ns), ignores metas.
    // RECORDING path: appends to ring + MetaLog (~15ns), uses metas.
    const auto n_metas = static_cast<uint32_t>(num_inputs + num_outputs);
    (void)vigil->dispatch_op(entry, inline_metas, n_metas);
}

// ── Registration ─────────────────────────────────────────────────────
//
// One line: catches every op in every namespace.  Fires on dlopen.
TORCH_LIBRARY_IMPL(_, Crucible, m) {
    m.fallback(torch::CppFunction::makeFromBoxedFunction<&crucibleFallback>());
}

// ── Profiler ops: passthrough (do not record) ────────────────────────
//
// These are injected by PyTorch's profiling infrastructure and would
// pollute the trace with zero-tensor ops that break iteration detection.
TORCH_LIBRARY_IMPL(profiler, Crucible, m) {
    m.impl("_record_function_enter_new",
           torch::CppFunction::makeFallthrough());
    m.impl("_record_function_exit",
           torch::CppFunction::makeFallthrough());
}

} // anonymous namespace

// ── Exported TLS accessors (called from Python via ctypes) ───────────
//
// libcrucible_dispatch.so links against libc10 (which defines the
// CrucibleState thread_local).  Python calls these after dlopen to
// set the Vigil handle and mode without additional fork modifications.
//
// This avoids adding pybind11 bindings to the fork — the only fork
// changes remain the 126-line c10 patch (DispatchKey + CrucibleState).

extern "C" {

__attribute__((visibility("default")))
void crucible_dispatch_set_tls_mode(uint8_t mode) {
    c10::CrucibleState::get_tls_state().set_mode(
        static_cast<c10::CrucibleMode>(mode));
}

__attribute__((visibility("default")))
void crucible_dispatch_set_tls_context(void* ctx) {
    c10::CrucibleState::get_tls_state().set_context(ctx);
}

__attribute__((visibility("default")))
uint8_t crucible_dispatch_get_tls_mode() {
    return static_cast<uint8_t>(
        c10::CrucibleState::get_tls_state().mode());
}

__attribute__((visibility("default")))
void* crucible_dispatch_get_tls_context() {
    return c10::CrucibleState::get_tls_state().context();
}

} // extern "C"
