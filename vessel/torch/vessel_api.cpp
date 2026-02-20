// Crucible Vessel C API — implementation.
//
// Thin wrapper: each extern "C" function casts the opaque handle to
// crucible::Vigil* and forwards to the C++ method. CrucibleMeta is
// reinterpret_cast'd to TensorMeta (binary-compatible by construction).

#include "vessel_api.h"

#include <crucible/Vigil.h>

#include <cstddef>

// ── Layout compatibility verification ────────────────────────────────
//
// CrucibleMeta (C struct) must be identical to crucible::TensorMeta.
// If any of these fire, the C API is passing garbage to the C++ code.
static_assert(sizeof(CrucibleMeta) == sizeof(crucible::TensorMeta),
              "CrucibleMeta size must match TensorMeta");
static_assert(sizeof(CrucibleMeta) == 144);
static_assert(offsetof(CrucibleMeta, sizes) == 0);
static_assert(offsetof(CrucibleMeta, strides) == 64);
static_assert(offsetof(CrucibleMeta, data_ptr) == 128);
static_assert(offsetof(CrucibleMeta, ndim) == 136);
static_assert(offsetof(CrucibleMeta, dtype) == 137);
static_assert(offsetof(CrucibleMeta, device_type) == 138);
static_assert(offsetof(CrucibleMeta, device_idx) == 139);
static_assert(offsetof(CrucibleMeta, layout) == 140);

static_assert(sizeof(CrucibleDispatchResult) == 8);

// ── FNV-1a 64-bit ────────────────────────────────────────────────────

static uint64_t fnv1a_bytes(const void* data, size_t len, uint64_t h) {
    auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;

// ── C API implementation ─────────────────────────────────────────────

extern "C" {

CrucibleHandle crucible_create(void) {
    return new crucible::Vigil();
}

void crucible_destroy(CrucibleHandle h) {
    delete static_cast<crucible::Vigil*>(h);
}

uint64_t crucible_hash_string(const char* s) {
    if (!s) return 0;
    uint64_t h = FNV_OFFSET;
    while (*s) {
        h ^= static_cast<uint8_t>(*s++);
        h *= 0x100000001b3ULL;
    }
    return h;
}

uint64_t crucible_hash_shapes(const int64_t* all_sizes,
                              const uint8_t* ndims,
                              uint32_t n_tensors) {
    uint64_t h = FNV_OFFSET;
    uint32_t offset = 0;
    for (uint32_t t = 0; t < n_tensors; t++) {
        // Hash ndim as a separator between tensors
        h = fnv1a_bytes(&ndims[t], 1, h);
        // Hash sizes[0..ndim-1]
        h = fnv1a_bytes(&all_sizes[offset], ndims[t] * sizeof(int64_t), h);
        offset += ndims[t];
    }
    return h;
}

CrucibleDispatchResult crucible_dispatch_op(
    CrucibleHandle handle,
    uint64_t schema_hash, uint64_t shape_hash,
    uint16_t num_inputs, uint16_t num_outputs,
    const CrucibleMeta* metas, uint32_t n_metas)
{
    auto* vigil = static_cast<crucible::Vigil*>(handle);

    crucible::TraceRing::Entry entry{};
    entry.schema_hash = schema_hash;
    entry.shape_hash = shape_hash;
    entry.num_inputs = num_inputs;
    entry.num_outputs = num_outputs;

    auto result = vigil->dispatch_op(
        entry,
        reinterpret_cast<const crucible::TensorMeta*>(metas),
        n_metas);

    CrucibleDispatchResult cr{};
    cr.action = static_cast<uint8_t>(result.action);
    cr.status = static_cast<uint8_t>(result.status);
    cr.op_index = result.op_index;
    return cr;
}

void crucible_flush(CrucibleHandle h) {
    static_cast<crucible::Vigil*>(h)->flush();
}

int crucible_is_compiled(CrucibleHandle h) {
    return static_cast<crucible::Vigil*>(h)->is_compiled() ? 1 : 0;
}

uint32_t crucible_compiled_iterations(CrucibleHandle h) {
    return static_cast<crucible::Vigil*>(h)->compiled_iterations();
}

uint32_t crucible_diverged_count(CrucibleHandle h) {
    return static_cast<crucible::Vigil*>(h)->diverged_count();
}

void* crucible_output_ptr(CrucibleHandle h, uint16_t j) {
    return static_cast<crucible::Vigil*>(h)->output_ptr(j);
}

void* crucible_input_ptr(CrucibleHandle h, uint16_t j) {
    return static_cast<crucible::Vigil*>(h)->input_ptr(j);
}

} // extern "C"
