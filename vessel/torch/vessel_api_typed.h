#pragma once

// Typed helpers for the internal Vessel C-ABI handle boundary.
//
// CrucibleHandle remains `void*` and CrucibleMeta remains a plain C
// struct in vessel_api.h because both are exported to C / ctypes
// callers.  Inside C++, every value crossing the FFI boundary is
// immediately tagged with the `source::ABIBoundary` provenance tag so
// foreign-runtime provenance is visible in the type system before the pointer
// reaches Vigil / TensorMeta-consuming code.
//
// ── Helper surface ─────────────────────────────────────────────────
//
//   TypedHandle               := Tagged<Vigil*, ABIBoundary>
//   as_vigil_typed(handle)    : CrucibleHandle           -> TypedHandle
//   from_typed(typed)         : TypedHandle              -> CrucibleHandle
//
//   TypedMeta                 := Tagged<const CrucibleMeta*, ABIBoundary>
//   as_meta_typed(metas)      : const CrucibleMeta*      -> TypedMeta
//   metas_from_typed(typed)   : TypedMeta                -> const CrucibleMeta*
//
//   data_ptr_typed(typed, i)  : TypedMeta, std::size_t   -> ExternalDataPtr
//   decode_ffi_meta(record)   : const CrucibleMeta&      -> Option<TensorMeta>
//   decode_ffi_records(...)   : TypedMeta, count, list   -> bool
//
//   TypedSchemaName           := SchemaTable::LookupName
//   schema_name_typed(hash)   : SchemaHash               -> TypedSchemaName
//
//   decode_c10_scalar_type(o) : int8_t                   -> Option<ScalarType>
//   decode_c10_device_type(o) : int8_t                   -> Option<DeviceType>
//   decode_c10_layout(o)      : int8_t                   -> Option<Layout>
//
// ── ABI invariant ──────────────────────────────────────────────────
//
// The C caller writes CrucibleMeta records, and the vessel reads each
// record through CrucibleMeta.  decode_ffi_meta reads each field of a record
// and gives the fields to decode_tensor_meta of crucible/TensorMeta.h, the
// one checked door to a TensorMeta.  A read of a CrucibleMeta record
// through a glvalue of TensorMeta breaks the rule of strict aliasing.
// GCC at -O3 then removes the stores of the record and reads bytes that
// the caller did not write.  So no code casts between the two structs.
//
// The static_asserts below keep the layout of CrucibleMeta equal to the
// layout of TensorMeta, byte for byte.  The trace file of crucible_mode.py
// holds its records in that one layout.  The decode does not need it.
//
// ── Zero-cost guarantee ────────────────────────────────────────────
//
// `Tagged<T, Tag>` is regime-1 EBO collapse via TrustLattice<Tag>'s
// empty element_type, so `sizeof(TypedHandle) == sizeof(void*)` and
// `sizeof(TypedMeta) == sizeof(void*)` are static_assert'd below.
// The helpers that give a type to a pointer are CRUCIBLE_HOT
// (always-inline) and compile to the same machine code as the bare
// pointer after EBO collapse.
//
// ── Usage rule (review-enforced) ───────────────────────────────────
//
// Every C-ABI thunk in vessel_api.cpp begins with `as_vigil_typed(h)`
// (and `as_meta_typed(metas)` if it accepts a meta array).  No raw
// `static_cast<Vigil*>(h)` is permitted outside this header.  Grepping
// for `as_vigil_typed` or `as_meta_typed` finds every ABI-crossing site
// in O(1).

#include "vessel_api.h"

#include <crucible/MerkleDag.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <crucible/ValidatedOp.h>
#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/contracts/Pre.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

// The helpers below name the Vigil only through a pointer, so a declaration
// is sufficient, and this header does not include crucible/Vigil.h.
namespace crucible {
class Vigil;
}  // namespace crucible

namespace crucible::vessel {

// ── Vigil typed handle ────────────────────────────────────────────
//
// ABIBoundary names where a value came from, so mint_tagged builds a
// value under it.  A check that the value passed reaches Sanitized only
// by retag along the catalog edge.  The data pointer of a meta is the
// ExternalDataPtr of TensorMeta, another ::fixy Tagged.

using TypedHandle = ::fixy::Tagged<Vigil*, ::fixy::tags::source::ABIBoundary>;

static_assert(sizeof(TypedHandle) == sizeof(CrucibleHandle));
static_assert(alignof(TypedHandle) == alignof(CrucibleHandle));
static_assert(std::is_trivially_copy_constructible_v<TypedHandle>);

// ── The typed records of a C caller ───────────────────────────────
//
// The records keep their C type.  decode_ffi_meta below makes a TensorMeta
// from one record.

using TypedMeta = ::fixy::Tagged<const CrucibleMeta*, ::fixy::tags::source::ABIBoundary>;

static_assert(sizeof(TypedMeta) == sizeof(const CrucibleMeta*));
static_assert(alignof(TypedMeta) == alignof(const CrucibleMeta*));
static_assert(std::is_trivially_copy_constructible_v<TypedMeta>);

// ── The layout of CrucibleMeta ────────────────────────────────────
//
// The trace file of crucible_mode.py holds CrucibleMeta records, and the
// trace loader reads them as TensorMeta records.  So the two structs keep
// one layout, byte for byte.  The C thunks do not read a record through
// TensorMeta, and decode_ffi_meta does not need the equal layouts.
static_assert(sizeof(CrucibleMeta) == sizeof(crucible::TensorMeta), "CrucibleMeta size must match TensorMeta");
static_assert(sizeof(CrucibleMeta) == 176);
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
static_assert(offsetof(CrucibleMeta, pad0) == 156);
static_assert(offsetof(CrucibleMeta, storage_nbytes) == 160);
static_assert(offsetof(CrucibleMeta, grad_fn_hash) == 168);

// The helpers from here to decode_ffi_records take the handle and the meta
// array of the C ABI as raw pointers, give them a type, and read the records.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the helpers give a type to the handle and the meta array of the C caller")

namespace detail {

// The alignment of crucible::Vigil.  The class is only declared here, so
// alignof cannot read it.  vessel_api.cpp includes crucible/Vigil.h and
// checks that the two values are equal.
inline constexpr std::size_t kVigilHandleAlignment = 64;

// A handle of the C caller is above the first page, so it is not null, and
// it has the alignment of a Vigil.  The checks stay in each build, so a
// damaged handle ends the process before a read through it.
inline void assert_plausible_vigil_handle(CrucibleHandle handle) noexcept {
    const auto bits = std::bit_cast<std::uintptr_t>(handle);
    CRUCIBLE_PRE(bits >= 4096U);
    CRUCIBLE_PRE((bits & (kVigilHandleAlignment - 1U)) == 0U);
}

// An empty array comes with a null pointer, and each other array comes with
// a pointer that is not null.  The pointer has the alignment of a
// CrucibleMeta.  The checks stay in each build, so a damaged array of the C
// ABI ends the process before a read through it.
inline void assert_plausible_meta_array(const CrucibleMeta* metas, std::size_t n_metas) noexcept {
    CRUCIBLE_PRE((metas == nullptr) == (n_metas == 0U));
    CRUCIBLE_PRE((std::bit_cast<std::uintptr_t>(metas) & (alignof(CrucibleMeta) - 1U)) == 0U);
}

}  // namespace detail

[[nodiscard]] CRUCIBLE_HOT TypedHandle as_vigil_typed(CrucibleHandle handle) noexcept {
    detail::assert_plausible_vigil_handle(handle);
    return ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(static_cast<Vigil*>(handle));
}

[[nodiscard]] CRUCIBLE_HOT CrucibleHandle from_typed(TypedHandle handle) noexcept {
    return static_cast<CrucibleHandle>(handle.value());
}

// ── Meta-array typed view ──────────────────────────────────────────
//
// `n_metas` is consumed only by the plausibility check.  The returned
// typed view does not carry the length, because callers already thread
// `n_metas` separately through the C ABI.  When n_metas==0, `metas`
// MUST be nullptr — the invariant the C ABI promises.
[[nodiscard]] CRUCIBLE_HOT TypedMeta as_meta_typed(const CrucibleMeta* metas, std::size_t n_metas = 0) noexcept {
    detail::assert_plausible_meta_array(metas, n_metas);
    return ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(metas);
}

[[nodiscard]] CRUCIBLE_HOT const CrucibleMeta* metas_from_typed(TypedMeta typed) noexcept { return typed.value(); }

// ── Per-meta data_ptr typed accessor ───────────────────────────────
//
// Crucible's TensorMeta::data_ptr is a `void*` whose provenance is
// "data pages PyTorch handed to us — externally-owned, lifetime
// controlled by the autograd / caching-allocator graph on the Python
// side".  The field is an ExternalDataPtr, a `Tagged<void*,
// source::External>`, so downstream consumers (memory-plan recording,
// pool-shadow-handle binding, Cipher snapshot serializer) carry the
// provenance into their own type-level reasoning.  This helper reads
// the pointer of one record through CrucibleMeta.
//
// `i` MUST be a valid index in the typed meta array (caller is the
// authority on the array length, which travels via `n_metas` through
// the C ABI).  The precondition refuses a null array in each build.
[[nodiscard]] CRUCIBLE_HOT ExternalDataPtr data_ptr_typed(TypedMeta typed, std::size_t i) noexcept {
    const CrucibleMeta* metas = typed.value();
    CRUCIBLE_PRE(metas != nullptr);
    return external_data_ptr(metas[i].data_ptr);
}

// ── The records of one op of a C caller ────────────────────────────
//
// A recording entry point takes the records of an op as a View of
// TensorMeta.  The C caller gives CrucibleMeta records, and a TensorMeta
// exists only where the vessel makes one.  No door of the base starts the
// life of a TensorMeta over foreign bytes: the checked lifetime start and
// mint_foreign_view refuse a type with a Tagged pointer, a bool and
// enumerations, and decode refuses a pointer.  So a thunk decodes each
// record into the OpRecordSlots of the Vigil (Vigil::op_record_slots), and
// the View borrows the slots.  The slots hold the most records that the trust
// ladder admits: kMaxOpInputs inputs and kMaxOpOutputs outputs
// (entry_is_well_formed).

// The TensorMeta of one record of a C caller, or no value.  The function reads
// each field through CrucibleMeta, the type that the caller wrote, and gives
// the fields to decode_tensor_meta of crucible/TensorMeta.h.  That door does
// the checks of each claim of TensorMeta, the same checks as for a record of
// a trace file.  The data pointer is an address of this process, and the
// TensorMeta keeps it.  Linear in kMaxTensorNDim.
[[nodiscard]] inline ::fixy::Option<TensorMeta> decode_ffi_meta(const CrucibleMeta& record) noexcept {
    UncheckedTensorMeta fields{};
    for (const TensorDimArray::LaneIndex lane : decltype(fields.sizes)::indices()) {
        fields.sizes.at(lane) = record.sizes[lane.raw()];
        fields.strides.at(lane) = record.strides[lane.raw()];
    }
    fields.ndim = record.ndim;
    fields.dtype = record.dtype;
    fields.device_type = record.device_type;
    fields.device_idx = record.device_idx;
    fields.layout = record.layout;
    fields.requires_grad = record.requires_grad;
    fields.flags = record.flags;
    fields.output_nr = record.output_nr;
    fields.storage_offset = record.storage_offset;
    fields.version = record.version;
    fields.storage_nbytes = record.storage_nbytes;
    fields.grad_fn_hash = record.grad_fn_hash;
    return decode_tensor_meta(fields, external_data_ptr(record.data_ptr));
}

// Decodes the n_metas records at typed into the slots 0 to n_metas - 1, and
// gives the View of those slots.  A slot at or past n_metas can hold a record
// of an earlier op, and the View does not reach it.  No value when the count
// passes the room of the slots or when decode_ffi_meta refuses a record.  The
// caller then records nothing.  Linear in n_metas.
[[nodiscard]] inline ::fixy::Option<::fixy::View<TensorMeta const>>
decode_ffi_records(TypedMeta typed, uint32_t n_metas, OpRecordSlots& slots) noexcept {
    if (n_metas > kMaxOpRecords) return ::fixy::none;
    for (const ::fixy::Index<kMaxOpRecords> slot : OpRecordSlots::indices()) {
        if (slot.raw() == n_metas) break;
        const bool is_decoded = decode_ffi_meta(typed.value()[slot.raw()])
                                    .match(
                                        [&slots, slot](TensorMeta meta) noexcept {
                                            slots.at(slot) = meta;
                                            return true;
                                        },
                                        [] noexcept { return false; });
        if (!is_decoded) return ::fixy::none;
    }
    const OpRecordSlots& written = slots;
    return written.view().window(0, n_metas);
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// ── Schema-name typed lookup ───────────────────────────────────────
//
// `crucible::schema_name(SchemaHash)` gives a C text in the interned-name
// storage of the schema tables.  The table admits a name only when it is
// Sanitized (a boundary checked it) or FromInternal (it crossed no
// boundary).  The table owns the characters that it gives, so the text is
// tagged `source::Interned`.  That tag is different from the raw C ABI input
// (`source::ABIBoundary`) that crucible_register_schema_name receives.
//
// An unknown hash gives no value.  The text borrows the storage of the table,
// which keeps it until a registration of the same hash replaces the name.

using TypedSchemaName = crucible::SchemaTable::LookupName;

[[nodiscard]] CRUCIBLE_HOT ::fixy::Option<TypedSchemaName>
schema_name_typed(crucible::SchemaHash schema_hash) noexcept {
    return crucible::schema_name(schema_hash);
}

// ── Recording trust ladder ─────────────────────────────────────────
//
// Every Entry this adapter builds is filled from values a foreign
// runtime supplied: the C ABI hands over raw integers, the boxed
// fallback reads an ATen stack, and the unboxed kernels read their own
// typed arguments.  All three start at
// `crucible::TraceRing::FromPytorchEntryPtr`.  `Vigil::record_op` and
// `Vigil::dispatch_op` take a `crucible::ValidatedOp`, whose one door
// `crucible::mint_validated_op` takes a
// `crucible::TraceRing::ValidatedEntryPtr` and checks the count of the
// records.  The retag catalog admits exactly one transition between the
// two tags of the entry, so the checks below are the whole route from an
// adapter to the ring.  The boxed fallback and the C ABI build entries
// from data that has no compile-time bound, and they run the checks at
// run time through `validate_ffi_op`.  The unboxed kernels discharge the
// same checks at compile time: record_kernel.h asserts that
// `entry_is_well_formed` and `metas_are_well_formed` accept the
// largest entry each operator can build, and then retags without the
// runtime walk.
//
// Three adapters share one ladder deliberately.  A check that lives in
// one adapter is a check the next adapter does not have, and this file
// already holds every other rule the boundary obeys.

// Structural bounds on an Entry.  Each one names a downstream array
// the recording pipeline indexes with the field:
//
//   - schema_hash != 0, because 0 is the invalid sentinel and the
//     content hash and the region cache both key on this field
//   - num_inputs at most kMaxOpInputs and num_outputs at most
//     kMaxOpOutputs, so the records of the op fit the OpRecordSlots
//   - num_scalar_args at most 5, the width of Entry::scalar_values.
//     BackgroundThread asserts the same bound when it drains the ring,
//     so a larger count reaches a contract violation rather than the
//     pipeline; the adapter rejects the operation here instead.
[[nodiscard]] CRUCIBLE_HOT constexpr bool entry_is_well_formed(const crucible::TraceRing::Entry& entry) noexcept {
    if (entry.schema_hash.raw() == 0) return false;
    if (entry.num_inputs > crucible::kMaxOpInputs) return false;
    if (entry.num_outputs > crucible::kMaxOpOutputs) return false;
    if (entry.num_scalar_args > decltype(crucible::TraceRing::Entry::scalar_values)::size()) return false;
    return true;
}

// The structural invariant of TensorMeta is that sizes[] and strides[]
// are fixed arrays of kMaxTensorNDim entries.  An ndim past that makes
// every per-dimension loop in the recording pipeline read out of
// bounds.
//
// Each enumeration of a meta must also name an enumerator.  A meta of the
// C ABI holds the bytes that a foreign caller wrote, and an element type
// that no enumerator names reaches element_size in the background
// pipeline, which ends the process.  The decoders below keep such a value
// out of a meta that a recording kernel fills.
//
// An empty View is well formed.  An operation carrying no tensors is
// ordinary.
//
// Cost is one byte compare and three enumerator tests per meta.  A
// typical operation carries two to eight metas, and the dispatch that
// reached here is orders of magnitude larger.
[[nodiscard]] CRUCIBLE_HOT constexpr bool
metas_are_well_formed(::fixy::View<const crucible::TensorMeta> metas) noexcept {
    for (const crucible::TensorMeta& meta : metas) {
        if (meta.ndim > crucible::kMaxTensorNDim) return false;
        if (!crucible::valid_scalar_type(meta.dtype) || !crucible::valid_device_type(meta.device_type)
            || !crucible::valid_layout(meta.layout))
            return false;
    }
    return true;
}

// ── The enumerations of a foreign tensor ───────────────────────────
//
// A tensor of the foreign runtime gives its element type, its device and
// its layout as c10 enumerations, and each one enters a TensorMeta only
// through a decoder below.  A decoder compares the c10 ordinal with the c10
// ordinals of the enumerators that crucible names.  It gives no value for
// each other ordinal: a quantized type, a narrow integer, a new device,
// the MKL-DNN layout or the jagged layout.  The recording paths then leave
// the ring alone, as for each other operation that the trust ladder
// refuses.
//
// crucible::ScalarType and crucible::DeviceType give each enumerator the
// ordinal of c10, and record_kernel.h asserts each one.  crucible::Layout
// numbers its enumerators from SparseCsc on in a different way than c10, so
// the layout decoder maps the ordinals of C10Layout by name.
// record_kernel.h asserts each enumerator of C10Layout against c10::Layout.

// The ordinals of c10::Layout.
enum class C10Layout : int8_t {
    Strided = 0,
    Sparse = 1,
    SparseCsr = 2,
    Mkldnn = 3,
    SparseCsc = 4,
    SparseBsr = 5,
    SparseBsc = 6,
    Jagged = 7,
};

// The crucible element type of a c10 element type, or no value.  Each c10
// ordinal is zero or more.  The one negative crucible enumerator is
// Undefined, which c10 spells with a different ordinal, so the caller maps
// the c10 undefined type before the call.
[[nodiscard]] CRUCIBLE_HOT constexpr ::fixy::Option<crucible::ScalarType>
decode_c10_scalar_type(int8_t ordinal) noexcept {
    if (ordinal < 0) return ::fixy::none;
    return ::fixy::enum_from<crucible::ScalarType>(ordinal);
}

// The crucible device type of a c10 device type, or no value.
[[nodiscard]] CRUCIBLE_HOT constexpr ::fixy::Option<crucible::DeviceType>
decode_c10_device_type(int8_t ordinal) noexcept {
    return ::fixy::enum_from<crucible::DeviceType>(ordinal);
}

// The crucible layout of a c10 layout.  The map goes by the name of each
// layout, because the ordinals differ from SparseCsc on.
[[nodiscard]] CRUCIBLE_HOT constexpr ::fixy::Option<crucible::Layout> layout_of_c10(C10Layout layout) noexcept {
    using Decoded = ::fixy::Option<crucible::Layout>;
    switch (layout) {
        case C10Layout::Strided:
            return Decoded::some(crucible::Layout::Strided);
        case C10Layout::Sparse:
            return Decoded::some(crucible::Layout::Sparse);
        case C10Layout::SparseCsr:
            return Decoded::some(crucible::Layout::SparseCsr);
        case C10Layout::SparseCsc:
            return Decoded::some(crucible::Layout::SparseCsc);
        case C10Layout::SparseBsr:
            return Decoded::some(crucible::Layout::SparseBsr);
        case C10Layout::SparseBsc:
            return Decoded::some(crucible::Layout::SparseBsc);
        case C10Layout::Mkldnn:
        case C10Layout::Jagged:
            return ::fixy::none;
        default:
            ::fixy::unreachable();
    }
}

// The crucible layout of a c10 layout ordinal, or no value.
[[nodiscard]] CRUCIBLE_HOT constexpr ::fixy::Option<crucible::Layout> decode_c10_layout(int8_t ordinal) noexcept {
    return ::fixy::enum_from<C10Layout>(ordinal).match(
        [](C10Layout layout) noexcept { return layout_of_c10(layout); },
        [] noexcept { return ::fixy::Option<crucible::Layout>{::fixy::none}; });
}

// The tag a recording entry point accepts.  TraceRing owns the pointer
// type, so the vessel reads the tag from it and names no substrate
// namespace of its own.  The two retags in the vessel use this alias.
using ValidatedEntryTag = crucible::TraceRing::ValidatedEntryPtr::tag_type;

// The runtime door from the first tag to an op that a recording entry point
// takes.  An empty return says no certification exists for this operation,
// and the caller executes eagerly and leaves the ring alone.  There is no
// failure value that carries the second tag, which is what makes running
// the checks the only way an adapter reaches a recording entry point.
//
// The records are the inputs and then the outputs of the entry.
// crucible::mint_validated_op gives no op for a count of records that is
// not their sum, so the foreign caller of such a count records nothing.
[[nodiscard]] CRUCIBLE_HOT constexpr ::fixy::Option<crucible::ValidatedOp>
validate_ffi_op(crucible::TraceRing::FromPytorchEntryPtr raw, ::fixy::View<const crucible::TensorMeta> metas) noexcept {
    if (raw.value() == nullptr) return ::fixy::none;
    const crucible::TraceRing::Entry& entry = *raw.value();
    if (!entry_is_well_formed(entry) || !metas_are_well_formed(metas)) return ::fixy::none;
    return crucible::mint_validated_op(std::move(raw).retag<ValidatedEntryTag>(), metas);
}

}  // namespace crucible::vessel
