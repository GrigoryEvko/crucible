// Crucible Vessel C API — implementation.
//
// Every C-ABI thunk crosses the FFI boundary through the typed helpers
// in vessel_api_typed.h:
//   * `vessel::as_vigil_typed(h)` materializes a
//     `Tagged<Vigil*, source::ABIBoundary>` from the opaque handle.
//   * `vessel::as_meta_typed(metas)` materializes a
//     `Tagged<const TensorMeta*, source::ABIBoundary>` from the
//     CrucibleMeta array (layout-compat reinterpret pinned by the
//     static_asserts in the typed-helper header).
// The Tagged value is unwrapped (`.value()`) just before the actual
// Vigil method invocation, so provenance is type-visible across the
// entire thunk while the runtime cost stays at zero (regime-1 EBO
// collapse on `Tagged<T, Tag>`).
//
// Review-enforced rule: no raw `static_cast<Vigil*>(h)` /
// `reinterpret_cast<TensorMeta*>(metas)` is permitted in this file —
// grep for `as_vigil_typed` / `as_meta_typed` to find every ABI-
// crossing site in O(1).  The CrucibleMeta ↔ TensorMeta layout
// invariants live in vessel_api_typed.h so the proof and the cast
// are co-located.

#include "vessel_api.h"
#include "vessel_api_typed.h"

#include <crucible/SchemaTable.h>
#include <crucible/TraceLoader.h>
#include <crucible/Vigil.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

// CrucibleDispatchResult is a small POD; size-pin is local to this
// file because the struct is only consumed at this boundary.
static_assert(sizeof(CrucibleDispatchResult) == 8);

// The handle check of vessel_api_typed.h reads the alignment of the Vigil
// from a constant, because that header only declares the class.  This file
// makes each Vigil, so the constant is checked here.
static_assert(alignof(crucible::Vigil) == crucible::vessel::detail::kVigilHandleAlignment,
              "vessel_api_typed.h: kVigilHandleAlignment must equal alignof(crucible::Vigil)");

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

// ── FFI entry validation ─────────────────────────────────────────────
//
// The checks and the retag that carries an Entry from the first trust
// tag to the second live in vessel_api_typed.h, beside the rest of the
// boundary's rules and shared with the two PyTorch adapters.  The two
// thunks below build an Entry, mint the first tag over it, and reach a
// recording entry point only through mint_validated_entry.
//
// ── Schema names ─────────────────────────────────────────────────────
//
// crucible_create() constructs a Vigil, whose constructor calls
// BackgroundThread::start(), whose first statement seals the global
// SchemaTable.  A name arrives when its operator first runs, which is after
// that, so crucible::register_schema_name puts it in the late table
// (crucible/SchemaTable.h).  The two tables are one pair for the process, so
// the names that libcrucible_dispatch.so registers while it records are here
// too.  crucible_export_crtrace() writes the union of the two tables, and
// crucible_schema_name() reads through both.

// ── C API implementation ─────────────────────────────────────────────

extern "C" {

uint64_t crucible_abi_version(void) noexcept {
    // ABI version stamp — mirrors the constant declared in
    // vessel_api.h.  Python `crucible_native.py` calls this at load
    // and refuses to construct the controller on mismatch.  Bump the
    // header constant when the C ABI surface changes; the Python side
    // ships the same constant in EXPECTED_ABI_VERSION.
    return CRUCIBLE_VESSEL_ABI_VERSION;
}

CrucibleHandle crucible_create(void) noexcept { return new crucible::Vigil(); }

void crucible_destroy(CrucibleHandle h) noexcept {
    auto handle = crucible::vessel::as_vigil_typed(h);
    delete handle.value();
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

uint64_t crucible_hash_shapes(const int64_t* all_sizes, const uint8_t* ndims, uint32_t n_tensors) noexcept {
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

CrucibleDispatchResult crucible_dispatch_op(CrucibleHandle handle, uint64_t schema_hash, uint64_t shape_hash,
                                            uint16_t num_inputs, uint16_t num_outputs, const CrucibleMeta* metas,
                                            uint32_t n_metas) noexcept {
    auto vigil_typed = crucible::vessel::as_vigil_typed(handle);

    // C→C++ boundary: wrap raw uint64_t into strong hash types.
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema_hash};
    entry.shape_hash = crucible::ShapeHash{shape_hash};
    entry.num_inputs = num_inputs;
    entry.num_outputs = num_outputs;

    // A count with no array behind it is a corrupt caller, and it is
    // refused here rather than one line further on. as_meta_typed states
    // the C ABI invariant that the pointer and the count travel together
    // and holds it with a debug assert, which ends the process. This is
    // the FFI, so the answer to a caller that broke the invariant is an
    // empty result in every build mode.
    if (metas == nullptr && n_metas > 0) return CrucibleDispatchResult{};

    // metas crosses the FFI as `CrucibleMeta*` and takes the typed-meta
    // helper for the layout-compatible view change and the provenance
    // tag.
    auto metas_typed = crucible::vessel::as_meta_typed(metas, n_metas);

    // The trust ladder.  Every field above came from a caller this
    // process does not control, so the Entry starts at the first tag and
    // reaches the second only by passing the checks.  An empty result
    // (action=RECORD, status=DIVERGED by default-init of uint8_t) tells
    // the Python side the operation was not recorded.
    auto validated =
        crucible::vessel::mint_validated_entry(crucible::mint_ffi_entry(entry), metas_typed.value(), n_metas);
    if (!validated) return CrucibleDispatchResult{};

    // dispatch_op_pure takes the context of the Vigil's producer claim.  The
    // mint claims the calling thread, or ends the process when another
    // thread holds the claim, which is the same gate that dispatch_op has.
    // This entry asks no is_producer_thread question, so that gate is its
    // only guard.
    crucible::Vigil* const vigil = vigil_typed.value();
    auto result = vigil->dispatch_op_pure(vigil->mint_producer_context(), *validated, metas_typed.value(), n_metas);

    CrucibleDispatchResult cr{};
    cr.action = static_cast<uint8_t>(result.action);
    cr.status = static_cast<uint8_t>(result.status);
    cr.op_index = result.op_index.raw();
    return cr;
}

CrucibleDispatchResult crucible_dispatch_op_ex(CrucibleHandle handle, uint64_t schema_hash, uint64_t shape_hash,
                                               uint16_t num_inputs, uint16_t num_outputs, const CrucibleMeta* metas,
                                               uint32_t n_metas, const int64_t* scalar_values, uint16_t num_scalars,
                                               uint8_t grad_enabled, uint8_t inference_mode) noexcept {
    auto vigil_typed = crucible::vessel::as_vigil_typed(handle);

    // C→C++ boundary: wrap raw uint64_t into strong hash types.
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema_hash};
    entry.shape_hash = crucible::ShapeHash{shape_hash};
    entry.num_inputs = num_inputs;
    entry.num_outputs = num_outputs;
    entry.num_scalar_args = num_scalars;
    uint8_t flags = 0;
    if (inference_mode != 0) flags |= crucible::op_flag::INFERENCE_MODE;
    if (grad_enabled != 0) flags |= crucible::op_flag::GRAD_ENABLED;
    entry.op_flags = flags;

    // The count above is what the caller claims; the inline array is the
    // room there is for it. Writing past the array would be the first
    // corruption, so the copy takes the smaller of the two and the
    // ladder below rejects the operation outright when the claim does
    // not fit.
    constexpr uint16_t inline_scalars = decltype(entry.scalar_values)::capacity;
    const uint16_t stored_scalars = num_scalars < inline_scalars ? num_scalars : inline_scalars;
    if (scalar_values) {
        for (uint16_t i = 0; i < stored_scalars; i++)
            entry.scalar_values[i] = scalar_values[i];
    }

    // A count with no array behind it is a corrupt caller, and it is
    // refused here rather than one line further on. as_meta_typed states
    // the C ABI invariant that the pointer and the count travel together
    // and holds it with a debug assert, which ends the process. This is
    // the FFI, so the answer to a caller that broke the invariant is an
    // empty result in every build mode.
    if (metas == nullptr && n_metas > 0) return CrucibleDispatchResult{};

    // metas crosses the FFI as `CrucibleMeta*` and takes the typed-meta
    // helper for the layout-compatible view change and the provenance
    // tag.
    auto metas_typed = crucible::vessel::as_meta_typed(metas, n_metas);

    // The trust ladder; see the sister site in crucible_dispatch_op.
    auto validated =
        crucible::vessel::mint_validated_entry(crucible::mint_ffi_entry(entry), metas_typed.value(), n_metas);
    if (!validated) return CrucibleDispatchResult{};

    // The producer claim gates this entry as it gates crucible_dispatch_op.
    crucible::Vigil* const vigil = vigil_typed.value();
    auto result = vigil->dispatch_op_pure(vigil->mint_producer_context(), *validated, metas_typed.value(), n_metas);

    CrucibleDispatchResult cr{};
    cr.action = static_cast<uint8_t>(result.action);
    cr.status = static_cast<uint8_t>(result.status);
    cr.op_index = result.op_index.raw();
    return cr;
}

void crucible_flush(CrucibleHandle h) noexcept { crucible::vessel::as_vigil_typed(h).value()->flush(); }

int crucible_is_compiled(CrucibleHandle h) noexcept {
    return crucible::vessel::as_vigil_typed(h).value()->is_compiled() ? 1 : 0;
}

uint32_t crucible_compiled_iterations(CrucibleHandle h) noexcept {
    return crucible::vessel::as_vigil_typed(h).value()->compiled_iterations();
}

uint32_t crucible_diverged_count(CrucibleHandle h) noexcept {
    return crucible::vessel::as_vigil_typed(h).value()->diverged_count();
}

uint32_t crucible_bg_iterations(CrucibleHandle h) noexcept {
    return crucible::vessel::as_vigil_typed(h).value()->bg_iterations_completed();
}

uint32_t crucible_ring_size(CrucibleHandle h) noexcept {
    // .peek(): unwrap the Stale<uint32_t> racy snapshot to the C-ABI scalar.
    return crucible::vessel::as_vigil_typed(h).value()->ring_size().peek();
}

uint32_t crucible_metalog_size(CrucibleHandle h) noexcept {
    // .peek(): unwrap the Stale<uint32_t> racy snapshot to the C-ABI scalar.
    return crucible::vessel::as_vigil_typed(h).value()->meta_log_size().peek();
}

// Each call proves that it runs on the thread that holds the producer claim.
// A call from any other thread ends the process, as a dispatch from one does.
void* crucible_output_ptr(CrucibleHandle h, uint16_t j) noexcept {
    auto* vigil = crucible::vessel::as_vigil_typed(h).value();
    return vigil->output_ptr(vigil->mint_producer_context(), j);
}

void* crucible_input_ptr(CrucibleHandle h, uint16_t j) noexcept {
    auto* vigil = crucible::vessel::as_vigil_typed(h).value();
    return vigil->input_ptr(vigil->mint_producer_context(), j);
}

void crucible_register_schema_name(CrucibleHandle h, uint64_t schema_hash, const char* name) noexcept {
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

    // The tables take names only from the thread that holds the producer
    // claim of this Vigil.  Minting the context proves that, and it ends
    // the process on any other thread.
    const crucible::VigilFgCtx fg = crucible::vessel::as_vigil_typed(h).value()->mint_producer_context();

    // The checks above are the boundary's sanitizer, and the retag records
    // that they ran.  The name is never dropped: a sealed global table sends
    // it to the late table.
    const auto name_tag =
        ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(name).retag<::fixy::tags::source::Sanitized>();
    crucible::register_schema_name(fg, crucible::SchemaHash{schema_hash}, name_tag);
}

const char* crucible_schema_name(uint64_t schema_hash) noexcept {
    // crucible::schema_name reads through both tables.  A caller cannot tell
    // which one accepted the registration, and should not have to.
    return crucible::schema_name(crucible::SchemaHash{schema_hash}).value().data();
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

    auto vigil_typed = crucible::vessel::as_vigil_typed(h);
    auto* vigil = vigil_typed.value();
    vigil->flush();

    const auto* region = vigil->active_region();
    if (!region || region->num_ops == 0) return 0;

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return 0;

    // Track write errors: if any fwrite fails (disk full, I/O error),
    // we still close the file but report failure.
    bool ok = true;
    auto w = [&](const void* ptr, size_t size, size_t count) {
        if (ok && std::fwrite(ptr, size, count, f) != count) ok = false;
    };

    // Count total tensor metas across all ops.
    uint32_t total_metas = 0;
    for (uint32_t i = 0; i < region->num_ops; i++) {
        total_metas += static_cast<uint32_t>(region->ops[i].num_inputs + region->ops[i].num_outputs);
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
        rec.schema_hash = te.schema_hash;
        rec.shape_hash = te.shape_hash;
        rec.scope_hash = te.scope_hash;
        rec.callsite_hash = te.callsite_hash;
        rec.num_inputs = te.num_inputs;
        rec.num_outputs = te.num_outputs;
        rec.num_scalars = te.num_scalar_args;
        rec.grad_enabled = te.grad_enabled ? 1 : 0;
        // Pack all op_flags back into one byte for on-disk format.
        uint8_t flags = 0;
        if (te.inference_mode) flags |= crucible::op_flag::INFERENCE_MODE;
        if (te.is_mutable) flags |= crucible::op_flag::IS_MUTABLE;
        flags |= (static_cast<uint8_t>(te.training_phase) & 0x3) << crucible::op_flag::PHASE_SHIFT;
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

    // Schema name table: the union of the two tables described above.
    // A hash registered before the seal and again after it appears in
    // both, so the late table's copy is skipped -- the reader keys by
    // hash and a second record for one hash is a malformed trace.
    const auto& table = crucible::global_schema_table();
    const auto& late = crucible::late_schema_table();

    const auto is_in_global = [&table](crucible::SchemaHash hash) noexcept {
        return table.lookup(hash).value().data() != nullptr;
    };

    // The count is written before the records, so it has to be the
    // post-dedup count, not the sum of the two sizes.
    uint32_t num_names = table.count();
    for (const crucible::SchemaEntry& entry : late.entries())
        if (!is_in_global(entry.hash)) num_names++;
    w(&num_names, 4, 1);

    const auto write_entry = [&w](const crucible::SchemaEntry& entry) {
        uint64_t sh = entry.hash.raw();
        auto name_len = static_cast<uint16_t>(entry.name_len);
        w(&sh, 8, 1);
        w(&name_len, 2, 1);
        w(entry.name, 1, name_len);
    };

    for (const crucible::SchemaEntry& entry : table.entries())
        write_entry(entry);
    for (const crucible::SchemaEntry& entry : late.entries())
        if (!is_in_global(entry.hash)) write_entry(entry);

    std::fclose(f);
    return ok ? 1 : 0;
}

uint32_t crucible_active_num_ops(CrucibleHandle h) noexcept {
    auto vigil_typed = crucible::vessel::as_vigil_typed(h);
    const auto* region = vigil_typed.value()->active_region();
    return region ? region->num_ops : 0;
}

}  // extern "C"
