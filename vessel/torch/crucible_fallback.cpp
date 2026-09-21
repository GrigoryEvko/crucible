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

// First, and before every crucible/ include below: record_kernel.h parses the
// two sibling substrates in a fixed order so each keeps its own spelling of the
// five CRUCIBLE_ macros the two Platform.h files define differently, and it
// refuses to compile after one of them.
#include "record_kernel.h"

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
// The bridge this path shares with the unboxed kernels
//
// FNV-1a, the metadata fill, the shape hash, the slot limits and the phase
// derivation all live in record_kernel.h. Two spellings of any of them would
// let the boxed and the unboxed path write different entries for one
// operation, which is the property the whole design rests on. The names are
// pulled in here so the call sites below read as they did when the definitions
// were local.
//
// The hashes must also match vessel_api.cpp's crucible_hash_string and
// crucible_hash_shapes, so that hash("aten::mm.default") is the same value in
// the Python ctypes path.
// =====================================================================

using crucible::vessel::fnv1a_bytes;
using crucible::vessel::fnv1a_str;
using crucible::vessel::kFnvOffset;
using crucible::vessel::kFnvPrime;
using crucible::vessel::MAX_INLINE_METAS;
using crucible::vessel::MetaCount;
using crucible::vessel::ScalarArgs;
using crucible::vessel::backward_depth;
using crucible::vessel::compute_shape_hash;
using crucible::vessel::fill_meta;
using crucible::vessel::is_foreach_op_name;
using crucible::vessel::phase_flag_bits;

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

// Per-op cached info: schema hash, is_mutable flag, is_foreach flag.
// SchemaHash (16B slot) + two parallel arrays of 1B = 36KB total.
// Fits comfortably in L1d (48KB Zen 4). The parallel arrays avoid
// bloating slots to 24B which would spill L1d.
//
// Both flags are properties of the operator rather than of the operation, so
// both are computed once at the cache miss below. is_foreach is a substring
// search over the operator name, which the unboxed path answers at compile
// time; caching it here keeps the hot boxed path at one array index.
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
static thread_local bool schema_is_foreach[SCHEMA_CACHE_CAP]{};

struct SchemaInfo {
    crucible::SchemaHash hash;
    bool is_mutable = false;
    bool is_foreach = false;
};

[[nodiscard]] static SchemaInfo get_schema_info(const c10::OperatorHandle& op, const c10::FunctionSchema& schema) {
    // The handle's address is the cache key. bit_cast reproduces it as an
    // integer for the index arithmetic; the pointer itself is compared
    // below, so the integer never turns back into one.
    const auto idx = (std::bit_cast<std::uintptr_t>(&op) >> 4) & SCHEMA_CACHE_MASK;
    auto& slot = schema_cache[idx];
    if (slot.key.value() == &op) [[likely]]
        return {slot.hash, schema_is_mutable[idx], schema_is_foreach[idx]};

    // Cache miss: compute FNV-1a over "namespace::name.overload".
    const auto& name = op.operator_name();
    uint64_t h = kFnvOffset;
    for (char c : name.name) {
        h ^= static_cast<uint8_t>(c);
        h *= kFnvPrime;
    }
    if (!name.overload_name.empty()) {
        h ^= static_cast<uint8_t>('.');
        h *= kFnvPrime;
        for (char c : name.overload_name) {
            h ^= static_cast<uint8_t>(c);
            h *= kFnvPrime;
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

    // Half of what the phase derivation reads to name an operation an
    // optimizer operation. The overload never carries the infix, so the base
    // name is the whole question.
    const bool foreach_op = is_foreach_op_name(name.name);

    // Re-tag at the FFI source: the OperatorHandle pointer just
    // crossed the boxed-fallback boundary, so it carries source::External
    // until something downstream proves otherwise.  Construction is
    // explicit per Tagged's API; the wrapper is move-assigned
    // into the slot at zero runtime cost.
    slot.key = ExternalOpKey{&op};
    slot.hash = schema_hash;
    schema_is_mutable[idx] = mutable_op;
    schema_is_foreach[idx] = foreach_op;
    return {schema_hash, mutable_op, foreach_op};
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
    // A recording session closes that window upstream instead of widening
    // this door.  CrucibleNative holds the autograd engine on the thread
    // that calls backward() for every window, the replayed ones included,
    // so the backward pass has one producer and arrives here on that
    // thread.  A replayed dispatch records nothing but it advances the
    // replay cursor, so a window turned away here leaves the cursor
    // standing still until the next op fails its guard.  Refer to
    // the serialisation note in vessel/torch/crucible_native.py.  This
    // branch then carries the sessions that do not arm that guard, and any
    // thread the engine still owns outside a backward pass.
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
    const auto [schema_hash, is_mutable, is_foreach] = get_schema_info(op, schema);
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
    flags |= phase_flag_bits(is_foreach);
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
    //
    // The trust ladder.  Every field of the Entry was read out of the
    // ATen stack and the live c10 query surface, which is a foreign
    // runtime, so the Entry starts at the first tag and reaches a
    // recording entry point only by passing the same checks the C ABI
    // thunks pass.  The operation already ran eagerly above, so a
    // rejected Entry costs the trace this operation and costs the caller
    // nothing.
    auto validated =
        crucible::vessel::mint_validated_entry(crucible::mint_ffi_entry(entry), inline_metas, counts.total());
    if (!validated) [[unlikely]]
        return;

    // dispatch_op_pure<>() (FOUND-I19): the row-typed facade pinning the
    // PyTorch fallback handler as a `Pure` caller — the ATen dispatcher
    // hands control here on the foreground producer thread, with no I/O,
    // Block, Bg, Init, Test, or Alloc effect in scope.  Migrating from
    // dispatch_op() to dispatch_op_pure<>() is zero-cost at runtime
    // (thin forwarder, default CallerRow = Row<>) and gives the
    // compile-time guarantee that this foreground hot path cannot
    // silently drift into a non-Pure context.
    (void)vigil->dispatch_op_pure(*validated, inline_metas, counts.total(), scope_hash);
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
//           per-device worker thread by default. GraphTask carries the
//           caller's at::ThreadLocalState to it, and LocalDispatchKeySet is
//           part of that state, so the Crucible key arrives and this
//           handler fires — on a thread that is not the recording thread.
//
//       Closing that gap is not a matter of admitting the worker thread to
//       the ring. Two threads have no defined emission order, so a trace
//       that contains both is a different trace on every run. It needs a
//       recording path that gives the backward window its own single
//       producer, and a recording session now arranges one: it holds the
//       engine on the thread that calls backward(), for every window the
//       Vigil sees rather than for the recorded ones alone.
//       Engine::ready_queue sends every node to the graph task's CPU ready
//       queue when multithreading is off, and the calling thread drives
//       that queue, so one thread produces the whole window. Refer to the
//       serialisation note in vessel/torch/crucible_native.py.
//
//       Measured on cuda:0 over 12 iterations of the MiniGPT in
//       examples/record_cuda.py, against a CPU control. Without the guard
//       the recorded region held 285 ops and 51 distinct schemas, and the
//       control recorded 7 backward schemas the accelerator arm did not.
//       With it the region holds 801 ops and 65 schemas and the count of
//       missing backward schemas is 0. A shape with the token table on the
//       host, which runs its backward pass on two threads at once by
//       default, reaches 898 ops and 0 missing backward schemas. Both
//       shapes replay that region from iteration 2 to iteration 11 with no
//       divergence, which is what says the region holds a whole period and
//       nothing else.
//
//       Holding the engine for the recorded windows alone is not enough,
//       and the measurement says so. A replayed dispatch records nothing,
//       but it advances the replay cursor one op at a time. With the guard
//       lifted under replay, the engine-driven window arrived on a worker
//       thread and reached no cursor, so the cursor stood still at index
//       286 of the 801-op region, holding aten::zero_ from that window,
//       while the op that arrived next was
//       aten::_foreach_mul_.Scalar from the optimizer. That is a schema
//       mismatch, the hard divergence tier. Over 12 iterations it cost 3
//       divergences on each accelerator shape and left the Vigil recording
//       rather than compiled. The published region was the whole 801 ops
//       either way. What was short was the stream that replayed it.
//
//       A session that does not arm that guard keeps the old behavior: the
//       recorded trace is forward plus optimizer, with the backward window
//       absent from it. That window is a gap in the trace, not a corruption
//       of it, because the op order that is recorded is still exactly the
//       order one thread produced.
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
CRUCIBLE_API void crucible_dispatch_backward_enter();
CRUCIBLE_API void crucible_dispatch_backward_exit();
CRUCIBLE_API uint32_t crucible_dispatch_backward_depth();
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

// ── The backward window ─────────────────────────────────────────────
//
// The wrapper CrucibleNative installs over Tensor.backward brackets each
// backward pass with these two calls, and record_kernel.h derives the BACKWARD
// phase from a non-zero depth. Lives in the dispatch lib — no PyTorch patch
// needed.
//
// The counter is owned here rather than in Python so that neither bound can be
// crossed from outside: `enter` saturates at the maximum instead of wrapping to
// zero and ending a window that is still open, and `exit` stops at zero instead
// of wrapping to the maximum and leaving every later operation labelled
// backward. Both bounds are unreachable from a matched bracket. They are here
// for the caller that is not matched, whose trace is then short of a phase bit
// rather than wrong about every one that follows.
//
// Not atomic: the counter is thread-local, one thread writes it, and that same
// thread is the only reader.

CRUCIBLE_API void crucible_dispatch_backward_enter() {
    if (backward_depth != UINT32_MAX) [[likely]]
        backward_depth++;
}

CRUCIBLE_API void crucible_dispatch_backward_exit() {
    if (backward_depth != 0) [[likely]]
        backward_depth--;
}

CRUCIBLE_API uint32_t crucible_dispatch_backward_depth() { return backward_depth; }

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
