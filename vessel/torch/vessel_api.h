// Crucible Vessel C API — ABI boundary for Python/ctypes.
//
// Wraps crucible::Vigil behind opaque handles and C-compatible structs.
// TorchDispatchMode (Python) calls these functions to feed real ATen ops
// into Crucible's recording and dispatch pipeline.
//
// CrucibleMeta is binary-compatible with crucible::TensorMeta (168 bytes).
// All enum types use the same int8_t ordinals as c10::ScalarType etc.
//
// ── Typed-boundary discipline (GAPS-094..096) ────────────────────────
//
// The C ABI fields below remain raw (`void*`, `const char*`) because
// ctypes consumers cannot interpret C++ wrapper types.  Inside C++,
// every value crossing this boundary is immediately re-typed via the
// helpers in vessel_api_typed.h:
//
//   * CrucibleHandle          → Tagged<Vigil*, source::ABIBoundary>
//                               via `as_vigil_typed(h)` (GAPS-094).
//   * const CrucibleMeta*     → Tagged<const TensorMeta*, ABIBoundary>
//                               via `as_meta_typed(metas, n)` (GAPS-095).
//   * CrucibleMeta::data_ptr  → Tagged<void*, source::External>
//                               via `data_ptr_typed(typed_meta, i)`
//                               (GAPS-096) — provenance threads into
//                               TensorMeta-consuming code without
//                               changing the wire-struct layout.
//   * schema name lookup      → SchemaTable::LookupName
//                               via `schema_name_typed(schema_hash)`
//                               (GAPS-096) — distinguishes the
//                               post-validation pointer from raw FFI
//                               input.
//
// ── ABI version stamp ────────────────────────────────────────────────
//
// `crucible_abi_version()` returns a uint64_t that identifies the wire
// layout of CrucibleMeta + CrucibleDispatchResult + the function
// signatures below.  Python loaders compare the value against the
// constant baked into crucible_native.py at startup; mismatched .so
// vs .py refuses to construct the controller.  Bump the constant when
// adding/removing functions or changing struct layout.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Symbol visibility — must be visible for ctypes/dlsym.
#ifdef _MSC_VER
  #define CRUCIBLE_VESSEL_API __declspec(dllexport)
#else
  #define CRUCIBLE_VESSEL_API __attribute__((visibility("default")))
#endif

// Exception transparency for the C ABI boundary.  Every extern "C"
// function is noexcept: throwing across a C ABI is UB, and the project
// builds with -fno-exceptions anyway, so the annotation is both
// documentation and a guarantee that GCC elides any unwind machinery
// for these thunks.  The macro collapses to empty under a C compiler
// because `noexcept` is C++-only syntax.
#ifdef __cplusplus
  #define CRUCIBLE_VESSEL_NOEXCEPT noexcept
#else
  #define CRUCIBLE_VESSEL_NOEXCEPT
#endif

// Opaque handle to a crucible::Vigil instance.
typedef void* CrucibleHandle;

// Binary-compatible with crucible::TensorMeta (168 bytes).
// Field order and types must match exactly — verified via static_assert
// in the implementation.
typedef struct {
    int64_t sizes[8];
    int64_t strides[8];
    void* data_ptr;
    uint8_t ndim;
    int8_t dtype;        // crucible::ScalarType ordinal
    int8_t device_type;  // crucible::DeviceType ordinal
    int8_t device_idx;
    int8_t layout;       // crucible::Layout ordinal
    uint8_t requires_grad; // tensor.requires_grad
    uint8_t flags;       // packed: is_leaf|is_contiguous|has_grad_fn|is_view|is_neg|is_conj
    uint8_t output_nr;   // autograd output number (which output of multi-output op)
    int64_t storage_offset; // offset into underlying storage
    uint32_t version;    // tensor data version counter
    uint32_t storage_nbytes; // actual storage size
    uint64_t grad_fn_hash; // FNV-1a hash of grad_fn class name
} CrucibleMeta;

// Result from crucible_dispatch_op (8 bytes).
typedef struct {
    uint8_t action;     // 0=RECORD, 1=COMPILED
    uint8_t status;     // 0=MATCH, 1=DIVERGED, 2=COMPLETE
    uint8_t pad[2];
    uint32_t op_index;
} CrucibleDispatchResult;

// ── ABI version (GAPS-096) ───────────────────────────────────────────

// CRUCIBLE_VESSEL_ABI_VERSION — identifies the wire layout of every
// extern "C" symbol declared in this header AND the byte layout of
// CrucibleMeta / CrucibleDispatchResult.  Increment on any of:
//   * Adding / removing / renaming a function.
//   * Changing argument types or counts.
//   * Re-laying-out CrucibleMeta or CrucibleDispatchResult.
// Python ctypes consumers cross-check via crucible_abi_version() at
// load.  The constant is also exposed as a #define so C consumers can
// bake it into their own startup checks.
#define CRUCIBLE_VESSEL_ABI_VERSION 1ULL

// Returns CRUCIBLE_VESSEL_ABI_VERSION as compiled into the loaded .so.
// Never aborts — pure constant return.  Used by crucible_native.py
// __init__ to guard against .so/.py drift.
CRUCIBLE_VESSEL_API uint64_t crucible_abi_version(void) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Lifecycle ────────────────────────────────────────────────────────

// Create a Vigil instance. Starts the background thread.
CRUCIBLE_VESSEL_API CrucibleHandle crucible_create(void) CRUCIBLE_VESSEL_NOEXCEPT;

// Destroy a Vigil instance. Joins the background thread.
CRUCIBLE_VESSEL_API void crucible_destroy(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Hashing ──────────────────────────────────────────────────────────

// FNV-1a 64-bit hash of a null-terminated string.
// Use for: op name → schema_hash.
CRUCIBLE_VESSEL_API uint64_t crucible_hash_string(const char* s) CRUCIBLE_VESSEL_NOEXCEPT;

// FNV-1a hash of tensor shapes. Concatenates (ndim, sizes[0..ndim-1])
// for each tensor. Use for: input shapes → shape_hash.
// all_sizes: flat array of sizes for all tensors concatenated.
// ndims: array of ndim values, one per tensor.
// n_tensors: number of tensors.
CRUCIBLE_VESSEL_API uint64_t crucible_hash_shapes(const int64_t* all_sizes,
                                                   const uint8_t* ndims,
                                                   uint32_t n_tensors) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Dispatch ─────────────────────────────────────────────────────────

// Unified dispatch: records in RECORDING mode, checks guards in COMPILED.
// metas: array of CrucibleMeta, [inputs..., outputs...].
// n_metas: num_inputs + num_outputs.
CRUCIBLE_VESSEL_API CrucibleDispatchResult crucible_dispatch_op(
    CrucibleHandle h,
    uint64_t schema_hash, uint64_t shape_hash,
    uint16_t num_inputs, uint16_t num_outputs,
    const CrucibleMeta* metas, uint32_t n_metas) CRUCIBLE_VESSEL_NOEXCEPT;

// Extended dispatch with scalar arguments and grad/inference flags.
// scalar_values: up to 5 int64_t values (floats bitcast via memcpy).
// num_scalars: number of valid entries in scalar_values (0-5).
CRUCIBLE_VESSEL_API CrucibleDispatchResult crucible_dispatch_op_ex(
    CrucibleHandle h,
    uint64_t schema_hash, uint64_t shape_hash,
    uint16_t num_inputs, uint16_t num_outputs,
    const CrucibleMeta* metas, uint32_t n_metas,
    const int64_t* scalar_values, uint16_t num_scalars,
    uint8_t grad_enabled, uint8_t inference_mode) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Control ──────────────────────────────────────────────────────────

// Spin-wait until TraceRing is drained (1s timeout).
CRUCIBLE_VESSEL_API void crucible_flush(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

// Query mode: 1 if COMPILED, 0 if RECORDING/DIVERGED.
CRUCIBLE_VESSEL_API int crucible_is_compiled(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

// Number of complete iterations in COMPILED mode.
CRUCIBLE_VESSEL_API uint32_t crucible_compiled_iterations(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

// Number of divergences detected.
CRUCIBLE_VESSEL_API uint32_t crucible_diverged_count(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Schema name registration ─────────────────────────────────────────

// Register an op name for a schema hash. Call once per op at startup.
// Enables human-readable labels in trace visualization and diagnostics.
CRUCIBLE_VESSEL_API void crucible_register_schema_name(uint64_t schema_hash,
                                                        const char* name) CRUCIBLE_VESSEL_NOEXCEPT;

// Lookup the registered name for a schema hash. Returns NULL if unknown.
CRUCIBLE_VESSEL_API const char* crucible_schema_name(uint64_t schema_hash) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Diagnostics ──────────────────────────────────────────────────────

// Number of iteration boundaries detected by the background thread.
CRUCIBLE_VESSEL_API uint32_t crucible_bg_iterations(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

// Number of entries currently in the ring buffer (approximate).
CRUCIBLE_VESSEL_API uint32_t crucible_ring_size(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

// Number of entries in the MetaLog (approximate).
CRUCIBLE_VESSEL_API uint32_t crucible_metalog_size(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Compiled mode accessors ──────────────────────────────────────────

// Pre-allocated output pointer for output j of the current op.
// Valid only after dispatch returned COMPILED with MATCH/COMPLETE.
CRUCIBLE_VESSEL_API void* crucible_output_ptr(CrucibleHandle h, uint16_t j) CRUCIBLE_VESSEL_NOEXCEPT;

// Pre-allocated input pointer for input j of the current op.
CRUCIBLE_VESSEL_API void* crucible_input_ptr(CrucibleHandle h, uint16_t j) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Trace export ────────────────────────────────────────────────────

// Export the active compiled region as .crtrace binary.
// Flushes the ring first, then serializes the RegionNode's TraceEntry
// data to disk.  Format: 16B header + 80B/op + 168B/meta + schema table.
// Returns 1 on success, 0 on failure (no active region, file I/O error).
CRUCIBLE_VESSEL_API int crucible_export_crtrace(CrucibleHandle h, const char* path) CRUCIBLE_VESSEL_NOEXCEPT;

// Number of ops in the active compiled region.  0 if no region.
CRUCIBLE_VESSEL_API uint32_t crucible_active_num_ops(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

#ifdef __cplusplus
}
#endif
