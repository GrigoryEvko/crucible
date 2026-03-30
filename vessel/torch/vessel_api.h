// Crucible Vessel C API — ABI boundary for Python/ctypes.
//
// Wraps crucible::Vigil behind opaque handles and C-compatible structs.
// TorchDispatchMode (Python) calls these functions to feed real ATen ops
// into Crucible's recording and dispatch pipeline.
//
// CrucibleMeta is binary-compatible with crucible::TensorMeta (168 bytes).
// All enum types use the same int8_t ordinals as c10::ScalarType etc.

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

// ── Lifecycle ────────────────────────────────────────────────────────

// Create a Vigil instance. Starts the background thread.
CRUCIBLE_VESSEL_API CrucibleHandle crucible_create(void);

// Destroy a Vigil instance. Joins the background thread.
CRUCIBLE_VESSEL_API void crucible_destroy(CrucibleHandle h);

// ── Hashing ──────────────────────────────────────────────────────────

// FNV-1a 64-bit hash of a null-terminated string.
// Use for: op name → schema_hash.
CRUCIBLE_VESSEL_API uint64_t crucible_hash_string(const char* s);

// FNV-1a hash of tensor shapes. Concatenates (ndim, sizes[0..ndim-1])
// for each tensor. Use for: input shapes → shape_hash.
// all_sizes: flat array of sizes for all tensors concatenated.
// ndims: array of ndim values, one per tensor.
// n_tensors: number of tensors.
CRUCIBLE_VESSEL_API uint64_t crucible_hash_shapes(const int64_t* all_sizes,
                                                   const uint8_t* ndims,
                                                   uint32_t n_tensors);

// ── Dispatch ─────────────────────────────────────────────────────────

// Unified dispatch: records in RECORDING mode, checks guards in COMPILED.
// metas: array of CrucibleMeta, [inputs..., outputs...].
// n_metas: num_inputs + num_outputs.
CRUCIBLE_VESSEL_API CrucibleDispatchResult crucible_dispatch_op(
    CrucibleHandle h,
    uint64_t schema_hash, uint64_t shape_hash,
    uint16_t num_inputs, uint16_t num_outputs,
    const CrucibleMeta* metas, uint32_t n_metas);

// Extended dispatch with scalar arguments and grad/inference flags.
// scalar_values: up to 5 int64_t values (floats bitcast via memcpy).
// num_scalars: number of valid entries in scalar_values (0-5).
CRUCIBLE_VESSEL_API CrucibleDispatchResult crucible_dispatch_op_ex(
    CrucibleHandle h,
    uint64_t schema_hash, uint64_t shape_hash,
    uint16_t num_inputs, uint16_t num_outputs,
    const CrucibleMeta* metas, uint32_t n_metas,
    const int64_t* scalar_values, uint16_t num_scalars,
    uint8_t grad_enabled, uint8_t inference_mode);

// ── Control ──────────────────────────────────────────────────────────

// Spin-wait until TraceRing is drained (1s timeout).
CRUCIBLE_VESSEL_API void crucible_flush(CrucibleHandle h);

// Query mode: 1 if COMPILED, 0 if RECORDING/DIVERGED.
CRUCIBLE_VESSEL_API int crucible_is_compiled(CrucibleHandle h);

// Number of complete iterations in COMPILED mode.
CRUCIBLE_VESSEL_API uint32_t crucible_compiled_iterations(CrucibleHandle h);

// Number of divergences detected.
CRUCIBLE_VESSEL_API uint32_t crucible_diverged_count(CrucibleHandle h);

// ── Schema name registration ─────────────────────────────────────────

// Register an op name for a schema hash. Call once per op at startup.
// Enables human-readable labels in trace visualization and diagnostics.
CRUCIBLE_VESSEL_API void crucible_register_schema_name(uint64_t schema_hash,
                                                        const char* name);

// Lookup the registered name for a schema hash. Returns NULL if unknown.
CRUCIBLE_VESSEL_API const char* crucible_schema_name(uint64_t schema_hash);

// ── Diagnostics ──────────────────────────────────────────────────────

// Number of iteration boundaries detected by the background thread.
CRUCIBLE_VESSEL_API uint32_t crucible_bg_iterations(CrucibleHandle h);

// Number of entries currently in the ring buffer (approximate).
CRUCIBLE_VESSEL_API uint32_t crucible_ring_size(CrucibleHandle h);

// Number of entries in the MetaLog (approximate).
CRUCIBLE_VESSEL_API uint32_t crucible_metalog_size(CrucibleHandle h);

// ── Compiled mode accessors ──────────────────────────────────────────

// Pre-allocated output pointer for output j of the current op.
// Valid only after dispatch returned COMPILED with MATCH/COMPLETE.
CRUCIBLE_VESSEL_API void* crucible_output_ptr(CrucibleHandle h, uint16_t j);

// Pre-allocated input pointer for input j of the current op.
CRUCIBLE_VESSEL_API void* crucible_input_ptr(CrucibleHandle h, uint16_t j);

// ── Trace export ────────────────────────────────────────────────────

// Export the active compiled region as .crtrace binary.
// Flushes the ring first, then serializes the RegionNode's TraceEntry
// data to disk.  Format: 16B header + 80B/op + 168B/meta + schema table.
// Returns 1 on success, 0 on failure (no active region, file I/O error).
CRUCIBLE_VESSEL_API int crucible_export_crtrace(CrucibleHandle h, const char* path);

// Number of ops in the active compiled region.  0 if no region.
CRUCIBLE_VESSEL_API uint32_t crucible_active_num_ops(CrucibleHandle h);

#ifdef __cplusplus
}
#endif
