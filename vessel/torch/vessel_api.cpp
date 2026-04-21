// Crucible Vessel C API — implementation.
//
// Thin wrapper: each extern "C" function casts the opaque handle to
// crucible::Vigil* and forwards to the C++ method. CrucibleMeta is
// reinterpret_cast'd to TensorMeta (binary-compatible by construction).

#include "vessel_api.h"

#include <crucible/SchemaTable.h>
#include <crucible/TraceLoader.h>
#include <crucible/Vigil.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

// ── Layout compatibility verification ────────────────────────────────
//
// CrucibleMeta (C struct) must be identical to crucible::TensorMeta.
// If any of these fire, the C API is passing garbage to the C++ code.
static_assert(sizeof(CrucibleMeta) == sizeof(crucible::TensorMeta),
              "CrucibleMeta size must match TensorMeta");
static_assert(sizeof(CrucibleMeta) == 168);
static_assert(offsetof(CrucibleMeta, sizes) == 0);
static_assert(offsetof(CrucibleMeta, strides) == 64);
static_assert(offsetof(CrucibleMeta, data_ptr) == 128);
static_assert(offsetof(CrucibleMeta, ndim) == 136);
static_assert(offsetof(CrucibleMeta, dtype) == 137);
static_assert(offsetof(CrucibleMeta, device_type) == 138);
static_assert(offsetof(CrucibleMeta, device_idx) == 139);
static_assert(offsetof(CrucibleMeta, layout) == 140);
static_assert(offsetof(CrucibleMeta, requires_grad) == 141);
static_assert(offsetof(CrucibleMeta, flags) == 142);
static_assert(offsetof(CrucibleMeta, output_nr) == 143);
static_assert(offsetof(CrucibleMeta, storage_offset) == 144);
static_assert(offsetof(CrucibleMeta, version) == 152);
static_assert(offsetof(CrucibleMeta, storage_nbytes) == 156);
static_assert(offsetof(CrucibleMeta, grad_fn_hash) == 160);

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

// ── Handle validation ────────────────────────────────────────────────

static crucible::Vigil* as_vigil(CrucibleHandle h) {
    assert(h && "CrucibleHandle is null — did you call crucible_create()?");
    return static_cast<crucible::Vigil*>(h);
}

// ── FFI entry validation ─────────────────────────────────────────────
//
// The Vessel-boundary compose-3 pattern: raw uint64_t/uint16_t args
// from Python/PyTorch cross untrusted.  validate_ffi_entry checks the
// fields for structural soundness BEFORE any in-memory Entry is built
// or routed to Vigil.  On success it returns the Entry; the caller
// then vouches for it at the typed dispatch boundary.
//
// Rules, conservative by design — the FFI is the attack surface:
//   - schema_hash != 0 (0 is the invalid sentinel)
//   - num_inputs  ≤ 64  (TensorMeta array cap)
//   - num_outputs ≤ 64
//   - num_scalars ≤ 4096 (the 5-inline cap is separate)
//
// A real fleet of PyTorch ops never violates these.  The validator
// exists to reject corrupt FFI state before it reaches the hot path.
static bool validate_ffi_entry(uint64_t schema_hash,
                               uint16_t num_inputs,
                               uint16_t num_outputs,
                               uint16_t num_scalars) noexcept
{
    if (schema_hash == 0) return false;
    if (num_inputs  > 64) return false;
    if (num_outputs > 64) return false;
    if (num_scalars > 4096) return false;
    return true;
}

// Per-meta FFI validation.  The structural invariant of TensorMeta is
// that sizes[] and strides[] are length-8 fixed arrays; any ndim > 8
// from a corrupt FFI caller would cause out-of-bounds reads when the
// recording pipeline iterates per-dim.
//
// Cost: one uint8_t compare per meta on the dispatch path.  Typical
// op has 2-8 metas → <10 ns overhead; PyTorch dispatch itself is
// orders of magnitude larger, so the validation is free in the
// regime the FFI is actually used.
static bool validate_ffi_metas(const CrucibleMeta* metas, uint32_t n_metas) noexcept
{
    // n_metas == 0 requires metas == nullptr is OK (trivial op).
    if (n_metas == 0) return true;
    if (metas == nullptr) return false;
    for (uint32_t i = 0; i < n_metas; ++i) {
        if (metas[i].ndim > 8) return false;
    }
    return true;
}

// ── C API implementation ─────────────────────────────────────────────

extern "C" {

CrucibleHandle crucible_create(void) noexcept {
    return new crucible::Vigil();
}

void crucible_destroy(CrucibleHandle h) noexcept {
    delete static_cast<crucible::Vigil*>(h);
}

uint64_t crucible_hash_string(const char* s) noexcept {
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
                              uint32_t n_tensors) noexcept {
    if (n_tensors == 0 || !all_sizes || !ndims) return FNV_OFFSET;
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
    const CrucibleMeta* metas, uint32_t n_metas) noexcept
{
    auto* vigil = as_vigil(handle);

    // Validate FFI inputs before constructing an Entry — see
    // validate_ffi_entry / validate_ffi_metas for rules.  Failure
    // returns an empty result (action=RECORD, status=DIVERGED by
    // default-init of uint8_t) so the Python side can see it didn't
    // take effect.
    if (!validate_ffi_entry(schema_hash, num_inputs, num_outputs, 0)) {
        return CrucibleDispatchResult{};
    }
    if (!validate_ffi_metas(metas, n_metas)) {
        return CrucibleDispatchResult{};
    }

    // C→C++ boundary: wrap raw uint64_t into strong hash types.
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema_hash};
    entry.shape_hash = crucible::ShapeHash{shape_hash};
    entry.num_inputs = num_inputs;
    entry.num_outputs = num_outputs;

    // Entry fields are now validated; vouch at the typed dispatch boundary.
    auto result = vigil->dispatch_op(
        crucible::vouch(entry),
        reinterpret_cast<const crucible::TensorMeta*>(metas),
        n_metas);

    CrucibleDispatchResult cr{};
    cr.action = static_cast<uint8_t>(result.action);
    cr.status = static_cast<uint8_t>(result.status);
    cr.op_index = result.op_index.raw();
    return cr;
}

CrucibleDispatchResult crucible_dispatch_op_ex(
    CrucibleHandle handle,
    uint64_t schema_hash, uint64_t shape_hash,
    uint16_t num_inputs, uint16_t num_outputs,
    const CrucibleMeta* metas, uint32_t n_metas,
    const int64_t* scalar_values, uint16_t num_scalars,
    uint8_t grad_enabled, uint8_t inference_mode) noexcept
{
    auto* vigil = as_vigil(handle);

    // Validate FFI inputs before constructing an Entry.
    if (!validate_ffi_entry(schema_hash, num_inputs, num_outputs, num_scalars)) {
        return CrucibleDispatchResult{};
    }
    if (!validate_ffi_metas(metas, n_metas)) {
        return CrucibleDispatchResult{};
    }

    // C→C++ boundary: wrap raw uint64_t into strong hash types.
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema_hash};
    entry.shape_hash = crucible::ShapeHash{shape_hash};
    entry.num_inputs = num_inputs;
    entry.num_outputs = num_outputs;
    entry.num_scalar_args = num_scalars;
    entry.set_grad_enabled(grad_enabled != 0);
    // C API doesn't set op_flags bits beyond inference_mode.
    // Native C++ fallback (crucible_fallback.cpp) sets all 5 bits directly.
    entry.op_flags = inference_mode != 0 ? crucible::op_flag::INFERENCE_MODE : 0;

    uint16_t n = num_scalars < 5 ? num_scalars : 5;
    if (scalar_values) {
        for (uint16_t i = 0; i < n; i++)
            entry.scalar_values[i] = scalar_values[i];
    }

    // Entry fields validated above; vouch at the typed dispatch boundary.
    auto result = vigil->dispatch_op(
        crucible::vouch(entry),
        reinterpret_cast<const crucible::TensorMeta*>(metas),
        n_metas);

    CrucibleDispatchResult cr{};
    cr.action = static_cast<uint8_t>(result.action);
    cr.status = static_cast<uint8_t>(result.status);
    cr.op_index = result.op_index.raw();
    return cr;
}

void crucible_flush(CrucibleHandle h) noexcept {
    as_vigil(h)->flush();
}

int crucible_is_compiled(CrucibleHandle h) noexcept {
    return as_vigil(h)->is_compiled() ? 1 : 0;
}

uint32_t crucible_compiled_iterations(CrucibleHandle h) noexcept {
    return as_vigil(h)->compiled_iterations();
}

uint32_t crucible_diverged_count(CrucibleHandle h) noexcept {
    return as_vigil(h)->diverged_count();
}

uint32_t crucible_bg_iterations(CrucibleHandle h) noexcept {
    return as_vigil(h)->bg_iterations_completed();
}

uint32_t crucible_ring_size(CrucibleHandle h) noexcept {
    return as_vigil(h)->ring().size();
}

uint32_t crucible_metalog_size(CrucibleHandle h) noexcept {
    return as_vigil(h)->meta_log().size();
}

void* crucible_output_ptr(CrucibleHandle h, uint16_t j) noexcept {
    return as_vigil(h)->output_ptr(j);
}

void* crucible_input_ptr(CrucibleHandle h, uint16_t j) noexcept {
    return as_vigil(h)->input_ptr(j);
}

void crucible_register_schema_name(uint64_t schema_hash, const char* name) noexcept {
    // C ABI boundary: validate before routing to SanitizedName.
    // Rules:
    //   - schema_hash != 0 (0 is the invalid sentinel)
    //   - name != null
    //   - strnlen(name, MAX_NAME+1) ≤ MAX_NAME
    //     (strnlen caps the walk so a missing NUL-terminator from a
    //     malformed FFI caller can't walk off into foreign memory)
    // ATen op names in PyTorch all fit in <= 128 bytes; the 256 cap
    // leaves headroom while still bounding the worst-case walk.
    constexpr size_t MAX_NAME = 256;
    if (schema_hash == 0) return;
    if (name == nullptr) return;
    const size_t len = ::strnlen(name, MAX_NAME + 1);
    if (len == 0 || len > MAX_NAME) return;
    crucible::register_schema_name(crucible::SchemaHash{schema_hash},
        crucible::SchemaTable::SanitizedName{name});
}

const char* crucible_schema_name(uint64_t schema_hash) noexcept {
    return crucible::schema_name(crucible::SchemaHash{schema_hash});
}

int crucible_export_crtrace(CrucibleHandle h, const char* path) noexcept {
    // FFI boundary: path validation before any fs::* / fopen call.
    //   - path != null
    //   - strnlen(path, PATH_MAX+1) ≤ PATH_MAX
    // A missing NUL-terminator from a malformed caller could otherwise
    // walk into foreign memory; PATH_MAX=4096 is Linux's filesystem
    // component-path ceiling.  Cap the walk to bound the failure mode.
    constexpr size_t MAX_PATH_LEN = 4096;
    if (path == nullptr) return 0;
    const size_t path_len = ::strnlen(path, MAX_PATH_LEN + 1);
    if (path_len == 0 || path_len > MAX_PATH_LEN) return 0;

    auto* vigil = as_vigil(h);
    vigil->flush();

    const auto* region = vigil->active_region();
    if (!region || region->num_ops == 0) return 0;

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return 0;

    // Track write errors: if any fwrite fails (disk full, I/O error),
    // we still close the file but report failure.
    bool ok = true;
    auto w = [&](const void* ptr, size_t size, size_t count) {
        if (ok && std::fwrite(ptr, size, count, f) != count)
            ok = false;
    };

    // Count total tensor metas across all ops.
    uint32_t total_metas = 0;
    for (uint32_t i = 0; i < region->num_ops; i++) {
        total_metas += static_cast<uint32_t>(
            region->ops[i].num_inputs + region->ops[i].num_outputs);
    }

    // Header: "CRTR" + version(1) + num_ops + num_metas = 16B.
    w("CRTR", 1, 4);
    uint32_t version = 1;
    w(&version, 4, 1);
    w(&region->num_ops, 4, 1);
    w(&total_metas, 4, 1);

    // Op records: 80B each (TraceOpRecord layout).
    for (uint32_t i = 0; i < region->num_ops; i++) {
        const auto& te = region->ops[i];
        crucible::TraceOpRecord rec{};
        rec.schema_hash   = te.schema_hash;
        rec.shape_hash    = te.shape_hash;
        rec.scope_hash    = te.scope_hash;
        rec.callsite_hash = te.callsite_hash;
        rec.num_inputs = te.num_inputs;
        rec.num_outputs = te.num_outputs;
        rec.num_scalars = te.num_scalar_args;
        rec.grad_enabled = te.grad_enabled ? 1 : 0;
        // Pack all op_flags back into one byte for on-disk format.
        uint8_t flags = 0;
        if (te.inference_mode) flags |= crucible::op_flag::INFERENCE_MODE;
        if (te.is_mutable)     flags |= crucible::op_flag::IS_MUTABLE;
        flags |= (static_cast<uint8_t>(te.training_phase) & 0x3)
                 << crucible::op_flag::PHASE_SHIFT;
        if (te.torch_function) flags |= crucible::op_flag::TORCH_FUNCTION;
        rec.inference_mode = flags;
        const uint16_t ns = te.num_scalar_args < 5 ? te.num_scalar_args : 5;
        for (uint16_t s = 0; s < ns; s++)
            rec.scalar_values[s] = te.scalar_args ? te.scalar_args[s] : 0;
        w(&rec, sizeof(rec), 1);
    }

    // Meta records: 168B each (TensorMeta layout).
    for (uint32_t i = 0; i < region->num_ops; i++) {
        const auto& te = region->ops[i];
        for (const auto& m : te.input_span())
            w(&m, sizeof(crucible::TensorMeta), 1);
        for (const auto& m : te.output_span())
            w(&m, sizeof(crucible::TensorMeta), 1);
    }

    // Schema name table.
    const auto& table = crucible::global_schema_table();
    uint32_t num_names = table.count();
    w(&num_names, 4, 1);
    for (uint32_t i = 0; i < num_names; i++) {
        uint64_t sh = table.entries[i].hash.raw();
        const char* name = table.entries[i].name;
        auto name_len = static_cast<uint16_t>(std::strlen(name));
        w(&sh, 8, 1);
        w(&name_len, 2, 1);
        w(name, 1, name_len);
    }

    std::fclose(f);
    return ok ? 1 : 0;
}

uint32_t crucible_active_num_ops(CrucibleHandle h) noexcept {
    auto* vigil = as_vigil(h);
    const auto* region = vigil->active_region();
    return region ? region->num_ops : 0;
}

} // extern "C"
