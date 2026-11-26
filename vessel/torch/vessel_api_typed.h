#pragma once

// Typed helpers for the internal Vessel C-ABI boundary.
//
// CrucibleMeta remains a plain C struct in vessel_api.h because it is
// exported to C / ctypes callers.  Inside C++, every value crossing the FFI
// boundary is immediately tagged with the `source::ABIBoundary` provenance tag
// so foreign-runtime provenance is visible in the type system before the
// pointer reaches TensorMeta-consuming code.  A handle of a Vigil is a word of
// the table of handles, and vigil_handles.h checks it.
//
// ── Helper surface ─────────────────────────────────────────────────
//
//   TypedArray<E>             := Tagged<E*, ABIBoundary>
//   as_array_typed(first, n)  : E*, std::size_t          -> Option<TypedArray<E>>
//   TypedMeta                 := TypedArray<const CrucibleMeta>
//   as_meta_typed(metas, n)   : const CrucibleMeta*, n   -> Option<TypedMeta>
//   metas_from_typed(typed)   : TypedMeta                -> const CrucibleMeta*
//
//   data_ptr_typed(typed, i)  : TypedMeta, std::size_t   -> ExternalDataPtr
//   decode_ffi_meta(record)   : const CrucibleMeta&      -> Option<TensorMeta>
//   decode_ffi_records(...)   : TypedMeta, slots&&       -> Option<ViewMut<TensorMeta>>
//
//   TypedSchemaName           := InternedSchemaName
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
// empty element_type, so `sizeof(TypedMeta) == sizeof(void*)` is
// static_assert'd below.  The helpers that give a type to a pointer are
// CRUCIBLE_HOT (always-inline) and compile to the same machine code as the
// bare pointer after EBO collapse.
//
// ── Usage rule (review-enforced) ───────────────────────────────────
//
// Every C-ABI thunk in vessel_api.cpp that takes a handle begins with
// `vigil_handles.mint_held_vigil(h)` of vigil_handles.h.  A thunk that takes
// an array gives the pointer and the count to `as_meta_typed` or
// `as_array_typed` before it reads or writes an element, and it gives its
// refusal when the door gives no value.  Grepping for `mint_held_vigil`,
// `as_meta_typed` or `as_array_typed` finds every ABI-crossing site in O(1).

#include "aten_op_table.h"
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

// The helpers below name the Vigil only as a template argument, so a
// declaration is sufficient, and this header does not include
// crucible/Vigil.h.
namespace crucible {
class Vigil;
}  // namespace crucible

namespace crucible::vessel {

// ── The typed records of a C caller ───────────────────────────────
//
// ABIBoundary names where a value came from, so mint_tagged builds a
// value under it.  A check that the value passed reaches Sanitized only
// by retag along the catalog edge.  The data pointer of a meta is the
// ExternalDataPtr of TensorMeta, another ::fixy Tagged.
//
// An array of a C caller, as the pointer to its first element.  The records
// keep their C type.  decode_ffi_meta below makes a TensorMeta from one
// record.

template <class Element>
using TypedArray = ::fixy::Tagged<Element*, ::fixy::tags::source::ABIBoundary>;

using TypedMeta = TypedArray<const CrucibleMeta>;

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

// The helpers from here to decode_ffi_records take the meta array of the C
// ABI as raw pointers, give them a type, and read the records.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the helpers give a type to the meta array of the C caller")

// ── The arrays of a C caller ───────────────────────────────────────
//
// A C caller gives an array as a pointer to its first element and a count.
// The pointer and the count are outside data, so this door tests the pair
// once, at the boundary, and a thunk gives the refusal of its entry when the
// door gives no value.  No contract check judges a pointer of a caller.
//
//   - A count of 0 reads no element.  The door gives the null pointer, so a
//     caller can give each pointer with an empty array, such as the buffer
//     of an empty ctypes array.
//   - A count above 0 needs a pointer that is not null and that has the
//     alignment of the element type.  The door gives no value for each
//     other pointer.
//
// The typed pointer does not carry the count, because each caller keeps
// the count of the C call beside it.  The door cannot prove that the caller
// owns count elements at the pointer: that is the pact of the C interface.

template <class Element>
[[nodiscard]] CRUCIBLE_HOT ::fixy::Option<TypedArray<Element>> as_array_typed(Element* first,
                                                                              std::size_t count) noexcept {
    using Checked = ::fixy::Option<TypedArray<Element>>;
    if (count == 0U)
        return Checked::some(::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(static_cast<Element*>(nullptr)));
    const bool is_aligned = (std::bit_cast<std::uintptr_t>(first) & (alignof(Element) - 1U)) == 0U;
    if (first == nullptr || !is_aligned) return ::fixy::none;
    return Checked::some(::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(first));
}

// The records of a C caller: n_metas CrucibleMeta records at metas.
[[nodiscard]] CRUCIBLE_HOT ::fixy::Option<TypedMeta> as_meta_typed(const CrucibleMeta* metas,
                                                                   std::size_t n_metas) noexcept {
    return as_array_typed(metas, n_metas);
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
// enumerations, and decode refuses a pointer.  So a thunk opens a frame on
// the record stack of the Vigil (Vigil::op_record_stack), decodes each record
// into a slot of the frame, and the View borrows the slots.  The
// stack holds the most records that the trust ladder admits: kMaxOpInputs
// inputs and kMaxOpOutputs outputs (entry_is_well_formed).

// The TensorMeta of one record of a C caller, or no value.  The function reads
// each field through CrucibleMeta, the type that the caller wrote, and gives
// the fields to decode_tensor_meta of crucible/TensorMeta.h.  That door does
// the checks of each claim of TensorMeta, the same checks as for a record of
// a trace file.  The data pointer is an address of this process, and the
// TensorMeta keeps it.  Linear in kMaxTensorNDim.
[[nodiscard]] inline ::fixy::Option<TensorMeta> decode_ffi_meta(const CrucibleMeta& record) noexcept {
    UncheckedTensorMeta fields{};
    for (const TensorDimArray::LaneIndex lane : decltype(fields.sizes)::indices()) {
        fields.sizes.set(lane, record.sizes[lane.raw()]);
        fields.strides.set(lane, record.strides[lane.raw()]);
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

namespace detail {

// Decodes one record at typed into each slot of slots, in order: the loop of
// decode_ffi_records.  False when decode_ffi_meta refuses a record.  Linear in
// the count of the slots.
template <class Slots>
[[nodiscard]] inline bool decode_ffi_records_into_(TypedMeta typed, Slots& slots) noexcept {
    uint32_t position = 0;
    for (TensorMeta& slot : slots) {
        const bool is_decoded = decode_ffi_meta(typed.value()[position])
                                    .match(
                                        [&slot](TensorMeta meta) noexcept {
                                            slot = meta;
                                            return true;
                                        },
                                        [] noexcept { return false; });
        if (!is_decoded) return false;
        ++position;
    }
    return true;
}

}  // namespace detail

// Decodes one record at typed into each slot of the run of a frame, in order,
// and gives the run back.  The caller gives as many slots as it gives
// records: the slots of the frame of the op on the record stack, or the run of
// another frame.  The decode takes the ViewMut of the frame by move, so it is
// the one writer of the run, and the result is that one writer again, which
// the caller gives to the mint of the op.  No value when decode_ffi_meta
// refuses a record.  The caller then records nothing.  Linear in the count of
// the slots.
template <class Brand, std::size_t Extent>
[[nodiscard]] inline ::fixy::Option<::fixy::ViewMut<TensorMeta, Brand, Extent>>
decode_ffi_records(TypedMeta typed, ::fixy::ViewMut<TensorMeta, Brand, Extent>&& slots) noexcept {
    ::fixy::ViewMut<TensorMeta, Brand, Extent> decoded{std::move(slots)};
    if (!detail::decode_ffi_records_into_(typed, decoded)) return ::fixy::none;
    return ::fixy::Option<::fixy::ViewMut<TensorMeta, Brand, Extent>>::some(std::move(decoded));
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
// which keeps the first name of each hash for the life of the table.

using TypedSchemaName = crucible::InternedSchemaName;

[[nodiscard]] CRUCIBLE_HOT ::fixy::Option<TypedSchemaName>
schema_name_typed(crucible::SchemaHash schema_hash) noexcept {
    return crucible::schema_name(schema_hash);
}

// ── Schema-name registration ───────────────────────────────────────
//
// A schema name from outside the process comes to two doors of the vessel:
// the C entry crucible_register_schema_name, and the boxed fallback, which
// reads the name of an operator of PyTorch.  A library or a Python module can
// define an operator with any name at run time, so the name of an operator is
// outside data too.  register_outside_schema_name is the one door of such a
// name, and no name from outside ends the process there.  It gives a refusal,
// and the tables stay as they were, for these names:
//   - A name that crucible::sanitize_schema_name refuses: a hash of zero, and
//     a name that is empty, longer than SCHEMA_NAME_MAX or that holds a zero
//     byte.
//   - A name with a reserved hash (the reserved hashes below).
//   - A name whose hash the tables hold with a different name.
//   - A name with a new hash, when the table has no room for it: it holds
//     its capacity of names, or the name would take its names past
//     SCHEMA_NAME_BYTES_MAX bytes.
//
// The reserved hashes.  The unboxed kernel of an operator of the generated
// table (aten_op_table.h) registers the name of the operator as an internal
// name when the operator first records.  An internal name that collides with
// a held name ends the process (SchemaTableOf::register_name).  So the door
// refuses an outside name that has the hash of an operator of the table and a
// different name, and no later internal registration finds an outside name on
// its hash.  The same name with the same hash names the same operator, and
// the door takes it.

// The refusal of a schema name from outside the process.
enum class OutsideNameRefusal : std::uint8_t {
    // The four refusals of crucible::SchemaNameError.
    zero_hash,
    empty,
    too_long,
    holds_zero,
    // An operator of the generated table has the hash and a different name.
    reserved,
    // The tables hold a different name of the hash.
    collision,
    // The table has no room for the new hash.
    full,
};

// The refusal of the door for a refusal of the validator.
[[nodiscard]] constexpr OutsideNameRefusal refusal_of(crucible::SchemaNameError refused) noexcept {
    switch (refused) {
        case crucible::SchemaNameError::zero_hash:
            return OutsideNameRefusal::zero_hash;
        case crucible::SchemaNameError::empty:
            return OutsideNameRefusal::empty;
        case crucible::SchemaNameError::too_long:
            return OutsideNameRefusal::too_long;
        case crucible::SchemaNameError::holds_zero:
            return OutsideNameRefusal::holds_zero;
        default:
            ::fixy::unreachable();
    }
}

// The two parts of the name of the operator of row: its name, and its
// overload, which is empty for an operator with no overload.
[[nodiscard]] constexpr crucible::SchemaNameParts name_parts_of(OpEntry const& row) noexcept {
    return crucible::SchemaNameParts{::fixy::side_as<crucible::side::SchemaBaseName>(row.name),
                                     ::fixy::side_as<crucible::side::SchemaOverload>(row.overload)};
}

// The name of the operator of the generated table with the hash, or no value
// (the reserved hashes).  The search of the hash order of the table finds
// the row of the hash (aten_op_index_of).  Complexity: O(log n) in the rows of
// the table, one time for each name from outside.  The fallback keeps the
// result in its schema cache, and crucible_mode.py keeps the hash of each
// name.
[[nodiscard]] constexpr ::fixy::Option<crucible::SchemaNameParts>
find_aten_reserved_name(crucible::SchemaHash hash) noexcept {
    // The parts borrow the texts of the program image, so the match reads a
    // named Option.
    const ::fixy::Option<AtenOpIndex> found = aten_op_index_of(hash);
    return found.match([](AtenOpIndex index) noexcept { return ::fixy::some(name_parts_of(aten_op_row_at(index))); },
                       [] noexcept { return ::fixy::Option<crucible::SchemaNameParts>{::fixy::none}; });
}

// The reserved hashes of the vessel: the hash of each operator of the
// generated table, with the name of the operator.  The door of an outside
// name refuses a different name of such a hash, and a load of a trace that
// registers its names takes this reserve too.
inline constexpr crucible::SchemaNameReserve kAtenNameReserve{crucible::SchemaNameSearchOf<&find_aten_reserved_name>{}};

// The result of register_outside_schema_name.
using OutsideNameRegistration = ::fixy::Result<::fixy::Unit, OutsideNameRefusal>;

// Registers a schema name from outside the process, or gives its refusal (the
// list above).  The name crossed a foreign ABI: the C text of a ctypes caller,
// or the two parts of the name of an operator of PyTorch.  The function is a
// template on the Vigil, because this header names the Vigil only through a
// declaration.  Its caller includes crucible/Vigil.h.  Complexity: the length
// of the name, and O(log n) in the rows of the generated table.
template <class VigilType>
    requires std::same_as<VigilType, crucible::Vigil>
[[nodiscard, gnu::cold]] OutsideNameRegistration register_outside_schema_name(
    VigilType& vigil, crucible::SchemaHash hash,
    ::fixy::Tagged<crucible::SchemaNameParts, ::fixy::tags::source::ABIBoundary> const& name) noexcept {
    return crucible::sanitize_schema_name(hash, name)
        .match(
            [&vigil](crucible::SanitizedSchemaName sanitized) noexcept -> OutsideNameRegistration {
                if (kAtenNameReserve.is_reserved_for_other_name(sanitized.hash(), sanitized.parts()))
                    return ::fixy::err(OutsideNameRefusal::reserved);
                const crucible::SchemaRegistration registration = vigil.register_schema_name(sanitized);
                if (crucible::is_collision(registration)) return ::fixy::err(OutsideNameRefusal::collision);
                if (crucible::is_full(registration)) return ::fixy::err(OutsideNameRefusal::full);
                return ::fixy::Unit{};
            },
            [](crucible::SchemaNameError refused) noexcept -> OutsideNameRegistration {
                return ::fixy::err(refusal_of(refused));
            });
}

// The code of crucible_register_schema_name for a refusal of the door: a
// name that the validator refuses gives CRUCIBLE_SCHEMA_NAME_REFUSED, a
// reserved hash and a collision give CRUCIBLE_SCHEMA_NAME_COLLISION, and a
// full table gives CRUCIBLE_SCHEMA_NAME_FULL.
[[nodiscard]] constexpr int schema_name_code_of(OutsideNameRefusal refused) noexcept {
    switch (refused) {
        case OutsideNameRefusal::zero_hash:
        case OutsideNameRefusal::empty:
        case OutsideNameRefusal::too_long:
        case OutsideNameRefusal::holds_zero:
            return CRUCIBLE_SCHEMA_NAME_REFUSED;
        case OutsideNameRefusal::reserved:
        case OutsideNameRefusal::collision:
            return CRUCIBLE_SCHEMA_NAME_COLLISION;
        case OutsideNameRefusal::full:
            return CRUCIBLE_SCHEMA_NAME_FULL;
        default:
            ::fixy::unreachable();
    }
}

// crucible_register_schema_name reads the C text of its name up to the first
// zero, and gives the code of this function: CRUCIBLE_SCHEMA_NAME_REGISTERED,
// or the code of the refusal of register_outside_schema_name.
template <class VigilType>
    requires std::same_as<VigilType, crucible::Vigil>
[[nodiscard, gnu::cold]] int
register_schema_name_typed(VigilType& vigil, std::uint64_t schema_hash,
                           ::fixy::Tagged<::fixy::TextView, ::fixy::tags::source::ABIBoundary> const& name) noexcept {
    return register_outside_schema_name(
               vigil, crucible::SchemaHash{schema_hash},
               ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(crucible::SchemaNameParts{name.value()}))
        .match([](::fixy::Unit) noexcept { return int{CRUCIBLE_SCHEMA_NAME_REGISTERED}; },
               [](OutsideNameRefusal refused) noexcept { return schema_name_code_of(refused); });
}

// ── Recording trust ladder ─────────────────────────────────────────
//
// Every Entry this adapter builds is filled from values a foreign
// runtime supplied: the C ABI hands over raw integers, the boxed
// fallback reads an ATen stack, and the unboxed kernels read their own
// typed arguments.  All three start at
// `crucible::TraceRing::FromPytorchEntry`.  `Vigil::record_op` and
// `Vigil::dispatch_op` take a `crucible::ValidatedOp`, whose one door
// `crucible::mint_validated_op` takes a
// `crucible::TraceRing::ValidatedEntry` and checks the count of the
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

// The structural bounds on an Entry are the checks of
// crucible::entry_is_well_formed (crucible/ValidatedOp.h), the one validator
// of an entry that the trace loader calls too.

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
        if (::fixy::enum_from<crucible::ScalarType>(std::to_underlying(meta.dtype)).is_none()
            || ::fixy::enum_from<crucible::DeviceType>(std::to_underlying(meta.device_type)).is_none()
            || ::fixy::enum_from<crucible::Layout>(std::to_underlying(meta.layout)).is_none())
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
    return ::fixy::enum_from<C10Layout>(ordinal).and_then(
        [](C10Layout layout) noexcept { return layout_of_c10(layout); });
}

// The tag a recording entry point accepts.  TraceRing owns the tagged entry
// type, so the vessel reads the tag from it and names no substrate
// namespace of its own.  The two retags in the vessel use this alias.
using ValidatedEntryTag = crucible::TraceRing::ValidatedEntry::tag_type;

// The runtime door from the first tag to an op that a recording entry point
// takes.  An empty return says no certification exists for this operation,
// and the caller executes eagerly and leaves the ring alone.  There is no
// failure value that carries the second tag, which is what makes running
// the checks the only way an adapter reaches a recording entry point.
//
// The records are the inputs and then the outputs of the entry, in the run
// of a frame, whose one writer the door takes by move and gives to the mint.
// crucible::mint_validated_op gives no op for a count of records that is
// not their sum, so the foreign caller of such a count records nothing.
// The checks read the sealed entry that the tag names, and no code writes
// it.  So the op reads the fields that the checks read.
template <class Brand, std::size_t Extent>
[[nodiscard]] CRUCIBLE_HOT constexpr ::fixy::Option<crucible::ValidatedOp>
validate_ffi_op(crucible::TraceRing::FromPytorchEntry raw,
                ::fixy::ViewMut<crucible::TensorMeta, Brand, Extent>&& metas) noexcept {
    ::fixy::ViewMut<crucible::TensorMeta, Brand, Extent> held{std::move(metas)};
    if (!crucible::entry_is_well_formed(**raw.value())
        || !metas_are_well_formed(::fixy::View<const crucible::TensorMeta>{held.view()}))
        return ::fixy::none;
    return crucible::mint_validated_op(std::move(raw).retag<ValidatedEntryTag>(), std::move(held));
}

}  // namespace crucible::vessel
