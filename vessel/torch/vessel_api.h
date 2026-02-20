// Crucible Vessel C API — ABI boundary for Python/ctypes.
//
// Wraps crucible::Vigil behind opaque handles and C-compatible structs.
// TorchDispatchMode (Python) calls these functions to feed real ATen ops
// into Crucible's recording and dispatch pipeline.
//
// CrucibleMeta is binary-compatible with crucible::TensorMeta (144 bytes).
// All enum types use the same int8_t ordinals as c10::ScalarType etc.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Opaque handle to a crucible::Vigil instance.
typedef void* CrucibleHandle;

// Binary-compatible with crucible::TensorMeta (144 bytes).
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
    uint8_t pad[3];
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
CrucibleHandle crucible_create(void);

// Destroy a Vigil instance. Joins the background thread.
void crucible_destroy(CrucibleHandle h);

// ── Hashing ──────────────────────────────────────────────────────────

// FNV-1a 64-bit hash of a null-terminated string.
// Use for: op name → schema_hash.
uint64_t crucible_hash_string(const char* s);

// FNV-1a hash of tensor shapes. Concatenates (ndim, sizes[0..ndim-1])
// for each tensor. Use for: input shapes → shape_hash.
// all_sizes: flat array of sizes for all tensors concatenated.
// ndims: array of ndim values, one per tensor.
// n_tensors: number of tensors.
uint64_t crucible_hash_shapes(const int64_t* all_sizes,
                              const uint8_t* ndims,
                              uint32_t n_tensors);

// ── Dispatch ─────────────────────────────────────────────────────────

// Unified dispatch: records in RECORDING mode, checks guards in COMPILED.
// metas: array of CrucibleMeta, [inputs..., outputs...].
// n_metas: num_inputs + num_outputs.
CrucibleDispatchResult crucible_dispatch_op(
    CrucibleHandle h,
    uint64_t schema_hash, uint64_t shape_hash,
    uint16_t num_inputs, uint16_t num_outputs,
    const CrucibleMeta* metas, uint32_t n_metas);

// Extended dispatch with scalar arguments and grad/inference flags.
// scalar_values: up to 5 int64_t values (floats bitcast via memcpy).
// num_scalars: number of valid entries in scalar_values (0-5).
CrucibleDispatchResult crucible_dispatch_op_ex(
    CrucibleHandle h,
    uint64_t schema_hash, uint64_t shape_hash,
    uint16_t num_inputs, uint16_t num_outputs,
    const CrucibleMeta* metas, uint32_t n_metas,
    const int64_t* scalar_values, uint16_t num_scalars,
    uint8_t grad_enabled, uint8_t inference_mode);

// ── Control ──────────────────────────────────────────────────────────

// Spin-wait until TraceRing is drained (1s timeout).
void crucible_flush(CrucibleHandle h);

// Query mode: 1 if COMPILED, 0 if RECORDING/DIVERGED.
int crucible_is_compiled(CrucibleHandle h);

// Number of complete iterations in COMPILED mode.
uint32_t crucible_compiled_iterations(CrucibleHandle h);

// Number of divergences detected.
uint32_t crucible_diverged_count(CrucibleHandle h);

// ── Diagnostics ──────────────────────────────────────────────────────

// Number of iteration boundaries detected by the background thread.
uint32_t crucible_bg_iterations(CrucibleHandle h);

// Number of entries currently in the ring buffer (approximate).
uint32_t crucible_ring_size(CrucibleHandle h);

// Number of entries in the MetaLog (approximate).
uint32_t crucible_metalog_size(CrucibleHandle h);

// ── Compiled mode accessors ──────────────────────────────────────────

// Pre-allocated output pointer for output j of the current op.
// Valid only after dispatch returned COMPILED with MATCH/COMPLETE.
void* crucible_output_ptr(CrucibleHandle h, uint16_t j);

// Pre-allocated input pointer for input j of the current op.
void* crucible_input_ptr(CrucibleHandle h, uint16_t j);

#ifdef __cplusplus
}
#endif
