#pragma once

// record_kernel.h — one unboxed recording kernel per ATen operator, plus the
// ATen-to-Crucible metadata bridge both recording paths share.
//
// Why an unboxed kernel
//
// The boxed fallback in crucible_fallback.cpp catches every operator through
// one handler, and pays for the reach: the dispatcher builds an IValue stack,
// the handler walks it asking each element what it is, and the schema hash
// comes from a thread-local cache keyed on the operator handle. A kernel
// registered for one operator receives its arguments in their declared C++
// types. The walk becomes a fold the compiler unrolls, the type questions are
// answered at compile time, and the schema hash is a constant from
// aten_op_table.h.
//
// A per-operator kernel takes precedence over a fallback. Registering one
// moves that operator to this path and changes nothing else, because both
// paths build the same TraceRing::Entry from the bridge functions below.
//
// Where the signature comes from
//
// decltype(Op::call) is the function type of the declared call: the result
// and the parameter types. A partial specialization takes it apart into a
// parameter pack, which is the pack the kernel declares and the pack the
// dispatcher calls it with.
//
// Three agreements
//
//   1. The declared signature equals Op::schema, the typedef PyTorch's own
//      generator writes. Catches a fork whose call and schema drift apart.
//   2. The parameter count equals the table's arity. Catches a table built
//      against a different revision of the fork than this build compiles.
//   3. The tensor and tensor-list masks computed from the C++ parameter types
//      equal the masks the generator computed from the schema string. Two
//      readings of one argument list must name the same positions. Measured
//      over all 3110 operators of the fork: zero disagreements.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Role.h>
#include <fixy/Tagged.h>
#include <foundation/Quarantine.h>
#include <foundation/core/WindowStack.h>

#include <crucible/CKernel.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <crucible/Vigil.h>

#include <c10/core/CrucibleState.h>

#include <ATen/core/Tensor.h>
#include <ATen/core/dispatch/Dispatcher.h>
#include <torch/csrc/autograd/function.h>
#include <torch/csrc/autograd/variable.h>
#include <torch/library.h>

#include "aten_op_table.h"
#include "eager_numerics.h"
#include "entry_slots.h"
#include "storage_generation.h"
#include "vessel_api_typed.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible::vessel {

// =====================================================================
// FNV-1a over bytes
//
// The step and the fold over a text are in entry_slots.h, with the list of
// the structural scalars that keys on the schema hash.
// =====================================================================

// The default seed starts a fresh fold. Passing a previous result continues
// one, which is how the shape hash chains across several tensors.
// Complexity: O(n) in the bytes.
[[nodiscard]] inline uint64_t fnv1a_bytes(::fixy::View<std::byte const> bytes,
                                          uint64_t accumulator = kFnvOffset) noexcept {
    for (const std::byte byte : bytes)
        accumulator = fnv1a_step(accumulator, static_cast<uint8_t>(byte));
    return accumulator;
}

// =====================================================================
// What one operation contributes to a trace entry
//
// The metadata limit, the scalar slots and the overflow digest live in
// entry_slots.h, which holds no PyTorch type.
// =====================================================================

struct MetaCount {
    uint16_t inputs = 0;
    uint16_t outputs = 0;

    [[nodiscard]] uint32_t total() const { return static_cast<uint32_t>(inputs) + outputs; }
};

// =====================================================================
// Tensor metadata
//
// Fills all 176 bytes of crucible::TensorMeta from an at::Tensor. Shared with
// the boxed fallback: a trace whose entries were filled by two spellings of
// this would have a content hash that depends on which path recorded it.
//
// The result is false when the meta cannot describe the tensor:
//
//   - the rank is more than kMaxTensorNDim, or a size or a stride below the
//     rank is more than kMaxTensorDimExtent.  The meta is then the zero meta
//   - the element type, the device or the layout names no crucible
//     enumerator.  Each such field then holds its default.
//
// The caller then records nothing for the operation, and the operation runs
// eagerly.  The rank and the extents come from PyTorch, so this function is
// the boundary check of the record path for them.  It holds the claims of
// decode_tensor_meta of crucible/TensorMeta.h for the rank and the extents.
// Each test is one compare, once for each tensor or each dimension.
//
// The function reads the tensor through the API of PyTorch, so it is in an
// ABI region.
// =====================================================================

// Whether each meta of a record describes its tensor, or fill_meta refused
// one of the tensors.
enum class TensorDescription : unsigned char {
    described,
    refused,
};

// The answer of fill_meta for a tensor that a meta cannot describe: the zero
// meta, and false.
[[nodiscard]] CRUCIBLE_COLD inline bool refuse_tensor_meta(crucible::TensorMeta& meta) noexcept {
    meta = {};
    return false;
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: fill_meta reads a tensor through the API of PyTorch")
// The body of fill_meta, for a meta that holds zero in each byte: it writes
// the fields of the tensor, and gives false for a tensor that a meta cannot
// describe.
[[nodiscard]] inline bool fill_zeroed_meta(crucible::TensorMeta& meta, const at::Tensor& tensor) {
    if (!tensor.defined()) return true;

    // -- Core fields --------------------------------------------------

    // The bound is the lane count of the array being written, named rather than
    // spelled 8, because every other reader of that array derives its bound
    // from the same constant and a literal here could drift from it. The
    // unboxed path does not check ndim at run time, so the assert below makes
    // the rank check of the trust ladder accept each rank that this function
    // keeps, and refuse the first rank that it refuses.
    constexpr uint8_t kNDimCeiling = ::crucible::kMaxTensorNDim;
    static_assert(
        [] {
            const auto is_rank_well_formed = [](uint8_t rank) {
                crucible::TensorMeta ceiling{};
                ceiling.ndim = rank;
                const ::fixy::FixedArray<crucible::TensorMeta, 1> ceiling_records{ceiling};
                return crucible::vessel::metas_are_well_formed(ceiling_records.view());
            };
            return is_rank_well_formed(kNDimCeiling) && !is_rank_well_formed(static_cast<uint8_t>(kNDimCeiling + 1));
        }(),
        "fill_meta refuses each rank above the rank bound of the trust ladder, and the unboxed path relies on it");
    // The test has two sides, so the rank is a lane count from 0 to 8, and
    // each lane index below it is below 8.
    const int64_t rank = tensor.dim();
    if (rank < 0 || rank > int64_t{kNDimCeiling}) [[unlikely]]
        return refuse_tensor_meta(meta);
    const auto ndim = static_cast<uint8_t>(rank);
    meta.ndim = ndim;

    const auto sizes = tensor.sizes();
    for (uint8_t d = 0; d < ndim; d++) {
        const int64_t size = sizes[d];
        if (size > ::crucible::kMaxTensorDimExtent) [[unlikely]]
            return refuse_tensor_meta(meta);
        meta.sizes[d] = ::crucible::tensor_dim(size);
    }

    const bool strided = (tensor.layout() == c10::Layout::Strided);
    if (strided) {
        const auto strides = tensor.strides();
        for (uint8_t d = 0; d < ndim; d++) {
            const int64_t stride = strides[d];
            if (stride > ::crucible::kMaxTensorDimExtent) [[unlikely]]
                return refuse_tensor_meta(meta);
            meta.strides[d] = ::crucible::tensor_dim(stride);
        }
        // The storage address crosses the ATen boundary here, so it enters as
        // source::External: hashed and compared as an opaque cookie, never
        // dereferenced, until some later validator retags it.
        meta.data_ptr = ::crucible::external_data_ptr(tensor.data_ptr());
    }

    // A c10 enumeration enters the meta only through its decoder in
    // vessel_api_typed.h, which takes the c10 ordinal as an int8_t.  The
    // decoder compares the ordinal with the enumerators that crucible names,
    // and gives no value for each other ordinal.  The asserts below make the
    // ordinal exact: each c10 enumeration has the underlying type int8_t, so
    // std::to_underlying narrows nothing.
    //
    // Every mirrored ordinal is checked one by one. A single sampled ordinal
    // witnesses one lane of the invariant, not the invariant, and the per-type
    // message names the type that drifted.
    static_assert(std::is_same_v<std::underlying_type_t<c10::ScalarType>, int8_t>);
    static_assert(std::is_same_v<std::underlying_type_t<c10::DeviceType>, int8_t>);
    static_assert(std::is_same_v<std::underlying_type_t<c10::Layout>, int8_t>);

#define CRUCIBLE_MIRROR_SCALAR(name)                                                                             \
    static_assert(static_cast<int8_t>(c10::ScalarType::name) == static_cast<int8_t>(crucible::ScalarType::name), \
                  "c10::ScalarType::" #name " ordinal drifted from the crucible mirror")
    CRUCIBLE_MIRROR_SCALAR(Byte);
    CRUCIBLE_MIRROR_SCALAR(Char);
    CRUCIBLE_MIRROR_SCALAR(Short);
    CRUCIBLE_MIRROR_SCALAR(Int);
    CRUCIBLE_MIRROR_SCALAR(Long);
    CRUCIBLE_MIRROR_SCALAR(Half);
    CRUCIBLE_MIRROR_SCALAR(Float);
    CRUCIBLE_MIRROR_SCALAR(Double);
    CRUCIBLE_MIRROR_SCALAR(ComplexHalf);
    CRUCIBLE_MIRROR_SCALAR(ComplexFloat);
    CRUCIBLE_MIRROR_SCALAR(ComplexDouble);
    CRUCIBLE_MIRROR_SCALAR(Bool);
    CRUCIBLE_MIRROR_SCALAR(BFloat16);
    CRUCIBLE_MIRROR_SCALAR(Float8_e5m2);
    CRUCIBLE_MIRROR_SCALAR(Float8_e4m3fn);
    CRUCIBLE_MIRROR_SCALAR(Float8_e5m2fnuz);
    CRUCIBLE_MIRROR_SCALAR(Float8_e4m3fnuz);
    CRUCIBLE_MIRROR_SCALAR(Float8_e8m0fnu);
    CRUCIBLE_MIRROR_SCALAR(Float4_e2m1fn_x2);
#undef CRUCIBLE_MIRROR_SCALAR

    // Undefined is the one deliberate divergence of the element types. c10
    // appends it after the last scalar type, at a large positive ordinal;
    // Crucible uses -1 so that "no dtype" sorts outside the value range. The
    // asserts below pin the divergence and the property that makes the
    // explicit map total: every c10 ordinal is non-negative, so -1 cannot
    // collide with one.
    static_assert(static_cast<int8_t>(crucible::ScalarType::Undefined) < 0,
                  "the crucible Undefined sentinel must stay negative so it cannot alias a c10 ordinal");
    static_assert(static_cast<int16_t>(c10::ScalarType::Undefined) >= 0,
                  "c10 scalar ordinals must stay non-negative for the sentinel to be disjoint");
    static_assert(static_cast<int16_t>(c10::ScalarType::Undefined)
                      != static_cast<int16_t>(crucible::ScalarType::Undefined),
                  "if c10 ever adopts -1 for Undefined, drop the explicit map below");

#define CRUCIBLE_MIRROR_DEVICE(name)                                                                             \
    static_assert(static_cast<int8_t>(c10::DeviceType::name) == static_cast<int8_t>(crucible::DeviceType::name), \
                  "c10::DeviceType::" #name " ordinal drifted from the crucible mirror")
    CRUCIBLE_MIRROR_DEVICE(CPU);
    CRUCIBLE_MIRROR_DEVICE(CUDA);
    CRUCIBLE_MIRROR_DEVICE(MKLDNN);
    CRUCIBLE_MIRROR_DEVICE(HIP);
    CRUCIBLE_MIRROR_DEVICE(XLA);
    CRUCIBLE_MIRROR_DEVICE(MPS);
    CRUCIBLE_MIRROR_DEVICE(Meta);
    CRUCIBLE_MIRROR_DEVICE(PrivateUse1);
#undef CRUCIBLE_MIRROR_DEVICE

    // crucible::Layout does not mirror c10 from SparseCsc on.  C10Layout holds
    // the c10 ordinals, and decode_c10_layout maps them by name.
#define CRUCIBLE_MIRROR_LAYOUT(name)                                                              \
    static_assert(static_cast<int8_t>(c10::Layout::name) == static_cast<int8_t>(C10Layout::name), \
                  "c10::Layout::" #name " ordinal drifted from C10Layout")
    CRUCIBLE_MIRROR_LAYOUT(Strided);
    CRUCIBLE_MIRROR_LAYOUT(Sparse);
    CRUCIBLE_MIRROR_LAYOUT(SparseCsr);
    CRUCIBLE_MIRROR_LAYOUT(Mkldnn);
    CRUCIBLE_MIRROR_LAYOUT(SparseCsc);
    CRUCIBLE_MIRROR_LAYOUT(SparseBsr);
    CRUCIBLE_MIRROR_LAYOUT(SparseBsc);
    CRUCIBLE_MIRROR_LAYOUT(Jagged);
#undef CRUCIBLE_MIRROR_LAYOUT

    // A dtype c10 names and Crucible does not — a quantized type, a narrow
    // integer width, a newer FP8 variant — gives no value, and so do a device
    // and a layout that crucible does not name. Widening a mirror is a
    // Types.h change.
    const auto scalar_type = tensor.scalar_type();
    ::fixy::Option<crucible::ScalarType> dtype =
        scalar_type == c10::ScalarType::Undefined
            ? ::fixy::Option<crucible::ScalarType>::some(crucible::ScalarType::Undefined)
            : decode_c10_scalar_type(std::to_underlying(scalar_type));
    ::fixy::Option<crucible::DeviceType> device_type =
        decode_c10_device_type(std::to_underlying(tensor.device().type()));
    ::fixy::Option<crucible::Layout> layout = decode_c10_layout(std::to_underlying(tensor.layout()));
    const bool is_each_enumeration_named = dtype.is_some() && device_type.is_some() && layout.is_some();
    meta.dtype = std::move(dtype).value_or(crucible::ScalarType::Undefined);
    meta.device_type = std::move(device_type).value_or(crucible::DeviceType::CPU);
    // c10::DeviceIndex is already int8_t (c10/core/Device.h).  The record
    // keeps one index for the host, so each stored device is the device that
    // device_of gives.
    meta.device_idx = crucible::normalize_device_index(
        meta.device_type, tensor.device().has_index() ? tensor.device().index() : int8_t{-1});
    meta.layout = std::move(layout).value_or(crucible::Layout::Strided);

    // -- Extended fields (autograd + storage) --------------------------

    meta.requires_grad = tensor.requires_grad();

    uint8_t flags = 0;
    if (tensor.is_leaf()) flags |= crucible::meta_flags::IS_LEAF;
    if (strided && tensor.is_contiguous()) flags |= crucible::meta_flags::IS_CONTIGUOUS;
    if (tensor.is_neg()) flags |= crucible::meta_flags::IS_NEG;
    if (tensor.is_conj()) flags |= crucible::meta_flags::IS_CONJ;

    auto* impl = tensor.unsafeGetTensorImpl();
    auto* autograd_meta = impl->autograd_meta();
    if (autograd_meta) {
        auto* node = torch::autograd::impl::grad_fn_unsafe(tensor);
        if (node) {
            flags |= crucible::meta_flags::HAS_GRAD_FN;
            const auto& grad_fn_name = node->name();
            // Derived from an autograd node name, which is local to one
            // process, so it carries hash_family::FamilyB and must never key
            // anything that outlives the run.
            meta.grad_fn_hash = ::crucible::grad_fn_hash(
                fnv1a_text(::fixy::mint_foreign_text(grad_fn_name.data(), grad_fn_name.size()).characters()));
        }
        meta.output_nr = static_cast<uint8_t>(torch::autograd::impl::get_autograd_meta(tensor)->output_nr_ & 0xFF);
    }

    // The autograd is_view_ flag catches expand, as_strided and narrow, which
    // share a storage base at offset zero. The two checks after it catch a
    // view with no autograd metadata.
    if (autograd_meta && static_cast<torch::autograd::AutogradMeta*>(autograd_meta)->is_view_)
        flags |= crucible::meta_flags::IS_VIEW;

    if (strided && impl->has_storage()) {
        const c10::DataPtr& storage_data = impl->storage().data_ptr();
        auto* storage_base = storage_data.get();
        if (storage_base != nullptr && tensor.data_ptr() != static_cast<char*>(storage_base)) {
            flags |= crucible::meta_flags::IS_VIEW;
        }
        meta.storage_nbytes = impl->storage().nbytes();
        // A storage that the generation allocator did not make has an
        // unknown generation in the period of the read, and the build keys
        // it on its address in that period.
        meta.storage_generation = generation_of(storage_data);
    }
    if (strided && tensor.storage_offset() != 0) {
        flags |= crucible::meta_flags::IS_VIEW;
        meta.storage_offset = tensor.storage_offset();
    }

    meta.flags = flags;

    // c10's version counter is already uint32_t (TensorImpl.h), so a
    // mask-and-narrow would be a useless cast. Assert the width instead: a
    // future c10 that widens it fails the build rather than truncating a
    // version into a false match.
    static_assert(std::is_same_v<decltype(impl->version_counter().current_version()), uint32_t>,
                  "c10 version counter width drifted — re-check the TensorMeta.version narrowing");
    meta.version = impl->version_counter().current_version();
    return is_each_enumeration_named;
}

// Writes the meta of the tensor into meta, each byte of it, and gives false
// for a tensor that a meta cannot describe.
[[nodiscard]] inline bool fill_meta(crucible::TensorMeta& meta, const at::Tensor& tensor) {
    meta = {};  // zero-init (InitSafe -- NSDMI defaults)
    return fill_zeroed_meta(meta, tensor);
}

// The meta of the tensor as a value.  For a tensor that a meta cannot
// describe, the function sets description to refused, and it leaves
// description as it is for each other tensor.  The value door of a slot
// (build_at of FixedArray) builds the slot from this result in its place, so
// the call writes the meta straight into the slot.  The function stays out
// of line, so the zero fill of the meta stays in it and out of each kernel
// that records an operation.
[[nodiscard, gnu::noinline]] inline crucible::TensorMeta make_described_meta(const at::Tensor& tensor,
                                                                             TensorDescription& description) noexcept {
    crucible::TensorMeta meta{};
    if (!fill_zeroed_meta(meta, tensor)) description = TensorDescription::refused;
    return meta;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// The backward window
//
// The depth of the backward window this thread is inside. CrucibleNative wraps
// Tensor.backward and counts up on entry and down on exit, so a non-zero depth
// says that every operation arriving here belongs to a backward pass. It is
// the one phase signal the recording paths cannot read off the operation
// itself, and derive_training_phase() below is the only reader.
//
// A counter rather than a flag, because a double backward pass nests one
// window inside another, and the inner exit must not end the outer window.
//
// Thread-local. A recording session holds the autograd engine on the thread
// that called backward(), so the window and the operations it produces are on
// one thread. A caller that reaches the engine another way runs backward
// operations on a worker thread, where this counter reads zero — and the
// producer question in recording_vigil() turns that thread away before any
// phase is derived, so the phase it would have derived reaches no trace.
// That phase would be FORWARD, a false label for a backward operation.  The
// defect is latent: only the producer thread records.  A recording path that
// admits a second thread must give the window to that thread first.
//
// One definition for both recording paths: two copies would let them write
// different phase bits into one trace. Zero-initialized, so there is no
// initializer to order and no guard variable on the path that reads it.
//
// The counter is process-wide (CRUCIBLE_PROCESS_WIDE): each thread has one
// counter for the process, also when a second shared library compiles this
// header.  Only libcrucible_dispatch.so compiles it today, and its C entries
// crucible_dispatch_backward_enter and crucible_dispatch_backward_exit write
// it.  A thread_local of a shared library takes the call of the TLS resolver
// with each visibility, so the marker adds no call to the record of an op.
// =====================================================================

CRUCIBLE_PROCESS_WIDE inline thread_local uint32_t backward_depth = 0;

// =====================================================================
// Training phase
//
// Derived from what the runtime is doing when the operation arrives, and
// packed into bits 2 and 3 of op_flags.
//
// The backward window is asked first. It is the most specific of the three
// signals, and an operation inside a backward window belongs to that window
// whatever else is true of it.
//
// The optimizer is read off two facts together. A `_foreach_` operator is one
// of the fused multi-tensor kernels the optimizers of this runtime are built
// on, and gradients are off across an optimizer step. Either fact alone is
// weaker: a model can call a `_foreach_` operator in its own forward pass
// under grad, and grad is off across an inference forward pass too.
//
// That conjunction is narrow, and what it costs is named here rather than left
// for a reader to find. An optimizer step also emits operations that are not
// `_foreach_`, and a step built on single-tensor kernels emits none at all.
// Those operations derive FORWARD. So OPTIMIZER on an operation is evidence
// and FORWARD on one is the absence of evidence.
//
// Measured over one recorded period of a small transformer under SGD with
// momentum: on cuda:0 the step is three fused calls and all three carry the
// label, and on the host the same step is 87 single-tensor calls and none of
// them does. The runtime picks between the two forms per device inside the
// step, so which one a step takes is not a property this file can read.
//
// Narrow and derived is still the better trade. These bits are a label: the
// content hash folds the schema hash, the tensor metadata and the scalars and
// never reads op_flags, so no phase can move a region's identity, its cache
// key or a replay guard. Only the readers of the trace use them. Serialize.h
// and the trace export of vessel_api.cpp write them to the file. A label is
// worth having only if it says the same thing about the same computation
// every time, and a phase the caller set by hand said whatever that caller
// had remembered to call. Two
// loops over one model labelled one computation two ways, and the label of the
// loop that skipped a call was silently FORWARD throughout.
// =====================================================================

// `grad_enabled` arrives as an argument rather than being read here, because
// the unboxed caller needs the same bit for the GRAD_ENABLED flag. One read
// serves both, and the two cannot report different answers for one operation.
[[nodiscard]] inline crucible::TrainingPhase derive_training_phase(bool is_foreach, bool grad_enabled) noexcept {
    if (backward_depth != 0) return crucible::TrainingPhase::BACKWARD;
    if (is_foreach && !grad_enabled) return crucible::TrainingPhase::OPTIMIZER;
    return crucible::TrainingPhase::FORWARD;
}

// True for the fused multi-tensor operators. 452 names in aten_op_table.h carry
// this infix: the `aten::_foreach_*` family, and the two
// `aten::_amp_foreach_non_finite_check_and_unscale` overloads. The second pair
// belongs to the gradient scaler rather than to a fused update, and it runs
// inside an optimizer step with gradients off, so the phase it derives is the
// one it should have.
[[nodiscard]] constexpr bool is_foreach_op_name(::fixy::TextView name) noexcept {
    return ::fixy::find(name, ::fixy::TextView{"_foreach_"}).is_some();
}

// Everything below DispatchKey::Crucible, which is the backend that computes
// the operation. The constructor is constexpr, so this costs no initializer.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the dispatch keys of PyTorch name the backends below the recorder")
inline constexpr c10::DispatchKeySet kAfterCrucibleKeyset =
    c10::DispatchKeySet(c10::DispatchKeySet::FULL_AFTER, c10::DispatchKey::Crucible);
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// The name an operator goes into the SchemaTable under
//
// "aten::add" with overload "Tensor" becomes "aten::add.Tensor", which is the
// spelling the boxed path registers and the spelling the schema hash was taken
// over. register.cpp registers an operator under the same name minus the
// namespace, because the name Library::impl takes is relative to the namespace
// of its block, so both come from this one builder.
// =====================================================================

[[nodiscard]] consteval size_t longest_qualified_op_name() {
    size_t longest = 0;
    for_each_aten_op_row([&longest](const OpEntry& row) {
        size_t length = row.name.size();
        if (row.overload.size() != 0) length += 1 + row.overload.size();
        longest = ::fixy::max(longest, length);
    });
    return longest;
}

// One past the longest name the table holds, so the buffer is exact: it never
// truncates and it never allocates.  The last place holds the zero.
inline constexpr size_t kQualifiedOpNameCapacity = longest_qualified_op_name() + 1;

class QualifiedOpName {
public:
    // The qualified name of a row of the table.
    explicit QualifiedOpName(const OpEntry& row) noexcept : QualifiedOpName{row.name, row.overload} {}

    // The name, then a dot and the overload when the overload is named.  The
    // name of a row, or a part of it, fits the capacity.
    QualifiedOpName(::fixy::TextView name, ::fixy::TextView overload) noexcept {
        for (const char character : name)
            text_.append(character).expect("the capacity holds the longest name");
        if (overload.size() != 0) {
            text_.append('.').expect("the capacity holds the longest name");
            for (const char character : overload)
                text_.append(character).expect("the capacity holds the longest name");
        }
        text_.append('\0').expect("the capacity holds the longest name and its zero");
    }

    // The name as a C text.  The constructor puts one zero after the name.
    [[nodiscard]] ::fixy::CText c_text() const noexcept {
        return ::fixy::mint_c_text(text_.view()).expect("the constructor puts one zero after the name");
    }

    // The name as the C text that Library::impl of PyTorch takes.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: Library::impl of PyTorch takes the operator name as a C text")
    [[nodiscard]] const char* c_str() const noexcept { return ::foundation::core::foreign_c_str(c_text()); }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

private:
    ::fixy::FixedText<kQualifiedOpNameCapacity> text_{};
};

// =====================================================================
// What each argument shape contributes
//
// The boxed fallback walks the IValue stack asking six questions: is this a
// tensor, a tensor list, an optional-tensor list, an int or double or bool, an
// int list, a double list. An element that answers no to all six contributes
// nothing to the entry. This enum is that decision as a total function of the
// declared C++ type, so the mapping is one table a reader checks against the
// fallback rather than an overload set whose answer emerges from partial
// ordering.
//
// The boxing rules the table encodes, from ATen/core/ivalue.h:
//   * ScalarType, Layout and MemoryFormat box into the Int tag (:991-1005),
//     so each contributes its ordinal with the tag INT.
//   * Device, Generator, Storage, Stream and a string carry tags of their
//     own, so each contributes nothing.
//   * A Scalar boxes by its payload: Int, Double or Bool contributes, and
//     ComplexDouble does not.  An ArrayRef<Scalar> boxes into a list of
//     numbers, and each element contributes as a Scalar does.
//   * A concrete SymInt boxes into Int and contributes. A symbolic one boxes
//     into the SymInt tag and does not.
//   * An absent optional boxes into None, whatever its payload, and
//     contributes one slot with the value zero and the tag NONE.  So does a
//     None element of a list of optional tensors.
// Each slot has the tag of its boxed value (ScalarType2 of TraceRing.h).
// =====================================================================

enum class ArgumentKind : uint8_t {
    // Contributes no field of the entry.
    Ignored,
    // One metadata slot per defined tensor.
    Tensor,
    TensorList,
    OptionalTensorList,
    // One scalar slot, or one per element for the list kinds.
    Bool,
    Integer,
    Enumerator,
    Double,
    Scalar,
    SymInt,
    IntegerList,
    DoubleList,
    SymIntList,
    ScalarList,
    // An optional or an OptionalArrayRef. Contributes what its payload
    // contributes when it holds one.
    Optional,
};

// The optional types and the parameter types below are the types that the
// operators of PyTorch declare, so the classification is in an ABI region.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the classification names the parameter types of the operators of PyTorch")
template <class T>
inline constexpr bool is_std_optional_v = false;
template <class T>
inline constexpr bool is_std_optional_v<std::optional<T>> = true;

template <class T>
inline constexpr bool is_optional_array_ref_v = false;
template <class T>
inline constexpr bool is_optional_array_ref_v<c10::OptionalArrayRef<T>> = true;

// The payload of an optional argument, with the reference and the const
// stripped, so the classification of the payload is one recursive step.
template <class T>
struct optional_payload {
    using type = T;
};
template <class T>
struct optional_payload<std::optional<T>> {
    using type = T;
};
template <class T>
struct optional_payload<c10::OptionalArrayRef<T>> {
    using type = c10::ArrayRef<T>;
};
template <class T>
using optional_payload_t = std::remove_cvref_t<typename optional_payload<std::remove_cvref_t<T>>::type>;

template <class T>
[[nodiscard]] consteval ArgumentKind argument_kind_of() noexcept {
    using Arg = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Arg, at::Tensor>)
        return ArgumentKind::Tensor;
    else if constexpr (std::is_same_v<Arg, at::TensorList>)
        return ArgumentKind::TensorList;
    else if constexpr (std::is_same_v<Arg, at::ITensorListRef>)
        return ArgumentKind::TensorList;
    else if constexpr (std::is_same_v<Arg, c10::List<std::optional<at::Tensor>>>)
        return ArgumentKind::OptionalTensorList;
    else if constexpr (is_std_optional_v<Arg> || is_optional_array_ref_v<Arg>)
        return ArgumentKind::Optional;
    else if constexpr (std::is_same_v<Arg, bool>)
        return ArgumentKind::Bool;
    else if constexpr (std::is_same_v<Arg, double>)
        return ArgumentKind::Double;
    else if constexpr (std::is_same_v<Arg, at::Scalar>)
        return ArgumentKind::Scalar;
    else if constexpr (std::is_same_v<Arg, c10::SymInt>)
        return ArgumentKind::SymInt;
    else if constexpr (std::is_same_v<Arg, at::IntArrayRef>)
        return ArgumentKind::IntegerList;
    else if constexpr (std::is_same_v<Arg, c10::SymIntArrayRef>)
        return ArgumentKind::SymIntList;
    else if constexpr (std::is_same_v<Arg, at::ArrayRef<double>>)
        return ArgumentKind::DoubleList;
    else if constexpr (std::is_same_v<Arg, at::ArrayRef<at::Scalar>>)
        return ArgumentKind::ScalarList;
    else if constexpr (std::is_enum_v<Arg>)
        return ArgumentKind::Enumerator;
    else if constexpr (std::integral<Arg>)
        return ArgumentKind::Integer;
    else
        return ArgumentKind::Ignored;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The masks these two produce are asserted against the masks the generator
// read out of the schema strings, so a tensor shape this table does not name
// surfaces as a failed assertion on the operator that introduced it rather
// than as a tensor nobody recorded.
template <class T>
inline constexpr bool is_tensor_argument_v = argument_kind_of<T>() == ArgumentKind::Tensor
                                          || (argument_kind_of<T>() == ArgumentKind::Optional
                                              && argument_kind_of<optional_payload_t<T>>() == ArgumentKind::Tensor);

template <class T>
inline constexpr bool is_tensor_list_argument_v =
    argument_kind_of<T>() == ArgumentKind::TensorList || argument_kind_of<T>() == ArgumentKind::OptionalTensorList;

// Bit i is set when argument i is of the named shape. Argument positions are
// parameter positions, which is the correspondence the table's masks were
// computed against.  The fold reads the arguments in order, so an operator
// with no argument gives the empty mask.
template <class... Args>
[[nodiscard]] consteval uint32_t tensor_argument_mask() noexcept {
    uint32_t mask = 0;
    [[maybe_unused]] uint32_t position = 0;
    ((mask |= (is_tensor_argument_v<Args> ? uint32_t{1} << position : 0u), ++position), ...);
    return mask;
}

template <class... Args>
[[nodiscard]] consteval uint32_t tensor_list_argument_mask() noexcept {
    uint32_t mask = 0;
    [[maybe_unused]] uint32_t position = 0;
    ((mask |= (is_tensor_list_argument_v<Args> ? uint32_t{1} << position : 0u), ++position), ...);
    return mask;
}

// The count that stands for more scalars than the inline slots hold.
inline constexpr uint32_t kMoreThanInlineScalars = uint32_t{kInlineScalars} + 1;

// True for a type that fills one scalar slot as an argument: a bool, an
// integer, an enumerator, a double, a Scalar or a SymInt.  As a result, such
// a type boxes into one scalar of the stack.
template <class T>
inline constexpr bool is_scalar_kind_v =
    argument_kind_of<T>() == ArgumentKind::Bool || argument_kind_of<T>() == ArgumentKind::Integer
    || argument_kind_of<T>() == ArgumentKind::Enumerator || argument_kind_of<T>() == ArgumentKind::Double
    || argument_kind_of<T>() == ArgumentKind::Scalar || argument_kind_of<T>() == ArgumentKind::SymInt;

// The scalar slots one argument can fill, with kMoreThanInlineScalars for a
// list, whose length has no compile-time bound.  An absent optional fills
// one slot, so an optional fills at least one.
template <class T>
[[nodiscard]] consteval uint32_t scalar_bound_of() noexcept {
    constexpr ArgumentKind kind = argument_kind_of<T>();
    if constexpr (kind == ArgumentKind::Optional)
        return ::fixy::max(uint32_t{1}, scalar_bound_of<optional_payload_t<T>>());
    else if constexpr (kind == ArgumentKind::OptionalTensorList)
        return kMoreThanInlineScalars;
    else if constexpr (is_scalar_kind_v<T>)
        return 1;
    else if constexpr (kind == ArgumentKind::IntegerList || kind == ArgumentKind::DoubleList
                       || kind == ArgumentKind::SymIntList || kind == ArgumentKind::ScalarList)
        return kMoreThanInlineScalars;
    else
        return 0;
}

// The scalar slots the arguments can fill.  Each term is at most
// kMoreThanInlineScalars, so the sum of the 32 arguments of an operator
// cannot overflow.
template <class... Args>
inline constexpr uint32_t scalar_bound_v = (uint32_t{0} + ... + scalar_bound_of<Args>());

// The role of the slots of an argument by its declared type (the role of a
// scalar argument, entry_slots.h): data for a double, a Scalar and a list of
// them, the role of the payload for an optional, and structural for each other
// type.  A double is the type float of the schema, and an at::Scalar is the
// type Scalar, so the boxed fallback reads the same role from the schema.
template <class T>
[[nodiscard]] consteval crucible::ScalarRole role_of_kind() noexcept {
    constexpr ArgumentKind kind = argument_kind_of<T>();
    if constexpr (kind == ArgumentKind::Optional)
        return role_of_kind<optional_payload_t<T>>();
    else if constexpr (kind == ArgumentKind::Double || kind == ArgumentKind::Scalar || kind == ArgumentKind::DoubleList
                       || kind == ArgumentKind::ScalarList)
        return crucible::ScalarRole::DATA;
    else
        return crucible::ScalarRole::STRUCTURAL;
}

// The role of the argument at Position of an operator: the role of its type,
// unless kStructuralScalars names the argument.  An argument past
// kMaxSchemaArguments is structural, as on the boxed path.
template <std::size_t Position, class T>
[[nodiscard]] consteval crucible::ScalarRole argument_role(crucible::SchemaHash schema_hash) noexcept {
    if constexpr (Position >= kMaxSchemaArguments) {
        return crucible::ScalarRole::STRUCTURAL;
    } else {
        const ArgumentPosition position = argument_position_at(Position).expect("the test above bounds Position");
        if (is_structural_scalar(schema_hash, position)) return crucible::ScalarRole::STRUCTURAL;
        return role_of_kind<T>();
    }
}

// ---------------------------------------------------------------------
// The table classifies every parameter type the fork's 3110 operators use.
// The list is the measured inventory of `using schema` parameter types, in
// descending order of use, so a fork that introduces a shape the table sends
// to Ignored is caught by the mask agreement rather than by this block.  Each
// type is a type of PyTorch, std::array<bool, 3> too, so the block is in an
// ABI region.

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the witnesses name the parameter types of the operators of PyTorch")
namespace detail::argument_kind_witnesses {

static_assert(argument_kind_of<const at::Tensor&>() == ArgumentKind::Tensor);
static_assert(argument_kind_of<at::Tensor&>() == ArgumentKind::Tensor);
static_assert(argument_kind_of<bool>() == ArgumentKind::Bool);
static_assert(argument_kind_of<int64_t>() == ArgumentKind::Integer);
static_assert(argument_kind_of<at::TensorList>() == ArgumentKind::TensorList);
static_assert(argument_kind_of<const at::Scalar&>() == ArgumentKind::Scalar);
static_assert(argument_kind_of<c10::SymIntArrayRef>() == ArgumentKind::SymIntList);
static_assert(argument_kind_of<const std::optional<at::Tensor>&>() == ArgumentKind::Optional);
static_assert(argument_kind_of<at::IntArrayRef>() == ArgumentKind::IntegerList);
static_assert(argument_kind_of<double>() == ArgumentKind::Double);
static_assert(argument_kind_of<c10::SymInt>() == ArgumentKind::SymInt);
static_assert(argument_kind_of<std::optional<at::ScalarType>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<std::optional<double>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<std::optional<bool>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<std::optional<at::Layout>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<std::optional<at::Device>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<std::optional<int64_t>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<std::optional<at::Generator>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<c10::string_view>() == ArgumentKind::Ignored);
static_assert(argument_kind_of<std::optional<c10::string_view>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<at::OptionalIntArrayRef>() == ArgumentKind::Optional);
static_assert(argument_kind_of<std::optional<at::MemoryFormat>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<at::OptionalSymIntArrayRef>() == ArgumentKind::Optional);
static_assert(argument_kind_of<std::optional<c10::SymInt>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<at::ScalarType>() == ArgumentKind::Enumerator);
static_assert(argument_kind_of<const std::optional<at::Scalar>&>() == ArgumentKind::Optional);
static_assert(argument_kind_of<at::ArrayRef<at::Scalar>>() == ArgumentKind::ScalarList);
static_assert(argument_kind_of<std::optional<at::ArrayRef<double>>>() == ArgumentKind::Optional);
static_assert(argument_kind_of<std::array<bool, 3>>() == ArgumentKind::Ignored);
static_assert(argument_kind_of<const at::ITensorListRef&>() == ArgumentKind::TensorList);
static_assert(argument_kind_of<const c10::List<std::optional<at::Tensor>>&>() == ArgumentKind::OptionalTensorList);
static_assert(argument_kind_of<at::Storage>() == ArgumentKind::Ignored);
static_assert(argument_kind_of<at::DeviceIndex>() == ArgumentKind::Integer);
static_assert(argument_kind_of<at::Device>() == ArgumentKind::Ignored);
static_assert(argument_kind_of<at::MemoryFormat>() == ArgumentKind::Enumerator);
static_assert(argument_kind_of<at::Layout>() == ArgumentKind::Enumerator);
static_assert(argument_kind_of<at::Stream>() == ArgumentKind::Ignored);
static_assert(argument_kind_of<at::ArrayRef<double>>() == ArgumentKind::DoubleList);

// The optional payload resolves to what it wraps, which is what makes the
// recursive step below terminate in one hop.
static_assert(argument_kind_of<optional_payload_t<const std::optional<at::Tensor>&>>() == ArgumentKind::Tensor);
static_assert(argument_kind_of<optional_payload_t<at::OptionalIntArrayRef>>() == ArgumentKind::IntegerList);
static_assert(argument_kind_of<optional_payload_t<at::OptionalSymIntArrayRef>>() == ArgumentKind::SymIntList);
static_assert(argument_kind_of<optional_payload_t<std::optional<at::ArrayRef<double>>>>() == ArgumentKind::DoubleList);
static_assert(argument_kind_of<optional_payload_t<std::optional<at::ScalarType>>>() == ArgumentKind::Enumerator);

// The tensor masks follow the kinds, including through an optional.
static_assert(is_tensor_argument_v<const std::optional<at::Tensor>&>);
static_assert(!is_tensor_argument_v<std::optional<at::ScalarType>>);
static_assert(!is_tensor_argument_v<at::TensorList>);
static_assert(is_tensor_list_argument_v<at::TensorList>);
static_assert(is_tensor_list_argument_v<const c10::List<std::optional<at::Tensor>>&>);

}  // namespace detail::argument_kind_witnesses
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// The signature, and the cross-check against PyTorch's codegen
// =====================================================================

// The declared type of Op::call: a function type, which the partial
// specialization of RecordKernel takes apart into a parameter pack.  Each
// operator struct declares one call, so the name is no overload set.
template <class Op>
using call_signature = decltype(Op::call);

// The first agreement: the declared call and the typedef PyTorch's generator
// writes describe one function. A const and a non-const out= slot, an
// ITensorListRef and a tuple return each keep their type in both.
template <class Op>
inline constexpr bool call_agrees_with_codegen_v = std::is_same_v<call_signature<Op>, typename Op::schema>;

// =====================================================================
// How many metadata slots an operation can fill
//
// The shared limit is MAX_INLINE_METAS. Most operators cannot come near it:
// an operator with no tensor-list argument fills exactly one input slot per
// tensor argument, which the table's masks already state, and a result that
// is neither a vector nor a list fills a known number of output slots. When
// both bounds are exact the Recording has their sum of slots, so `mm` carries
// three slots rather than thirty-two.
//
// The bound is never smaller than what the operation can produce, so
// truncation behaviour is unchanged: where the bound is exact, neither path
// truncates, and where it is not, both stop at MAX_INLINE_METAS.
//
// Where the bound is exact, the kernel puts the slots on its own stack, and
// the operation writes almost each slot.  Where it is not, a list of
// MAX_INLINE_METAS slots on the stack would fill 5632 bytes for each call, and
// the operation writes few of them.  So such a kernel takes its slots from a
// frame of the record stack of the Vigil, as the boxed fallback does.
// =====================================================================

// MAX_INLINE_METAS stands for "no smaller bound", which makes the clamp below
// a plain minimum.  A scalar result fills one slot with a meta of rank zero,
// and each other result that is not a tensor fills none.  The result types
// are the types that the operators of PyTorch declare, a std::vector and a
// std::tuple too, so the bounds are in an ABI region.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the bounds name the result types of the operators of PyTorch")
template <class T>
inline constexpr uint32_t result_meta_bound_v = is_scalar_kind_v<T> ? 1 : 0;
template <>
inline constexpr uint32_t result_meta_bound_v<at::Tensor> = 1;
template <class T>
inline constexpr uint32_t result_meta_bound_v<std::vector<T>> = MAX_INLINE_METAS;
template <class T>
inline constexpr uint32_t result_meta_bound_v<c10::ArrayRef<T>> = MAX_INLINE_METAS;
template <class... Ts>
inline constexpr uint32_t result_meta_bound_v<std::tuple<Ts...>> =
    ::fixy::min(MAX_INLINE_METAS, (uint32_t{0} + ... + result_meta_bound_v<std::remove_cvref_t<Ts>>));
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// Recording one operation
// =====================================================================

// MayOverflow is true when the operation can carry more scalars than the
// inline slots or more input tensors than Capacity.  Then the Recording
// folds each scalar and each input tensor past Capacity into the overflow
// digest of entry_slots.h.  An operation that cannot overflow keeps the
// state and the code of a Recording with no digest.
//
// The Recording writes the records into slots that its caller gives: a list
// on the stack of the caller, or a frame of the record stack of the Vigil.  A
// slot of a frame holds the record of an earlier operation until the
// Recording writes it, and each reader reads only the slots that the
// Recording wrote.  fill_meta and scalar_result_meta write each byte of a
// slot, so the lanes of a record past its rank stay zero.
//
// Where the slots of a Recording live: in the window of a foreign frame of the
// record stack or of the list of the cold path, which the caller lends as the
// ViewMut of the window (SlotsLent), or in the list that the Recording holds
// inline (SlotsOwned).  An operation with an exact bound records into a
// Recording of its own, so its list gives out no View.  Each caller of a lent
// window erases its brand (erase_brand), so each family of the record has one
// instance for its lent windows.
template <class Brand>
struct SlotsLent final {
    template <uint32_t Count>
    using storage = ::fixy::ViewMut<crucible::TensorMeta, Brand, Count>;
};
struct SlotsOwned final {
    template <uint32_t Count>
    using storage = ::fixy::FixedArray<crucible::TensorMeta, Count>;
};

// Lends the slot at a proven index of the run of a frame to body in place,
// and gives the result of body.  The index is below the extent of the run, so
// the read of the element finds it, and the second arm never runs.
template <class Brand, std::size_t Count, class Body>
CRUCIBLE_INLINE decltype(auto) with_frame_slot(::fixy::ViewMut<crucible::TensorMeta, Brand, Count>& run,
                                               ::fixy::Index<Count> slot, Body&& body) noexcept {
    using Given = std::invoke_result_t<Body&&, crucible::TensorMeta&>;
    return run.get_mut(slot.raw())
        .match(
            [&body](::fixy::RefMut<crucible::TensorMeta, Brand> element) noexcept -> Given {
                return static_cast<Body&&>(body)(*element);
            },
            []() noexcept -> Given { ::fixy::unreachable(); });
}

template <uint32_t Capacity, bool MayOverflow, class Home>
struct Recording {
    static_assert(Capacity <= MAX_INLINE_METAS, "a Recording must not admit more metadata than either path records");

    // An array of the base holds at least one element, so an operation with
    // no tensor has one slot that it never writes.  The counts stay at most
    // Capacity.
    static constexpr uint32_t kSlots = Capacity > 0 ? Capacity : 1;

    // True when the caller lends the slots.
    static constexpr bool kIsLent = !std::is_same_v<Home, SlotsOwned>;

    // The slots of the records: the ViewMut of the window that a caller
    // lends, or the list that a Recording of the home SlotsOwned holds.
    using Storage = typename Home::template storage<kSlots>;

    explicit Recording(Storage&& slots) noexcept
        requires kIsLent
        : metas{std::move(slots)} {}
    Recording() noexcept
        requires(!kIsLent)
        : metas{} {}

    Storage metas;
    MetaCount counts{};
    ScalarArgs<MayOverflow> scalars{};
    [[no_unique_address]] typename input_overflow_of<MayOverflow>::type overflow{};
    // refused when fill_meta refuses a recorded tensor: its rank or an extent
    // is past the bound of a meta, or an enumeration names no crucible
    // enumerator.  append_trace_entry then records nothing.
    TensorDescription each_tensor_description = TensorDescription::described;

    // The three helpers below are always inline, so the optimizer sees each
    // read of the counts, and each check of an index or a window folds.
    //
    // Writes meta into the slot at position.  The slots of a lent window are
    // the ViewMut that the caller lends for the record of one op, and the list
    // of the home SlotsOwned takes the value through its value door.  Each
    // caller tests the position against Capacity first, so the optimizer
    // removes the check.
    CRUCIBLE_INLINE void set_slot(uint32_t position, crucible::TensorMeta const& meta) noexcept {
        const ::fixy::Index<kSlots> slot =
            ::fixy::Index<kSlots>::check(position).expect("a count below Capacity names a slot");
        if constexpr (kIsLent) {
            with_frame_slot(metas, slot, [&meta](crucible::TensorMeta& held) noexcept { held = meta; });
        } else {
            metas.set(slot, meta);
        }
    }

    // The slots for a read.
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::View<const crucible::TensorMeta> read_slots() const& noexcept {
        return ::fixy::View<const crucible::TensorMeta>{metas.view()};
    }

    // The records of the input tensors.  add_input keeps the count of the
    // inputs at most Capacity.
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::View<const crucible::TensorMeta> input_records() const& noexcept {
        const uint32_t count = counts.inputs;
        [[assume(count <= Capacity)]];
        return read_slots().window(0, count).expect("the inputs fit the slots");
    }

    // The records of the operation: the inputs and then the outputs.  The
    // adds keep the total count at most Capacity.
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::View<const crucible::TensorMeta> records() const& noexcept {
        const uint32_t count = counts.total();
        [[assume(count <= Capacity)]];
        return read_slots().window(0, count).expect("the records fit the slots");
    }

    // The slot at a position below Capacity.  Each caller tests the position
    // against Capacity first, so the optimizer removes the check.
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::Index<kSlots> slot_at(uint32_t position) const noexcept {
        return ::fixy::Index<kSlots>::check(position).expect("a count below Capacity names a slot");
    }

    // The three adds below take the tensors of PyTorch.  Each writes the meta
    // of a tensor into its slot in place, and sets each_tensor_description to
    // refused when fill_meta refuses the tensor.  A lent slot is an element of
    // the ViewMut of a window, and fill_meta writes it through
    // with_frame_slot.  The list of the home SlotsOwned builds its slot
    // through its value door build_at, which lends no borrow of the list, so
    // the tensor and the description go to make_described_meta as arguments of
    // the door.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a Recording takes the tensors of an operator of PyTorch")
    void add_input(const at::Tensor& tensor) {
        if (tensor.defined() && counts.inputs < Capacity) {
            if constexpr (kIsLent) {
                if (!with_frame_slot(metas, slot_at(counts.inputs), [&tensor](crucible::TensorMeta& slot) noexcept {
                        return fill_meta(slot, tensor);
                    })) {
                    each_tensor_description = TensorDescription::refused;
                }
            } else {
                metas.build_at(slot_at(counts.inputs), make_described_meta, tensor, each_tensor_description);
            }
            counts.inputs++;
        } else if constexpr (MayOverflow) {
            if (tensor.defined()) add_overflow_input(tensor);
        }
    }

    // An input past Capacity, which the overflow digest folds.  fill_meta
    // must describe it, as it must describe an inline input.
    [[gnu::noinline]] void add_overflow_input(const at::Tensor& tensor)
        requires MayOverflow
    {
        crucible::TensorMeta meta{};
        if (!fill_meta(meta, tensor)) each_tensor_description = TensorDescription::refused;
        overflow.fold(meta);
    }

    // Outputs follow the inputs in one array, which is the order the MetaLog
    // append and every reader of it expect.
    void add_output(const at::Tensor& tensor) {
        if (tensor.defined() && counts.total() < Capacity) {
            if constexpr (kIsLent) {
                if (!with_frame_slot(metas, slot_at(counts.total()), [&tensor](crucible::TensorMeta& slot) noexcept {
                        return fill_meta(slot, tensor);
                    })) {
                    each_tensor_description = TensorDescription::refused;
                }
            } else {
                metas.build_at(slot_at(counts.total()), make_described_meta, tensor, each_tensor_description);
            }
            counts.outputs++;
        }
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    // A result that is one scalar, as the meta of scalar_result_meta.
    void add_scalar_output(crucible::ScalarType dtype) {
        if (counts.total() < Capacity) {
            set_slot(counts.total(), scalar_result_meta(dtype));
            counts.outputs++;
        }
    }
};

// The three functions below read the arguments and the results of an operator
// through the API of PyTorch, so they are in an ABI region.
//
// One argument, classified by the table above and recorded accordingly. An
// Optional recurses once into its payload, which the payload witnesses prove
// terminates.  Role is the role of each slot of the argument (argument_role),
// and an absent optional fills a structural slot.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the record reads the arguments and the results of an operator of PyTorch")
template <crucible::ScalarRole Role, uint32_t Capacity, bool MayOverflow, class Home, class T>
void record_argument(Recording<Capacity, MayOverflow, Home>& recording, const T& value) {
    constexpr ArgumentKind kind = argument_kind_of<T>();

    if constexpr (kind == ArgumentKind::Tensor) {
        recording.add_input(value);
    } else if constexpr (kind == ArgumentKind::TensorList) {
        // IListRef dereferences to const Tensor& for Tensor through its
        // ivalue_to_const_ref_overload_return specialization, and ArrayRef
        // dereferences to the element, so both bind to the list's own storage.
        for (const at::Tensor& tensor : value)
            recording.add_input(tensor);
    } else if constexpr (kind == ArgumentKind::OptionalTensorList) {
        // A c10::List iterator dereferences to a proxy whose conversion
        // returns by value, so a reference would bind to a temporary.
        for (const auto element : value) {
            const std::optional<at::Tensor> tensor = element;
            if (tensor.has_value()) {
                recording.add_input(*tensor);
            } else {
                recording.scalars.push_absent();
            }
        }
    } else if constexpr (kind == ArgumentKind::Optional) {
        if (value.has_value()) {
            record_argument<Role>(recording, *value);
        } else {
            recording.scalars.push_absent();
        }
    } else if constexpr (kind == ArgumentKind::Bool) {
        recording.scalars.push(value ? 1 : 0, crucible::ScalarType2::BOOL, Role);
    } else if constexpr (kind == ArgumentKind::Integer) {
        recording.scalars.push(static_cast<int64_t>(value), crucible::ScalarType2::INT, Role);
    } else if constexpr (kind == ArgumentKind::Enumerator) {
        recording.scalars.push(static_cast<int64_t>(std::to_underlying(value)), crucible::ScalarType2::INT, Role);
    } else if constexpr (kind == ArgumentKind::Double) {
        recording.scalars.push(::fixy::bit_cast<int64_t>(value), crucible::ScalarType2::FLOAT, Role);
    } else if constexpr (kind == ArgumentKind::Scalar) {
        if (value.isFloatingPoint()) {
            recording.scalars.push(::fixy::bit_cast<int64_t>(value.toDouble()), crucible::ScalarType2::FLOAT, Role);
        } else if (value.isBoolean()) {
            recording.scalars.push(value.toBool() ? 1 : 0, crucible::ScalarType2::BOOL, Role);
        } else if (value.isIntegral(/*includeBool=*/false)) {
            recording.scalars.push(value.toLong(), crucible::ScalarType2::INT, Role);
        }
    } else if constexpr (kind == ArgumentKind::SymInt) {
        if (const auto concrete = value.maybe_as_int(); concrete.has_value())
            recording.scalars.push(*concrete, crucible::ScalarType2::INT, Role);
    } else if constexpr (kind == ArgumentKind::IntegerList) {
        for (const int64_t element : value)
            recording.scalars.push(element, crucible::ScalarType2::INT, Role);
    } else if constexpr (kind == ArgumentKind::DoubleList) {
        for (const double element : value)
            recording.scalars.push(::fixy::bit_cast<int64_t>(element), crucible::ScalarType2::FLOAT, Role);
    } else if constexpr (kind == ArgumentKind::SymIntList) {
        for (const c10::SymInt& element : value)
            record_argument<Role>(recording, element);
    } else if constexpr (kind == ArgumentKind::ScalarList) {
        for (const at::Scalar& element : value)
            record_argument<Role>(recording, element);
    } else {
        static_assert(kind == ArgumentKind::Ignored, "every ArgumentKind must have a branch");
    }
}

// A result that is one scalar.  The boxed fallback reads it off the stack as
// an IValue of the tag Double, ComplexDouble, Bool or Int, and the element
// type of its meta follows the tag.  A symbolic SymInt boxes into a tag of
// its own and gives no meta.
template <uint32_t Capacity, bool MayOverflow, class Home, class T>
void record_scalar_result(Recording<Capacity, MayOverflow, Home>& recording, const T& value) {
    constexpr ArgumentKind kind = argument_kind_of<T>();
    if constexpr (kind == ArgumentKind::Bool) {
        recording.add_scalar_output(crucible::ScalarType::Bool);
    } else if constexpr (kind == ArgumentKind::Integer || kind == ArgumentKind::Enumerator) {
        recording.add_scalar_output(crucible::ScalarType::Long);
    } else if constexpr (kind == ArgumentKind::Double) {
        recording.add_scalar_output(crucible::ScalarType::Double);
    } else if constexpr (kind == ArgumentKind::Scalar) {
        if (value.isFloatingPoint()) {
            recording.add_scalar_output(crucible::ScalarType::Double);
        } else if (value.isComplex()) {
            recording.add_scalar_output(crucible::ScalarType::ComplexDouble);
        } else if (value.isBoolean()) {
            recording.add_scalar_output(crucible::ScalarType::Bool);
        } else if (value.isIntegral(/*includeBool=*/false)) {
            recording.add_scalar_output(crucible::ScalarType::Long);
        }
    } else {
        static_assert(kind == ArgumentKind::SymInt, "every scalar kind must have a branch");
        if (value.maybe_as_int().has_value()) recording.add_scalar_output(crucible::ScalarType::Long);
    }
}

// One result. The boxed fallback reads return values off the stack, where a
// tuple return has already been flattened into one slot per element, so
// destructuring the tuple reaches the same elements in the same order. A
// result that is neither a tensor, a scalar nor a list of tensors
// contributes nothing, which is what the fallback does with one.
template <uint32_t Capacity, bool MayOverflow, class Home, class T>
void record_result(Recording<Capacity, MayOverflow, Home>& recording, const T& result) {
    using Result = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Result, at::Tensor>) {
        recording.add_output(result);
    } else if constexpr (is_scalar_kind_v<Result>) {
        record_scalar_result(recording, result);
    } else if constexpr (result_meta_bound_v<Result> == 0) {
        // Nothing to record. Named rather than left to a trailing else so
        // that a tensor-bearing shape cannot fall through here silently.
    } else if constexpr (requires { std::tuple_size<Result>::value; }) {
        std::apply([&recording](const auto&... element) { (record_result(recording, element), ...); }, result);
    } else {
        for (const at::Tensor& tensor : result)
            recording.add_output(tensor);
    }
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// The binding
//
// Recording touches memory only: TraceRing.h states that appending touches
// memory only, and Vigil.h states that both leaves of dispatch_op are
// memory-only. So the binding carries the empty effect row, which is what the
// strictest pole of every axis already means, and the role that names no axis
// is the one that says it.
//
// PureLinear over PureCopy: both carry the empty row and both are legal in the
// foreground, but the strict Usage pole is linear, the binding is minted once
// per dispatch and consumed once, and PureCopy would grant a duplication
// nothing here performs.
//
// HotFgCtx admits the empty row and nothing else, so the gate rejects a
// binding that declared an effect before it could reach the ring. Asserted for
// the type rather than per operator, because every kernel binds one payload.
// =====================================================================

using RecordingBinding = ::fixy::role::PureLinear<crucible::TraceRing::ValidatedEntryPtr>;

static_assert(::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, RecordingBinding>,
              "the recording binding declares an effect the foreground context does not admit");

static_assert(
    !::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, ::fixy::role::IoFunction<crucible::TraceRing::ValidatedEntryPtr>>,
    "the gate must reject an IO row under the foreground context, or it gates nothing");

static_assert(sizeof(RecordingBinding) == sizeof(crucible::TraceRing::ValidatedEntryPtr),
              "the binding must add no storage to the entry pointer it carries");

// =====================================================================
// The recording gate
//
// Three questions, asked in the order the boxed fallback asks them, and shared
// by every kernel rather than compiled into each of the 3110. A null answer
// means the caller redispatches and records nothing.
//
// The producer-thread question is the one that is not about configuration. The
// ring is single-producer and the recorded operation order is the trace's
// identity: it fixes the region content hash, the memory plan and the replay
// order. This runtime's autograd engine runs a backward pass on a worker
// thread of its own by default, and the Crucible key travels there with the
// captured ThreadLocalState, so this handler fires there for every backward
// operation of a model on an accelerator. A foreign thread executes its
// operation and leaves the ring alone. The trace is then short by that
// thread's operations, which is incomplete and reproducible. Recording them
// would be complete and irreproducible, and content addressing, the memory
// plan and bit-exact replay are built on the reproducibility.
//
// A recording session gives the backward window one producer instead of
// widening this gate. It holds the engine on the thread that calls backward()
// for every window, the replayed ones included, so the window arrives here on
// the producer thread and passes. Replay needs that as much as recording does:
// a replayed dispatch records nothing but it advances the replay cursor, and a
// window turned away here leaves the cursor standing still until the next op
// fails its guard. Refer to the serialisation note in
// vessel/torch/crucible_native.py. This question then turns away the sessions
// that do not arm that guard.
// =====================================================================

[[nodiscard]] CRUCIBLE_HOT ::fixy::Option<::fixy::Ref<crucible::Vigil>> recording_vigil() noexcept {
    using Recorder = ::fixy::Option<::fixy::Ref<crucible::Vigil>>;
    // The TLS state of PyTorch holds the mode and the Vigil handle of the
    // thread.  The TLS-stored pointer crosses the same ABI boundary as a
    // CrucibleHandle, so it enters through the typed door that tags it
    // source::ABIBoundary.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the gate reads the recording state that PyTorch keeps for each thread")
    auto& state = c10::CrucibleState::get_tls_state();
    if (state.mode() == c10::CrucibleMode::INACTIVE) [[likely]]
        return ::fixy::none;

    void* tls_context = state.context();
    if (!tls_context) [[unlikely]]
        return ::fixy::none;

    crucible::Vigil& vigil = *as_vigil_typed(static_cast<CrucibleHandle>(tls_context)).value();
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    if (!vigil.is_producer_thread()) [[unlikely]]
        return ::fixy::none;
    return Recorder::some(::fixy::erase_brand(::fixy::to(vigil)));
}

// Bits 2 and 3 of op_flags. `is_foreach` is a property of the operator, which
// the unboxed path knows at compile time and the boxed path caches beside the
// schema hash.
[[nodiscard]] inline uint8_t phase_flag_bits(bool is_foreach, bool grad_enabled) noexcept {
    const auto phase = static_cast<uint8_t>(derive_training_phase(is_foreach, grad_enabled));
    return static_cast<uint8_t>((phase & 0x3) << crucible::op_flag::PHASE_SHIFT);
}

// The two functions below read the modes and the dispatch keys of PyTorch for
// one operation, so they are in an ABI region.
//
// For the boxed fallback, which reads the gradient mode for no flag of its own
// and so has no value to share.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the flags read the modes and the dispatch keys of PyTorch for an operation")
[[nodiscard]] inline uint8_t phase_flag_bits(bool is_foreach) noexcept {
    return phase_flag_bits(is_foreach, c10::GradMode::is_enabled());
}

// The five bits of per-operation context. Mutability is a template parameter
// here and a parsed schema on the boxed path; the generator derives it from
// the same alias annotation FunctionSchema::is_mutable() reads, and the two
// were compared over the 3765 aten schemas the fork registers. The same split
// holds for the fused-kernel question the phase derivation asks: a template
// parameter here, a cached name search on the boxed path, one operator name
// behind both.
template <bool IsMutable, bool IsForeach>
[[nodiscard]] uint8_t entry_op_flags(c10::DispatchKeySet dispatch_keys) noexcept {
    uint8_t flags = 0;
    // One read, shared with the phase derivation below rather than repeated
    // inside it.
    const bool grad_enabled = c10::GradMode::is_enabled();
    if (c10::InferenceMode::is_enabled()) flags |= crucible::op_flag::INFERENCE_MODE;
    if (grad_enabled) flags |= crucible::op_flag::GRAD_ENABLED;
    if constexpr (IsMutable) flags |= crucible::op_flag::IS_MUTABLE;
    flags |= phase_flag_bits(IsForeach, grad_enabled);
    if (dispatch_keys.has(c10::DispatchKey::Python)) flags |= crucible::op_flag::TORCH_FUNCTION;
    return flags;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The entry the unboxed path builds for one operator, with each bounded field
// at the largest value it can take. The input and output counts are each at
// most `capacity`, because Recording stops both at its capacity. The scalar
// count is at most kInlineScalars, because write_scalar_slots writes at most
// that many slots, the overflow digest included.
[[nodiscard]] consteval crucible::TraceRing::Entry unboxed_entry_ceiling(crucible::SchemaHash schema_hash,
                                                                         uint32_t capacity) {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = schema_hash;
    entry.num_inputs = static_cast<uint16_t>(capacity);
    entry.num_outputs = static_cast<uint16_t>(capacity);
    entry.num_scalar_args = kInlineScalars;
    return entry;
}

// The scalar slots of an entry, from the Recording of its operation.
template <uint32_t Capacity, class Home>
void write_recorded_scalars(crucible::TraceRing::Entry& entry,
                            const Recording<Capacity, false, Home>& recording) noexcept {
    write_scalar_slots(entry, recording.scalars);
}

template <uint32_t Capacity, class Home>
void write_recorded_scalars(crucible::TraceRing::Entry& entry,
                            const Recording<Capacity, true, Home>& recording) noexcept {
    write_scalar_slots(entry, recording.scalars, recording.overflow);
}

// Builds the entry and hands it to the Vigil. The schema hash arrives as an
// argument rather than as a template parameter so that one instantiation
// serves every operator of the same capacity, mutability and kernel family,
// which is around a hundred rather than 3110.  The dispatch keys of PyTorch
// give the flags of the entry, so the signature is in an ABI region.  The
// context is the context of the producer claim, which call_recording took.
//
// Never inlined: an inline copy in each of the 3110 kernels would take the
// place of the one copy of each family.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the dispatch keys of PyTorch give the flags of the entry")
template <bool IsMutable, bool IsForeach, uint32_t Capacity, bool MayOverflow, class Home>
[[gnu::noinline]] void append_trace_entry(const Recording<Capacity, MayOverflow, Home>& recording,
                                          crucible::SchemaHash schema_hash, c10::DispatchKeySet dispatch_keys,
                                          crucible::Vigil& vigil, crucible::VigilFgCtx const& fg,
                                          crucible::ScopeHash scope_hash, crucible::OpDepth depth) {
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    // A tensor that fill_meta refuses has no meta, so the operation stays out
    // of the ring, as an operation that the trust ladder refuses.  This is the
    // rank part and the enumerator part of the ladder on this path.
    if (recording.each_tensor_description == TensorDescription::refused) [[unlikely]]
        return;

    crucible::TraceRing::Entry entry{};
    entry.schema_hash = schema_hash;
    entry.op_flags = entry_op_flags<IsMutable, IsForeach>(dispatch_keys);
    write_recorded_scalars(entry, recording);

    // The View of the records and the two counts of the entry come from one
    // read of the counts, after each call that takes the entry or reads the
    // Recording.  So the count check of mint_validated_op always passes.  GCC
    // keeps the compare, because it reads the counts back from the entry.
    const ::fixy::View<const crucible::TensorMeta> records = recording.records();
    entry.num_inputs = recording.counts.inputs;
    entry.num_outputs = recording.counts.outputs;

    // The guard word reads the scalar slots and the counts above, and the
    // records of the inputs.  The records hold the inputs as they were before
    // the operation ran, and no operation of the redispatch wrote them.
    entry.shape_hash = compute_shape_hash(recording.input_records(), entry, depth);

    // The trust ladder, discharged at compile time. Every bound it checks has
    // a compile-time ceiling on this path, and RecordKernel asserts that
    // entry_is_well_formed accepts the entry built at that ceiling. fill_meta
    // refuses each rank above the ceiling of metas_are_well_formed, and it
    // asserts that the ladder accepts each rank that it keeps. The
    // enumerators of each meta came through the decoders, and the check at
    // the start of this function refused each other meta. The runtime walk
    // could therefore reject nothing here. The boxed fallback and the C ABI
    // build entries from data with no compile-time bound, and they keep the
    // runtime walk through validate_ffi_op.
    //
    // The schema hash is the one input that is an argument rather than a
    // constant. Its only caller passes RecordKernel::kSchemaHash, which the
    // same assert pins to a value other than zero. The check below stays in
    // each build, so a caller that breaks that rule ends the process.
    CRUCIBLE_FATAL_INVARIANT(schema_hash.raw() != 0);
    auto validated = crucible::mint_ffi_entry(entry).retag<crucible::vessel::ValidatedEntryTag>();

    RecordingBinding binding = ::fixy::mint_fn_for<::fixy::role::PureLinear>(std::move(validated));

    // dispatch_op_pure rather than dispatch_op: it takes the context of the
    // Vigil's producer claim. recording_vigil confirmed that this thread can
    // take the claim. A kernel reached from an init, background or test
    // context has no such context, so its call does not compile.
    const auto op = crucible::mint_validated_op(std::move(binding).value(), records);
    for (const crucible::ValidatedOp& certified : op) {
        const crucible::DispatchResult result =
            vigil.dispatch_op_pure(fg, certified, scope_hash, crucible::CallsiteHash{}, depth);
        // A region entry comes once for each iteration of a region, so the
        // read of the eager word is a cold call.
        if (result.enters_region) [[unlikely]]
            observe_eager_numerics(vigil, fg);
    }
}

// =====================================================================
// The kernel
// =====================================================================

// Not defined: the specialization below takes the signature apart, and a
// Signature that is not a function type is a shape this file does not handle.
template <class Op, uint32_t TableIndex, class Signature>
struct RecordKernel;

template <class Op, uint32_t TableIndex, class Ret, class... Args>
struct RecordKernel<Op, TableIndex, Ret(Args...)> {
    // The row of the operator in the table.
    static constexpr const OpEntry& kRow = aten_op_row<TableIndex>();

    static_assert(call_agrees_with_codegen_v<Op>,
                  "the declared signature of Op::call is not the signature Op::schema declares; the fork's "
                  "operator-header shape changed");
    static_assert(sizeof...(Args) == kRow.arity,
                  "the parameter count of Op::call is not the arity in aten_op_table.h; the table was generated "
                  "against a different revision of the fork than this build compiles");
    static_assert(tensor_argument_mask<Args...>() == kRow.tensor_arg_mask,
                  "the tensor positions read from the C++ parameter types are not the positions the generator read "
                  "from the schema string");
    static_assert(tensor_list_argument_mask<Args...>() == kRow.tensor_list_mask,
                  "the tensor-list positions read from the C++ parameter types are not the positions the generator "
                  "read from the schema string");

    static constexpr crucible::SchemaHash kSchemaHash{kRow.schema_hash};
    static constexpr bool kIsMutable = kRow.is_mutable;

    // The role of the slots of the argument at Position, of the type Arg.
    template <std::size_t Position, class Arg>
    static constexpr crucible::ScalarRole kArgumentRole = argument_role<Position, Arg>(kSchemaHash);

    // One of the fused multi-tensor kernels, which is half of what the phase
    // derivation reads to name an operation an optimizer operation. Constant
    // here, so the derivation folds to a grad-mode question on this path.
    static constexpr bool kIsForeach = is_foreach_op_name(kRow.name);

    // Exact when no argument is a tensor list: one slot per tensor argument.
    static constexpr uint32_t kInputBound =
        kRow.tensor_list_mask == 0u ? static_cast<uint32_t>(::fixy::popcount(kRow.tensor_arg_mask)) : MAX_INLINE_METAS;

    static constexpr uint32_t kCapacity =
        ::fixy::min(MAX_INLINE_METAS, kInputBound + result_meta_bound_v<std::remove_cvref_t<Ret>>);

    // True when the operator can carry more scalars than the inline slots,
    // or more input tensors than kCapacity: a list argument has no length
    // bound at compile time.  Only such an operator folds the overflow
    // digest of entry_slots.h.
    static constexpr bool kMayOverflow =
        scalar_bound_v<Args...> > kInlineScalars || kRow.tensor_list_mask != 0u || kInputBound > kCapacity;

    // append_trace_entry certifies this operator's entries without a runtime
    // check. That is sound only while the ladder accepts the largest entry
    // this operator can build: a schema hash other than zero, both tensor
    // counts at kCapacity, and the scalar count at its saturation point. A
    // change to any of those caps, or to a ladder bound, fails here.
    static_assert(crucible::entry_is_well_formed(unboxed_entry_ceiling(kSchemaHash, kCapacity)),
                  "the trust ladder rejects an entry this operator can build, so append_trace_entry must not skip it");

    // The SchemaTable maps a schema hash to a human name, for the trace reader
    // and for the diagnostic C API in crucible_fallback.cpp. The boxed path
    // fills it the first time it sees an operator; this is that first sighting
    // for the unboxed path.
    //
    // It is a first-record event and not a load-time one.  The global table
    // holds SCHEMA_TABLE_CAP names, and the fork exposes 3110 operators, so a
    // registration of each operator at load would end the process.  A
    // registration at the first record also keeps the names of the tables
    // equal to the operators that the run used.
    //
    // A producer thread of a Vigil writes the flag, and the store comes after
    // the registration.  A second store of true writes the same value, so two
    // Vigils on two threads need no other order.  On x86_64 the acquire load
    // and the release store are plain moves.  A registration costs one branch
    // per operator after the first, not one per operation.
    //
    // The flag is a static member of a class template, so CRUCIBLE_PROCESS_WIDE
    // has no effect on it, and each library that instantiates the kernel holds
    // its own flag.  That is correct: the schema tables are process-wide, and a
    // second registration of one name in a second library finds the name there
    // and keeps it.  Only libcrucible_dispatch.so instantiates the kernels.
    static inline ::fixy::Atomic<bool> schema_name_registered{false};

    [[gnu::cold, gnu::noinline]] static void register_schema_name_once(crucible::Vigil& vigil) {
        // The tables take names only from a thread that holds a Vigil's
        // producer claim, which this thread does.  The global table is
        // sealed by now, so the name goes to the late table, which the vessel
        // library reads.  PyTorch's operator names are compiled into the
        // generated headers this table was built from, so crucible produced
        // the name.  The hash and the two parts of the row are constants of the
        // program image, and the constructor of the internal name checks them
        // at compile time.
        vigil.register_schema_name(
            crucible::InternalSchemaName{kSchemaHash, ::fixy::side_as<crucible::side::SchemaBaseName>(kRow.name),
                                         ::fixy::side_as<crucible::side::SchemaOverload>(kRow.overload)});
        schema_name_registered.store_release(true);
    }

    using OwnedRecording = Recording<kCapacity, kMayOverflow, SlotsOwned>;

    // The count of the slots of a Recording of the kernel.
    static constexpr uint32_t kSlots = OwnedRecording::kSlots;

    // True when the operator has no exact bound of its records: a tensor-list
    // argument, or a result that is a list.  Then the records go into a frame
    // of the record stack of the Vigil, and no call fills a list of
    // MAX_INLINE_METAS slots.
    static constexpr bool kRecordsInFrame = kCapacity == MAX_INLINE_METAS;

    // Outlined so that the not-recording path above stays a gate, a branch and
    // a redispatch. Not cold: when recording is on this is the only path that
    // runs, and `cold` would put it in .text.unlikely on that claim.
    //
    // The nest holds the depth of the operation from here until the
    // operation returns or unwinds.  Each operation that the redispatch runs
    // opens its nest inside this one, so it gets a depth one more.  The
    // dispatch to the Vigil stays after the redispatch, so a child gets to
    // the Vigil before its parent, as the replay cursor expects.  When the
    // operation raises, or append_trace_entry refuses it, after a child got
    // to the Vigil, the close of the nest gives the closing entry of the nest
    // (VigilOpNest).
    //
    // The frame holds the slots from the record of the inputs to the dispatch
    // to the Vigil.  An operation that the redispatch runs opens its frame
    // past these slots, and its frame closes before the redispatch returns.
    // An unwind of a PyTorch error closes the frame too.  When the stack has
    // fewer free slots than kCapacity, the kernel records through
    // call_recording_on_stack.  The arm of the frame runs the operator and
    // gives its result, which can be a reference, so the frame is a foreign
    // frame (with_foreign_frame of ValidatedOp.h).
    //
    // The functions from here to call take the arguments of an operator of
    // PyTorch and its dispatch keys, so they are in an ABI region.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the kernel takes the arguments of an operator of PyTorch and runs it again")
    [[gnu::noinline]] static Ret call_recording(crucible::Vigil& vigil, c10::DispatchKeySet dispatch_keys,
                                                Args... args) {
        if (!schema_name_registered.load_acquire()) [[unlikely]]
            register_schema_name_once(vigil);

        const crucible::VigilFgCtx fg = vigil.mint_producer_context();
        // The CPU allocator follows the mode of the Vigil at the entry and at
        // the exit of the operation (storage_generation.h).  The scope comes
        // before the nest, so its exit check follows the close of the nest.
        const AllocatorFollowScope follow{vigil.context()};
        crucible::OpRecordStack& stack = vigil.op_record_stack(fg);
        const crucible::VigilOpNest nest{vigil, fg};
        if constexpr (kRecordsInFrame) {
            return stack.template with_foreign_frame<kSlots>(
                [&](auto slots) -> Ret {
                    return record_in_slots(vigil, fg, dispatch_keys, nest.depth(),
                                           ::fixy::erase_brand(std::move(slots)), args...);
                },
                [&]() -> Ret { return call_recording_on_stack(vigil, fg, dispatch_keys, nest.depth(), args...); });
        } else {
            // The Recording holds its slots inline, on the stack of this call,
            // and writes them across the redispatch.
            OwnedRecording recording{};
            return record_operation(vigil, fg, dispatch_keys, nest.depth(), recording, args...);
        }
    }

    // The record of an operator with no exact bound, into the slots of a frame
    // or of the list of the cold path.  Each caller erases the brand of its
    // window, so one body serves the two paths, and the kernel holds one copy
    // of it.
    template <class Brand>
    [[gnu::noinline]] static Ret
    record_in_slots(crucible::Vigil& vigil, crucible::VigilFgCtx const& fg, c10::DispatchKeySet dispatch_keys,
                    crucible::OpDepth depth, ::fixy::ViewMut<crucible::TensorMeta, Brand, kSlots> metas, Args... args)
        requires kRecordsInFrame
    {
        Recording<kCapacity, kMayOverflow, SlotsLent<Brand>> recording{std::move(metas)};
        return record_operation(vigil, fg, dispatch_keys, depth, recording, args...);
    }

    // Records the operation through a list on the stack of this call.  The
    // kernel takes this path only when the record stack of the Vigil has
    // fewer free slots than kCapacity, which a deep nest of operations needs.
    // The list costs a fill of each of its slots.  The list is in this
    // function, so the frame of call_recording holds no list.  The list is a
    // window stack, and its foreign door lends its slots to the Recording of a
    // lent window, so this path and the frame path share one body of the
    // record.  A new list holds no window, so the window of kSlots fits.
    CRUCIBLE_COLD static Ret call_recording_on_stack(crucible::Vigil& vigil, crucible::VigilFgCtx const& fg,
                                                     c10::DispatchKeySet dispatch_keys, crucible::OpDepth depth,
                                                     Args... args)
        requires kRecordsInFrame
    {
        ::foundation::core::WindowStack<crucible::TensorMeta, kSlots> list{};
        return list.template with_foreign_window<kSlots>(
            [&](auto slots) -> Ret {
                return record_in_slots(vigil, fg, dispatch_keys, depth, ::fixy::erase_brand(std::move(slots)), args...);
            },
            []() -> Ret { ::fixy::unreachable(); });
    }

    // Records each argument with the role of its position (kArgumentRole), in
    // the order of the arguments, as the boxed fallback reads its stack.
    template <class Home, std::size_t... Positions>
    CRUCIBLE_INLINE static void record_arguments(Recording<kCapacity, kMayOverflow, Home>& recording,
                                                 std::index_sequence<Positions...>, const Args&... args) {
        (record_argument<kArgumentRole<Positions, Args>>(recording, args), ...);
    }

    // Records the inputs into the slots of recording, runs the operation,
    // records its results, and gives the operation to the Vigil.  The slots
    // hold the records from the first record_argument to the dispatch, and no
    // other operation writes them in between.
    template <class Home>
    CRUCIBLE_INLINE static Ret record_operation(crucible::Vigil& vigil, crucible::VigilFgCtx const& fg,
                                                c10::DispatchKeySet dispatch_keys, crucible::OpDepth depth,
                                                Recording<kCapacity, kMayOverflow, Home>& recording, Args... args) {
        // Inputs before the operation runs, because an out= argument is
        // written by it.  The role of each argument comes from its type and
        // its position, at compile time.
        record_arguments(recording, std::index_sequence_for<Args...>{}, args...);
        const auto scope_hash = crucible::ScopeHash{c10::CrucibleState::get_tls_state().scope_hash()};

        if constexpr (std::is_void_v<Ret>) {
            Op::redispatch(dispatch_keys & kAfterCrucibleKeyset, args...);
            append_trace_entry<kIsMutable, kIsForeach>(recording, kSchemaHash, dispatch_keys, vigil, fg, scope_hash,
                                                       depth);
        } else if constexpr (std::is_reference_v<Ret>) {
            // An in-place or out= operator returns a reference to an argument
            // the caller owns, which outlives this frame. The result is held
            // as a pointer rather than bound to a reference because
            // -Wdangling-reference flags any reference-returning call whose
            // arguments include a temporary, and every operator here takes a
            // DispatchKeySet by value.
            auto* result = &Op::redispatch(dispatch_keys & kAfterCrucibleKeyset, args...);
            record_result(recording, *result);
            append_trace_entry<kIsMutable, kIsForeach>(recording, kSchemaHash, dispatch_keys, vigil, fg, scope_hash,
                                                       depth);
            return *result;
        } else {
            Ret result = Op::redispatch(dispatch_keys & kAfterCrucibleKeyset, args...);
            record_result(recording, result);
            append_trace_entry<kIsMutable, kIsForeach>(recording, kSchemaHash, dispatch_keys, vigil, fg, scope_hash,
                                                       depth);
            return result;
        }
    }

    // Registration goes through a captureless lambda rather than TORCH_FN.
    // TORCH_FN reaches c10::KernelFunction::makeFromUnboxedFunction, whose
    // static_assert compares the function pointer with nullptr, and
    // -fno-delete-null-pointer-checks stops GCC folding that comparison to a
    // constant, so the assertion fails on every operator. The build carries
    // that flag on purpose and this library is the one that talks to foreign
    // code, so the flag stays and the registration changes.
    //
    // A captureless lambda is also the form that keeps the call direct.
    // makeFromUnboxedLambda stores an empty functor whose operator() names
    // `call`, so the dispatcher's trampoline calls it directly, where the
    // runtime-function-pointer overload would call through a stored pointer.
    [[nodiscard]] static torch::CppFunction as_cpp_function() {
        return torch::CppFunction(
            [](c10::DispatchKeySet dispatch_keys, Args... args) -> Ret { return call(dispatch_keys, args...); });
    }

    static Ret call(c10::DispatchKeySet dispatch_keys, Args... args) {
        const ::fixy::Option<::fixy::Ref<crucible::Vigil>> vigil = recording_vigil();
        return vigil.match(
            [&](::fixy::Ref<crucible::Vigil> recorder) -> Ret {
                return call_recording(*recorder, dispatch_keys, args...);
            },
            [&]() -> Ret { return Op::redispatch(dispatch_keys & kAfterCrucibleKeyset, args...); });
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
};

// The kernel of one table row, with its signature derived rather than named.
template <uint32_t TableIndex, class Op>
using RecordKernelFor = RecordKernel<Op, TableIndex, call_signature<Op>>;

}  // namespace crucible::vessel
