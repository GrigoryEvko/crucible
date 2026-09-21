#pragma once

// record_kernel.h — one unboxed recording kernel per ATen operator, plus the
// ATen-to-Crucible metadata bridge both recording paths share.
//
// WHY AN UNBOXED KERNEL
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
// WHERE THE SIGNATURE COMES FROM
//
// std::meta::parameters_of on ^^Op::call gives the declared parameter types
// and return_type_of gives the result. Folding both into a function type gives
// something a partial specialization takes apart into a parameter pack, which
// is the pack the kernel declares and the pack the dispatcher calls it with.
//
// THREE AGREEMENTS
//
//   1. The reflected signature equals Op::schema, the typedef PyTorch's own
//      generator writes. Catches a fork whose call and schema drift apart.
//   2. The reflected arity equals the table's arity. Catches a table built
//      against a different revision of the fork than this build compiles.
//   3. The tensor and tensor-list masks computed from the reflected C++ types
//      equal the masks the generator computed from the schema string. Two
//      readings of one argument list must name the same positions. Measured
//      over all 3110 operators of the fork: zero disagreements.
//
// THE MACRO FENCE
//
// This unit needs crucible::Vigil and crucible::TraceRing from
// include/crucible, and the role, the binding and the context gate from
// include/fixy. The two trees are siblings for the duration of the port and
// both spell the same macro names. Measured: the two Platform.h files define
// an identical set of CRUCIBLE_ macros, GCC accepts every identical
// redefinition silently, and the five that differ differ only in the
// namespace of the helper they call — ::crucible::detail::fail_invariant
// against ::foundation::detail::fail_invariant, and likewise for the contract
// helper. Neither spelling means anything the other does not.
//
// The fence gives each tree its own five. fixy comes first and parses with
// foundation's. The five are then undefined, and crucible comes second, which
// parses crucible/Platform.h for the first time in this unit and leaves
// crucible's five in force to the end of it. Every crucible header, here and
// in anything included after this, therefore parses with crucible's macros,
// so no inline function acquires two definitions across units.
//
// That holds only if crucible/Platform.h was not parsed before the fence: a
// header already consumed cannot redefine anything, and the five would stay
// foundation's for the rest of the unit. The check below enforces it. Both
// Platform.h files define CRUCIBLE_INLINE and nothing else in the tree does,
// so its presence means one of them came first.
//
// The fence goes when include/crucible/Platform.h flips onto
// foundation/Platform.h and the duplicated definitions are deleted.

#if defined(CRUCIBLE_INLINE)
#error \
    "record_kernel.h must precede every crucible/ and fixy/ include in this translation unit. It parses the two sibling substrates in a fixed order so each keeps its own spelling of the five CRUCIBLE_ macros the two Platform.h files define differently."
#endif

#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Role.h>

#undef CRUCIBLE_DIAG_ASSERT
#undef CRUCIBLE_FATAL_INVARIANT
#undef CRUCIBLE_INVARIANT
#undef CRUCIBLE_PRE
#undef CRUCIBLE_PRE_MSG

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
#include "vessel_api_typed.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible::vessel {

// =====================================================================
// FNV-1a
//
// One definition for both recording paths and for the Python ctypes path in
// vessel_api.cpp. The background thread joins a trace entry to the schema
// table on this hash and a replay compares shape hashes across runs, so two
// spellings of the fold would produce traces that cannot be compared.
// =====================================================================

inline constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
inline constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

// The default seed starts a fresh fold. Passing a previous result continues
// one, which is how the shape hash chains across several tensors.
[[nodiscard]] inline uint64_t fnv1a_bytes(const void* data, size_t length, uint64_t accumulator = kFnvOffset) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; i++) {
        accumulator ^= bytes[i];
        accumulator *= kFnvPrime;
    }
    return accumulator;
}

[[nodiscard]] inline uint64_t fnv1a_str(const char* text, size_t length) noexcept { return fnv1a_bytes(text, length); }

// =====================================================================
// What one operation contributes to a trace entry
// =====================================================================

// The metadata slots one entry can carry. Both recording paths stop here, so
// an operation with more tensors records its first MAX_INLINE_METAS and
// reports the count it recorded, identically on either path.
inline constexpr uint32_t MAX_INLINE_METAS = 32;

struct MetaCount {
    uint16_t inputs = 0;
    uint16_t outputs = 0;

    [[nodiscard]] uint32_t total() const { return static_cast<uint32_t>(inputs) + outputs; }
};

// The five scalar slots the trace entry stores inline. `count` saturates,
// which is what makes the two paths report the same number for an operation
// carrying more.
struct ScalarArgs {
    int64_t values[5]{};
    uint16_t count = 0;

    void push(int64_t value) {
        if (count < 5) values[count++] = value;
    }
};

// =====================================================================
// Tensor metadata
//
// Fills all 168 bytes of crucible::TensorMeta from an at::Tensor. Shared with
// the boxed fallback: a trace whose entries were filled by two spellings of
// this would have a content hash that depends on which path recorded it.
// =====================================================================

inline void fill_meta(crucible::TensorMeta& meta, const at::Tensor& tensor) {
    meta = {};  // zero-init (InitSafe -- NSDMI defaults)
    if (!tensor.defined()) return;

    // -- Core fields --------------------------------------------------

    const auto ndim = static_cast<uint8_t>(std::min(tensor.dim(), static_cast<int64_t>(8)));
    meta.ndim = ndim;

    const auto sizes = tensor.sizes();
    for (uint8_t d = 0; d < ndim; d++)
        meta.sizes[d] = ::crucible::tensor_dim(sizes[d]);

    const bool strided = (tensor.layout() == c10::Layout::Strided);
    if (strided) {
        const auto strides = tensor.strides();
        for (uint8_t d = 0; d < ndim; d++)
            meta.strides[d] = ::crucible::tensor_dim(strides[d]);
        // The storage address crosses the ATen boundary here, so it enters as
        // source::External: hashed and compared as an opaque cookie, never
        // dereferenced, until some later validator retags it.
        meta.data_ptr = ::crucible::external_data_ptr(tensor.data_ptr());
    }

    // c10 and crucible enums mirror ordinals by design (Types.h documents the
    // invariant), so bit_cast carries them. A
    // static_cast<crucible::X>(static_cast<int8_t>(c10_value)) would be a
    // double narrowing that silently truncated any future c10 value escaping
    // int8_t range while looking like an ordinary conversion. bit_cast is bit
    // reinterpretation and a size mismatch is a compile error.
    //
    // Every mirrored ordinal is checked one by one. A single sampled ordinal
    // witnesses one lane of the invariant, not the invariant, and the per-type
    // message names the type that drifted.
    static_assert(sizeof(c10::ScalarType) == sizeof(crucible::ScalarType));
    static_assert(sizeof(c10::DeviceType) == sizeof(crucible::DeviceType));
    static_assert(sizeof(c10::Layout) == sizeof(crucible::Layout));

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
#undef CRUCIBLE_MIRROR_SCALAR

    // Undefined is the one deliberate divergence and must not be bit_cast. c10
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
                  "if c10 ever adopts -1 for Undefined, drop the explicit map below and bit_cast it");

    static_assert(static_cast<int8_t>(c10::DeviceType::CUDA) == static_cast<int8_t>(crucible::DeviceType::CUDA),
                  "c10::DeviceType::CUDA ordinal drifted from crucible mirror");
    static_assert(static_cast<int8_t>(c10::Layout::Strided) == static_cast<int8_t>(crucible::Layout::Strided),
                  "c10::Layout::Strided ordinal drifted from crucible mirror");

    // A dtype c10 names and Crucible does not — a quantized type, a narrow
    // integer width, a newer FP8 variant — bit_casts to an ordinal no
    // crucible enumerator matches. The enum has a fixed underlying type, so
    // the value is well defined rather than UB and round-trips through the
    // trace unchanged. Widening the mirror is a Types.h change.
    const auto scalar_type = tensor.scalar_type();
    meta.dtype = (scalar_type == c10::ScalarType::Undefined) ? crucible::ScalarType::Undefined
                                                             : std::bit_cast<crucible::ScalarType>(scalar_type);
    meta.device_type = std::bit_cast<crucible::DeviceType>(tensor.device().type());
    // c10::DeviceIndex is already int8_t (c10/core/Device.h).
    meta.device_idx = tensor.device().has_index() ? tensor.device().index() : int8_t{-1};
    meta.layout = std::bit_cast<crucible::Layout>(tensor.layout());

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
            meta.grad_fn_hash = ::crucible::grad_fn_hash(fnv1a_str(grad_fn_name.data(), grad_fn_name.size()));
        }
        meta.output_nr = static_cast<uint8_t>(torch::autograd::impl::get_autograd_meta(tensor)->output_nr_ & 0xFF);
    }

    // The autograd is_view_ flag catches expand, as_strided and narrow, which
    // share a storage base at offset zero. The two checks after it catch a
    // view with no autograd metadata.
    if (autograd_meta && static_cast<torch::autograd::AutogradMeta*>(autograd_meta)->is_view_)
        flags |= crucible::meta_flags::IS_VIEW;

    if (strided && impl->has_storage()) {
        auto* storage_base = impl->storage().data_ptr().get();
        if (storage_base != nullptr && tensor.data_ptr() != static_cast<char*>(storage_base)) {
            flags |= crucible::meta_flags::IS_VIEW;
        }
        meta.storage_nbytes = static_cast<uint32_t>(impl->storage().nbytes() & 0xFFFFFFFF);
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
}

// =====================================================================
// Shape hash
//
// FNV-1a over (ndim, sizes[0..ndim-1]) per input tensor, one accumulator
// chained across them. Identical to vessel_api.cpp's crucible_hash_shapes(),
// because a replay compares this value across runs and across paths.
// =====================================================================

[[nodiscard]] inline crucible::ShapeHash compute_shape_hash(const crucible::TensorMeta* metas, uint16_t n_inputs) {
    uint64_t accumulator = kFnvOffset;
    for (uint16_t i = 0; i < n_inputs; i++) {
        accumulator = fnv1a_bytes(&metas[i].ndim, 1, accumulator);
        // `sizes` is a TensorDimArray, whose lanes stay plain int64_t so a
        // bulk reader can take the block without a copy. raw_data() is that
        // reader; only the write path goes through the refined TensorDim.
        const uint32_t nbytes = static_cast<uint32_t>(metas[i].ndim) * static_cast<uint32_t>(sizeof(int64_t));
        accumulator = fnv1a_bytes(metas[i].sizes.raw_data(), nbytes, accumulator);
    }
    return crucible::ShapeHash{accumulator};
}

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
//
// One definition for both recording paths: two copies would let them write
// different phase bits into one trace. Zero-initialized, so there is no
// initializer to order and no guard variable on the path that reads it.
// =====================================================================

inline thread_local uint32_t backward_depth = 0;

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
// key or a replay guard. What reads them is a reader of the trace —
// Serialize.h carries them to the file and TraceVisualizer.h splits the
// forward and backward columns on them. A label is worth having only if it
// says the same thing about the same computation every time, and a phase the
// caller set by hand said whatever that caller had remembered to call. Two
// loops over one model labelled one computation two ways, and the label of the
// loop that skipped a call was silently FORWARD throughout.
// =====================================================================

[[nodiscard]] inline crucible::TrainingPhase derive_training_phase(bool is_foreach) noexcept {
    if (backward_depth != 0) return crucible::TrainingPhase::BACKWARD;
    if (is_foreach && !c10::GradMode::is_enabled()) return crucible::TrainingPhase::OPTIMIZER;
    return crucible::TrainingPhase::FORWARD;
}

// True for the fused multi-tensor operators. 452 names in aten_op_table.h carry
// this infix: the `aten::_foreach_*` family, and the two
// `aten::_amp_foreach_non_finite_check_and_unscale` overloads. The second pair
// belongs to the gradient scaler rather than to a fused update, and it runs
// inside an optimizer step with gradients off, so the phase it derives is the
// one it should have.
[[nodiscard]] constexpr bool is_foreach_op_name(std::string_view name) noexcept {
    return name.contains("_foreach_");
}

// Everything below DispatchKey::Crucible, which is the backend that computes
// the operation. The constructor is constexpr, so this costs no initializer.
inline constexpr c10::DispatchKeySet kAfterCrucibleKeyset =
    c10::DispatchKeySet(c10::DispatchKeySet::FULL_AFTER, c10::DispatchKey::Crucible);

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
    for (const OpEntry& row : aten_op_table) {
        const std::string_view overload{row.overload};
        size_t length = std::string_view{row.name}.size();
        if (!overload.empty()) length += 1 + overload.size();
        longest = std::max(longest, length);
    }
    return longest;
}

// One past the longest name the table holds, so the buffer is exact: it never
// truncates and it never allocates.
inline constexpr size_t kQualifiedOpNameCapacity = longest_qualified_op_name() + 1;

class QualifiedOpName {
public:
    explicit QualifiedOpName(const OpEntry& row) noexcept {
        const std::string_view name{row.name};
        const std::string_view overload{row.overload};
        size_t next = 0;
        for (const char character : name)
            text_[next++] = character;
        if (!overload.empty()) {
            text_[next++] = '.';
            for (const char character : overload)
                text_[next++] = character;
        }
        CRUCIBLE_PRE(next < kQualifiedOpNameCapacity);
        text_[next] = '\0';
    }

    [[nodiscard]] const char* c_str() const noexcept { return text_.data(); }

private:
    std::array<char, kQualifiedOpNameCapacity> text_{};
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
//     so each contributes its ordinal.
//   * Device, Generator, Storage, Stream, a string and an ArrayRef<Scalar>
//     carry tags of their own, so each contributes nothing.
//   * A Scalar boxes by its payload: Int, Double or Bool contributes, and
//     ComplexDouble does not.
//   * A concrete SymInt boxes into Int and contributes. A symbolic one boxes
//     into the SymInt tag and does not.
//   * An absent optional boxes into None and contributes nothing.
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
    // An optional or an OptionalArrayRef. Contributes what its payload
    // contributes when it holds one.
    Optional,
};

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
    else if constexpr (std::is_enum_v<Arg>)
        return ArgumentKind::Enumerator;
    else if constexpr (std::integral<Arg>)
        return ArgumentKind::Integer;
    else
        return ArgumentKind::Ignored;
}

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
// computed against.
template <class... Args>
[[nodiscard]] consteval uint32_t tensor_argument_mask() noexcept {
    const std::array<bool, sizeof...(Args)> is_tensor{is_tensor_argument_v<Args>...};
    uint32_t mask = 0;
    for (uint32_t position = 0; position < sizeof...(Args); position++)
        if (is_tensor[position]) mask |= uint32_t{1} << position;
    return mask;
}

template <class... Args>
[[nodiscard]] consteval uint32_t tensor_list_argument_mask() noexcept {
    const std::array<bool, sizeof...(Args)> is_list{is_tensor_list_argument_v<Args>...};
    uint32_t mask = 0;
    for (uint32_t position = 0; position < sizeof...(Args); position++)
        if (is_list[position]) mask |= uint32_t{1} << position;
    return mask;
}

// ---------------------------------------------------------------------
// The table classifies every parameter type the fork's 3110 operators use.
// The list is the measured inventory of `using schema` parameter types, in
// descending order of use, so a fork that introduces a shape the table sends
// to Ignored is caught by the mask agreement rather than by this block.

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
static_assert(argument_kind_of<at::ArrayRef<at::Scalar>>() == ArgumentKind::Ignored);
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

// =====================================================================
// The reflected signature, and the cross-check against PyTorch's codegen
// =====================================================================

// Carries a return type and a parameter pack as one type, so that
// std::meta::substitute can build it from a reflected parameter list and a
// partial specialization can take it apart again.
template <class Ret, class... Args>
struct FunctionTypeOf {
    using type = Ret(Args...);
};

template <class Op>
[[nodiscard]] consteval std::meta::info reflected_call_type() {
    std::vector<std::meta::info> arguments;
    arguments.push_back(std::meta::return_type_of(^^Op::call));
    for (const auto parameter : std::meta::parameters_of(^^Op::call))
        arguments.push_back(std::meta::type_of(parameter));
    return std::meta::substitute(^^FunctionTypeOf, arguments);
}

template <class Op>
using reflected_call_signature = typename[:reflected_call_type<Op>():] ::type;

// The first agreement: the reflected fold and the typedef PyTorch's generator
// writes describe one function. Const and a non-const out= slot survive the
// round trip, an ITensorListRef is not flattened into an ArrayRef, and a tuple
// return stays a tuple.
template <class Op>
inline constexpr bool reflection_agrees_with_codegen_v =
    std::is_same_v<reflected_call_signature<Op>, typename Op::schema>;

// =====================================================================
// How many metadata slots an operation can fill
//
// The shared limit is MAX_INLINE_METAS, and a Recording sized to it zeroes
// 5376 bytes of stack per recorded operation. Most operators cannot come near
// it: an operator with no tensor-list argument fills exactly one input slot
// per tensor argument, which the table's masks already state, and a result
// that is neither a vector nor a list fills a known number of output slots.
// When both bounds are exact the Recording is sized to their sum, so `mm`
// carries three slots rather than thirty-two.
//
// The bound is never smaller than what the operation can produce, so
// truncation behaviour is unchanged: where the bound is exact, neither path
// truncates, and where it is not, both stop at MAX_INLINE_METAS.
// =====================================================================

// MAX_INLINE_METAS stands for "no smaller bound", which makes the clamp below
// a plain minimum.
template <class T>
inline constexpr uint32_t result_meta_bound_v = 0;
template <>
inline constexpr uint32_t result_meta_bound_v<at::Tensor> = 1;
template <class T>
inline constexpr uint32_t result_meta_bound_v<std::vector<T>> = MAX_INLINE_METAS;
template <class T>
inline constexpr uint32_t result_meta_bound_v<c10::ArrayRef<T>> = MAX_INLINE_METAS;
template <class... Ts>
inline constexpr uint32_t result_meta_bound_v<std::tuple<Ts...>> =
    std::min(MAX_INLINE_METAS, (uint32_t{0} + ... + result_meta_bound_v<std::remove_cvref_t<Ts>>));

// =====================================================================
// Recording one operation
// =====================================================================

template <uint32_t Capacity>
struct Recording {
    static_assert(Capacity <= MAX_INLINE_METAS, "a Recording must not admit more metadata than either path records");

    std::array<crucible::TensorMeta, Capacity> metas{};
    MetaCount counts{};
    ScalarArgs scalars{};

    void add_input(const at::Tensor& tensor) {
        if (tensor.defined() && counts.inputs < Capacity) {
            fill_meta(metas[counts.inputs], tensor);
            counts.inputs++;
        }
    }

    // Outputs follow the inputs in one array, which is the order the MetaLog
    // append and every reader of it expect.
    void add_output(const at::Tensor& tensor) {
        if (tensor.defined() && counts.total() < Capacity) {
            fill_meta(metas[counts.total()], tensor);
            counts.outputs++;
        }
    }
};

// One argument, classified by the table above and recorded accordingly. An
// Optional recurses once into its payload, which the payload witnesses prove
// terminates.
template <uint32_t Capacity, class T>
void record_argument(Recording<Capacity>& recording, const T& value) {
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
            if (tensor.has_value()) recording.add_input(*tensor);
        }
    } else if constexpr (kind == ArgumentKind::Optional) {
        if (value.has_value()) record_argument(recording, *value);
    } else if constexpr (kind == ArgumentKind::Bool) {
        recording.scalars.push(value ? 1 : 0);
    } else if constexpr (kind == ArgumentKind::Integer) {
        recording.scalars.push(static_cast<int64_t>(value));
    } else if constexpr (kind == ArgumentKind::Enumerator) {
        recording.scalars.push(static_cast<int64_t>(std::to_underlying(value)));
    } else if constexpr (kind == ArgumentKind::Double) {
        recording.scalars.push(std::bit_cast<int64_t>(value));
    } else if constexpr (kind == ArgumentKind::Scalar) {
        if (value.isFloatingPoint()) {
            recording.scalars.push(std::bit_cast<int64_t>(value.toDouble()));
        } else if (value.isBoolean()) {
            recording.scalars.push(value.toBool() ? 1 : 0);
        } else if (value.isIntegral(/*includeBool=*/false)) {
            recording.scalars.push(value.toLong());
        }
    } else if constexpr (kind == ArgumentKind::SymInt) {
        if (const auto concrete = value.maybe_as_int(); concrete.has_value()) recording.scalars.push(*concrete);
    } else if constexpr (kind == ArgumentKind::IntegerList) {
        for (const int64_t element : value)
            recording.scalars.push(element);
    } else if constexpr (kind == ArgumentKind::DoubleList) {
        for (const double element : value)
            recording.scalars.push(std::bit_cast<int64_t>(element));
    } else if constexpr (kind == ArgumentKind::SymIntList) {
        for (const c10::SymInt& element : value)
            record_argument(recording, element);
    } else {
        static_assert(kind == ArgumentKind::Ignored, "every ArgumentKind must have a branch");
    }
}

// One result. The boxed fallback reads return values off the stack, where a
// tuple return has already been flattened into one slot per element, so
// destructuring the tuple reaches the same elements in the same order. A
// result that is neither a tensor nor a list of them contributes nothing,
// which is what the fallback does with one.
template <uint32_t Capacity, class T>
void record_result(Recording<Capacity>& recording, const T& result) {
    using Result = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Result, at::Tensor>) {
        recording.add_output(result);
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

[[nodiscard]] CRUCIBLE_HOT crucible::Vigil* recording_vigil() noexcept {
    auto& state = c10::CrucibleState::get_tls_state();
    if (state.mode() == c10::CrucibleMode::INACTIVE) [[likely]]
        return nullptr;

    void* tls_context = state.context();
    if (!tls_context) [[unlikely]]
        return nullptr;

    // The TLS-stored pointer crosses the same ABI boundary as a
    // CrucibleHandle, so it enters through the typed door that tags it
    // source::ABIBoundary.
    crucible::Vigil* vigil = as_vigil_typed(static_cast<CrucibleHandle>(tls_context)).value();
    if (!vigil->is_producer_thread()) [[unlikely]]
        return nullptr;
    return vigil;
}

// Bits 2 and 3 of op_flags. `is_foreach` is a property of the operator, which
// the unboxed path knows at compile time and the boxed path caches beside the
// schema hash.
[[nodiscard]] inline uint8_t phase_flag_bits(bool is_foreach) noexcept {
    const auto phase = static_cast<uint8_t>(derive_training_phase(is_foreach));
    return static_cast<uint8_t>((phase & 0x3) << crucible::op_flag::PHASE_SHIFT);
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
    if (c10::InferenceMode::is_enabled()) flags |= crucible::op_flag::INFERENCE_MODE;
    if (c10::GradMode::is_enabled()) flags |= crucible::op_flag::GRAD_ENABLED;
    if constexpr (IsMutable) flags |= crucible::op_flag::IS_MUTABLE;
    flags |= phase_flag_bits(IsForeach);
    if (dispatch_keys.has(c10::DispatchKey::Python)) flags |= crucible::op_flag::TORCH_FUNCTION;
    return flags;
}

// Builds the entry and hands it to the Vigil. The schema hash arrives as an
// argument rather than as a template parameter so that one instantiation
// serves every operator of the same capacity, mutability and kernel family,
// which is around a hundred rather than 3110.
template <bool IsMutable, bool IsForeach, uint32_t Capacity>
void append_trace_entry(const Recording<Capacity>& recording, crucible::SchemaHash schema_hash,
                        crucible::ShapeHash shape_hash, c10::DispatchKeySet dispatch_keys, crucible::Vigil* vigil,
                        crucible::ScopeHash scope_hash) {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = schema_hash;
    entry.shape_hash = shape_hash;
    entry.num_inputs = recording.counts.inputs;
    entry.num_outputs = recording.counts.outputs;
    entry.num_scalar_args = recording.scalars.count;
    entry.op_flags = entry_op_flags<IsMutable, IsForeach>(dispatch_keys);
    for (uint16_t i = 0; i < recording.scalars.count; i++)
        entry.scalar_values[i] = recording.scalars.values[i];

    // The trust ladder. Every field above was read from this operator's own
    // typed arguments and from the live c10 query surface, which is a foreign
    // runtime, so the Entry starts at the first trust tag and reaches a
    // recording entry point only by passing the checks the two other adapters
    // pass. The operator already ran eagerly before this function was called,
    // so a rejected Entry costs the trace this operation and costs the caller
    // nothing.
    //
    // Sharing one ladder with the boxed fallback and the C ABI is what keeps
    // a bound from being enforced on one path and absent on the next.
    auto validated = crucible::vessel::mint_validated_entry(crucible::mint_ffi_entry(entry), recording.metas.data(),
                                                            recording.counts.total());
    if (!validated) [[unlikely]]
        return;

    RecordingBinding binding = ::fixy::mint_fn_for<::fixy::role::PureLinear>(*validated);

    // dispatch_op_pure rather than dispatch_op: the facade demands an empty
    // caller row, which catches a kernel reached from an init, background or
    // test context instead of from the foreground. The default CallerRow is
    // the empty row and costs nothing.
    (void)vigil->dispatch_op_pure(std::move(binding).value(), recording.metas.data(), recording.counts.total(),
                                  scope_hash);
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
    static_assert(reflection_agrees_with_codegen_v<Op>,
                  "the signature reflected from Op::call is not the signature Op::schema declares; the fork's "
                  "operator-header shape changed");
    static_assert(sizeof...(Args) == aten_op_table[TableIndex].arity,
                  "the reflected parameter count is not the arity in aten_op_table.h; the table was generated "
                  "against a different revision of the fork than this build compiles");
    static_assert(tensor_argument_mask<Args...>() == aten_op_table[TableIndex].tensor_arg_mask,
                  "the tensor positions read from the C++ parameter types are not the positions the generator read "
                  "from the schema string");
    static_assert(tensor_list_argument_mask<Args...>() == aten_op_table[TableIndex].tensor_list_mask,
                  "the tensor-list positions read from the C++ parameter types are not the positions the generator "
                  "read from the schema string");

    static constexpr crucible::SchemaHash kSchemaHash{aten_op_table[TableIndex].schema_hash};
    static constexpr bool kIsMutable = aten_op_table[TableIndex].is_mutable;

    // One of the fused multi-tensor kernels, which is half of what the phase
    // derivation reads to name an operation an optimizer operation. Constant
    // here, so the derivation folds to a grad-mode question on this path.
    static constexpr bool kIsForeach = is_foreach_op_name(aten_op_table[TableIndex].name);

    // Exact when no argument is a tensor list: one slot per tensor argument.
    static constexpr uint32_t kInputBound =
        aten_op_table[TableIndex].tensor_list_mask == 0u
            ? static_cast<uint32_t>(std::popcount(aten_op_table[TableIndex].tensor_arg_mask))
            : MAX_INLINE_METAS;

    static constexpr uint32_t kCapacity =
        std::min(MAX_INLINE_METAS, kInputBound + result_meta_bound_v<std::remove_cvref_t<Ret>>);

    // The SchemaTable maps a schema hash to a human name, for the trace reader
    // and for the diagnostic C API in crucible_fallback.cpp. The boxed path
    // fills it the first time it sees an operator; this is that first sighting
    // for the unboxed path.
    //
    // It is a first-record event rather than a load-time one because the table
    // holds SCHEMA_TABLE_CAP entries and aborts past it, and the fork exposes
    // 3110 operators. Registering them all at load would abort; registering
    // them as they are recorded also keeps the table's contents equal to the
    // set of operators the run actually used, which is what it meant before.
    //
    // Only the producer thread reaches here, so the flag has one writer and
    // relaxed ordering is enough. The flag is set even when the table is
    // sealed, so a sealed table costs one branch per operator rather than one
    // per operation.
    static inline std::atomic<bool> schema_name_registered{false};

    [[gnu::cold, gnu::noinline]] static void register_schema_name_once() {
        auto& table = crucible::global_schema_table();
        if (!table.is_sealed()) {
            // PyTorch's operator names are compiled into the generated headers
            // this table was built from, so the name is trusted by source.
            const QualifiedOpName name{aten_op_table[TableIndex]};
            crucible::register_schema_name(table.mint_mutable_view(), kSchemaHash,
                                           crucible::SchemaTable::SanitizedName{name.c_str()});
        }
        schema_name_registered.store(true, std::memory_order_relaxed);
    }

    // Outlined so that the not-recording path above stays a gate, a branch and
    // a redispatch. Not cold: when recording is on this is the only path that
    // runs, and `cold` would put it in .text.unlikely on that claim.
    [[gnu::noinline]] static Ret call_recording(crucible::Vigil* vigil, c10::DispatchKeySet dispatch_keys,
                                                Args... args) {
        if (!schema_name_registered.load(std::memory_order_relaxed)) [[unlikely]]
            register_schema_name_once();

        // Inputs before the operation runs, because an out= argument is
        // written by it.
        Recording<kCapacity> recording{};
        (record_argument(recording, args), ...);
        const auto shape_hash = compute_shape_hash(recording.metas.data(), recording.counts.inputs);
        const auto scope_hash = crucible::ScopeHash{c10::CrucibleState::get_tls_state().scope_hash()};

        if constexpr (std::is_void_v<Ret>) {
            Op::redispatch(dispatch_keys & kAfterCrucibleKeyset, args...);
            append_trace_entry<kIsMutable, kIsForeach>(recording, kSchemaHash, shape_hash, dispatch_keys, vigil,
                                                       scope_hash);
        } else if constexpr (std::is_reference_v<Ret>) {
            // An in-place or out= operator returns a reference to an argument
            // the caller owns, which outlives this frame. The result is held
            // as a pointer rather than bound to a reference because
            // -Wdangling-reference flags any reference-returning call whose
            // arguments include a temporary, and every operator here takes a
            // DispatchKeySet by value.
            auto* result = &Op::redispatch(dispatch_keys & kAfterCrucibleKeyset, args...);
            record_result(recording, *result);
            append_trace_entry<kIsMutable, kIsForeach>(recording, kSchemaHash, shape_hash, dispatch_keys, vigil,
                                                       scope_hash);
            return *result;
        } else {
            Ret result = Op::redispatch(dispatch_keys & kAfterCrucibleKeyset, args...);
            record_result(recording, result);
            append_trace_entry<kIsMutable, kIsForeach>(recording, kSchemaHash, shape_hash, dispatch_keys, vigil,
                                                       scope_hash);
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
        crucible::Vigil* vigil = recording_vigil();
        if (vigil == nullptr) [[likely]]
            return Op::redispatch(dispatch_keys & kAfterCrucibleKeyset, args...);
        return call_recording(vigil, dispatch_keys, args...);
    }
};

// The kernel of one table row, with its signature derived rather than named.
template <uint32_t TableIndex, class Op>
using RecordKernelFor = RecordKernel<Op, TableIndex, reflected_call_signature<Op>>;

}  // namespace crucible::vessel
