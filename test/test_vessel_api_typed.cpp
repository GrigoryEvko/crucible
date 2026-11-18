#include "../vessel/torch/vessel_api_typed.h"

#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Lifetime.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>
#include "test_assert.h"

namespace {

using crucible::ExternalDataPtr;
using crucible::vessel::TypedHandle;
using crucible::vessel::TypedMeta;
using crucible::vessel::TypedSchemaName;

// The tag is phantom, so the typed wrapper must add no storage at the
// ABI boundary.  A failure here means the wrapper costs something.

static_assert(std::is_same_v<TypedHandle, ::fixy::Tagged<crucible::Vigil*, ::fixy::tags::source::ABIBoundary>>);
static_assert(sizeof(TypedHandle) == sizeof(CrucibleHandle));
static_assert(alignof(TypedHandle) == alignof(CrucibleHandle));
static_assert(std::is_trivially_copy_constructible_v<TypedHandle>);

// The typed records keep the C type that the caller wrote.  Only
// decode_ffi_meta makes a TensorMeta from a record.
static_assert(std::is_same_v<TypedMeta, ::fixy::Tagged<const CrucibleMeta*, ::fixy::tags::source::ABIBoundary>>);
static_assert(sizeof(TypedMeta) == sizeof(const CrucibleMeta*));
static_assert(alignof(TypedMeta) == alignof(const CrucibleMeta*));
static_assert(std::is_trivially_copy_constructible_v<TypedMeta>);

// No door of the base starts the life of a TensorMeta over the bytes of a C
// record, so the thunks decode each record into a frame of the record stack
// of the Vigil.  The checked lifetime start refuses the Tagged pointer of a
// TensorMeta.  A foreign View refuses its bool, its enumerations and its
// pointer.  decode refuses a pointer.
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<crucible::TensorMeta>);
static_assert(!::foundation::core::ForeignElement<crucible::TensorMeta>);
static_assert(!::foundation::core::ForeignElement<CrucibleMeta>);
static_assert(!::foundation::core::DecodableFrom<crucible::TensorMeta, CrucibleMeta>);

// Two tags make two distinct classes with no conversion between them.
// That is what stops a value from crossing the boundary under the wrong
// provenance.
static_assert(!std::is_convertible_v<::fixy::Tagged<crucible::Vigil*, ::fixy::tags::source::External>, TypedHandle>);
static_assert(!std::is_convertible_v<::fixy::Tagged<const CrucibleMeta*, ::fixy::tags::source::External>, TypedMeta>);

// The boundary tag names a source, so the one door admits it.  The tag
// a check earns is not mintable, so no helper here can claim it.
static_assert(::fixy::MintableTag<::fixy::tags::source::ABIBoundary>);
static_assert(!::fixy::MintableTag<::fixy::tags::source::Sanitized>);

// A typed data pointer collapses to the bare pointer width.  A typed
// schema name is a C text in the storage of the table, and the tag
// collapses around it.

static_assert(
    std::is_same_v<decltype(crucible::vessel::data_ptr_typed(std::declval<TypedMeta>(), 0)), ExternalDataPtr>);
static_assert(std::is_same_v<ExternalDataPtr, ::fixy::Tagged<void*, ::fixy::tags::source::External>>);
static_assert(sizeof(ExternalDataPtr) == sizeof(void*));
static_assert(alignof(ExternalDataPtr) == alignof(void*));
static_assert(std::is_trivially_copy_constructible_v<ExternalDataPtr>);

static_assert(std::is_same_v<TypedSchemaName, crucible::SchemaTable::LookupName>);
static_assert(sizeof(TypedSchemaName) == sizeof(::fixy::CText));
static_assert(alignof(TypedSchemaName) == alignof(::fixy::CText));
static_assert(std::is_trivially_copy_constructible_v<TypedSchemaName>);

// The data pointer's provenance differs from the containing meta's, so
// a value carrying the container's tag cannot take its place.
static_assert(!std::is_convertible_v<::fixy::Tagged<void*, ::fixy::tags::source::ABIBoundary>, ExternalDataPtr>);
static_assert(!std::is_convertible_v<::fixy::Tagged<void*, ::fixy::tags::source::Sanitized>, ExternalDataPtr>);
// A tag is a type, not a name.  The trust family also has a tag named
// External, and a pointer under that tag cannot enter a meta either.
static_assert(!std::is_convertible_v<::fixy::Tagged<void*, ::fixy::tags::trust::External>, ExternalDataPtr>);

// A validated name and raw input from the boundary cannot be confused
// for one another.
static_assert(!std::is_convertible_v<::fixy::Tagged<::fixy::CText, ::fixy::tags::source::External>, TypedSchemaName>);

int g_failures = 0;

#define EXPECT(cond, msg)                                                                       \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::fprintf(stderr, "FAIL: %s -- %s (%s:%d)\n", #cond, (msg), __FILE__, __LINE__); \
            ++g_failures;                                                                       \
        }                                                                                       \
    } while (0)

void test_handle_roundtrip() {
    alignas(crucible::Vigil) std::array<std::byte, sizeof(crucible::Vigil)> storage{};
    auto* vigil_ptr = std::bit_cast<crucible::Vigil*>(storage.data());
    CrucibleHandle raw = static_cast<CrucibleHandle>(vigil_ptr);

    auto typed = crucible::vessel::as_vigil_typed(raw);
    EXPECT(typed.value() == vigil_ptr, "typed handle must preserve the Vigil pointer value");

    CrucibleHandle roundtrip = crucible::vessel::from_typed(typed);
    EXPECT(roundtrip == raw, "typed handle must roundtrip to CrucibleHandle");

    // The wrapper must hold exactly the bytes of the raw pointer.
    std::array<std::byte, sizeof(TypedHandle)> typed_bytes{};
    std::memcpy(typed_bytes.data(), &typed, sizeof(typed));
    std::array<std::byte, sizeof(CrucibleHandle)> raw_bytes{};
    std::memcpy(raw_bytes.data(), &raw, sizeof(raw));
    EXPECT(typed_bytes == raw_bytes, "TypedHandle layout must be byte-identical to CrucibleHandle");
}

void test_meta_roundtrip() {
    // Every field is filled, so the decode has to give each field of the
    // record to the TensorMeta.
    CrucibleMeta meta{};
    meta.sizes[0] = 4;
    meta.sizes[1] = 8;
    meta.sizes[2] = 16;
    meta.strides[0] = 128;
    meta.strides[1] = 16;
    meta.strides[2] = 1;
    meta.data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xCAFEBABE));
    meta.ndim = 3;
    meta.dtype = static_cast<int8_t>(crucible::ScalarType::Float);
    meta.device_type = static_cast<int8_t>(crucible::DeviceType::CUDA);
    meta.device_idx = 1;
    meta.layout = static_cast<int8_t>(crucible::Layout::Strided);
    meta.requires_grad = 1;
    meta.flags = 0x07;
    meta.output_nr = 2;
    meta.storage_offset = 32;
    meta.version = 5;
    meta.storage_nbytes = 4 * 8 * 16 * 4;
    meta.grad_fn_hash = 0x0123456789ABCDEFULL;

    const CrucibleMeta arr[2] = {meta, meta};

    // The count is passed explicitly.  A count of zero is reserved for
    // the null case, because the C boundary promises that a zero count
    // comes with a null pointer, and the helper refuses a non-null
    // pointer with a zero count under a debug build.
    auto typed_single = crucible::vessel::as_meta_typed(&arr[0], 1);
    EXPECT(typed_single.value() != nullptr, "as_meta_typed on non-null meta must return non-null pointer");

    auto typed_multi = crucible::vessel::as_meta_typed(arr, 2);
    EXPECT(typed_multi.value() == &arr[0], "as_meta_typed pointer must match the raw pointer");

    // The decode gives each field of the record, the data pointer too.
    crucible::TensorMeta expected{};
    expected.sizes[0] = crucible::tensor_dim(4);
    expected.sizes[1] = crucible::tensor_dim(8);
    expected.sizes[2] = crucible::tensor_dim(16);
    expected.strides[0] = crucible::tensor_dim(128);
    expected.strides[1] = crucible::tensor_dim(16);
    expected.strides[2] = crucible::tensor_dim(1);
    expected.data_ptr = crucible::external_data_ptr(meta.data_ptr);
    expected.ndim = 3;
    expected.dtype = crucible::ScalarType::Float;
    expected.device_type = crucible::DeviceType::CUDA;
    expected.device_idx = 1;
    expected.layout = crucible::Layout::Strided;
    expected.requires_grad = true;
    expected.flags = 0x07;
    expected.output_nr = 2;
    expected.storage_offset = 32;
    expected.version = 5;
    expected.storage_nbytes = 4 * 8 * 16 * 4;
    expected.grad_fn_hash = crucible::grad_fn_hash(0x0123456789ABCDEFULL);
    const bool is_decoded_whole = crucible::vessel::decode_ffi_meta(arr[1]).match(
        [&expected](crucible::TensorMeta decoded) noexcept { return decoded == expected; },
        [] noexcept { return false; });
    EXPECT(is_decoded_whole, "the decode must give each field of the record");

    const CrucibleMeta* roundtripped = crucible::vessel::metas_from_typed(typed_multi);
    EXPECT(roundtripped == arr, "metas_from_typed must round-trip to the original CrucibleMeta pointer");

    // The typed view must hold exactly the bytes of the pointer it
    // wraps.
    std::array<std::byte, sizeof(TypedMeta)> typed_bytes{};
    std::memcpy(typed_bytes.data(), &typed_multi, sizeof(typed_multi));
    const CrucibleMeta* raw_ptr = &arr[0];
    std::array<std::byte, sizeof(const CrucibleMeta*)> raw_bytes{};
    std::memcpy(raw_bytes.data(), &raw_ptr, sizeof(raw_ptr));
    EXPECT(typed_bytes == raw_bytes, "TypedMeta layout must be byte-identical to const CrucibleMeta*");
}

void test_meta_zero_n_with_null() {
    // The C boundary promises that a zero count comes with a null
    // pointer.  The helper takes that pair and returns a typed null that
    // round-trips, which is what an op with no tensor arguments uses.
    auto typed = crucible::vessel::as_meta_typed(nullptr, 0);
    EXPECT(typed.value() == nullptr, "as_meta_typed(nullptr, 0) must return null typed view");

    const CrucibleMeta* roundtripped = crucible::vessel::metas_from_typed(typed);
    EXPECT(roundtripped == nullptr, "metas_from_typed of null typed view must round-trip to nullptr");
}

void test_data_ptr_typed() {
    // The typed read must return the same bits the wire struct holds,
    // only carrying provenance.
    CrucibleMeta arr[3]{};
    arr[0].data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xAAAA0000));
    arr[1].data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xBBBB0000));
    arr[2].data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xCCCC0000));

    auto typed_arr = crucible::vessel::as_meta_typed(arr, 3);

    auto p0 = crucible::vessel::data_ptr_typed(typed_arr, 0);
    auto p1 = crucible::vessel::data_ptr_typed(typed_arr, 1);
    auto p2 = crucible::vessel::data_ptr_typed(typed_arr, 2);

    EXPECT(p0.value() == arr[0].data_ptr, "data_ptr_typed[0] must read the wire pointer");
    EXPECT(p1.value() == arr[1].data_ptr, "data_ptr_typed[1] must read the wire pointer");
    EXPECT(p2.value() == arr[2].data_ptr, "data_ptr_typed[2] must read the wire pointer");

    // Distinct inputs stay distinct, so nothing is cached or aliased.
    EXPECT(p0.value() != p1.value(), "distinct meta data_ptrs must yield distinct typed values");
    EXPECT(p1.value() != p2.value(), "distinct meta data_ptrs must yield distinct typed values");

    // The wrapper must hold exactly the bytes of the bare pointer.
    std::array<std::byte, sizeof(ExternalDataPtr)> typed_bytes{};
    std::memcpy(typed_bytes.data(), &p0, sizeof(p0));
    std::array<std::byte, sizeof(void*)> raw_bytes{};
    void* raw0 = arr[0].data_ptr;
    std::memcpy(raw_bytes.data(), &raw0, sizeof(raw0));
    EXPECT(typed_bytes == raw_bytes, "ExternalDataPtr layout must be byte-identical to void*");
}

// The ranks of some records, in order.
using Ranks = ::fixy::FixedList<uint8_t, crucible::kMaxOpRecords>;

// The ranks of the records, in order.
[[nodiscard]] Ranks ranks_of(::fixy::View<const crucible::TensorMeta> records) noexcept {
    Ranks ranks{};
    for (const crucible::TensorMeta& record : records) {
        EXPECT(ranks.append(record.ndim).is_ok(), "a View of the slots holds at most kMaxOpRecords records");
    }
    return ranks;
}

// The ranks of the records that a decode gives, in order, or no rank when the
// decode refuses the op.
[[nodiscard]] Ranks collect_ranks(::fixy::Option<::fixy::View<const crucible::TensorMeta>> const& decoded) noexcept {
    for (const ::fixy::View<const crucible::TensorMeta> records : decoded) {
        return ranks_of(records);
    }
    return Ranks{};
}

// True when the ranks are the expected ranks, in order.
template <std::size_t Count>
[[nodiscard]] bool has_ranks(Ranks const& ranks, ::fixy::FixedArray<uint8_t, Count> const& expected) noexcept {
    return ::fixy::equal(ranks.view(), ::fixy::View<const uint8_t>{expected.view()});
}

// The C thunks decode the records of the caller into the slots of a frame of
// the record stack of the Vigil, and the View of the op borrows the slots.
void test_ffi_records_copy() {
    // Each record has a different rank, which names it.
    ::fixy::FixedArray<CrucibleMeta, 3> sources{};
    sources.at(::fixy::index<0>()).ndim = 1;
    sources.at(::fixy::index<1>()).ndim = 2;
    sources.at(::fixy::index<2>()).ndim = 3;
    const TypedMeta typed = crucible::vessel::as_meta_typed(&sources.at(::fixy::index<0>()), 3);

    ::fixy::FixedArray<crucible::TensorMeta, 3> slots{};
    const auto ranks = collect_ranks(crucible::vessel::decode_ffi_records(typed, slots.view()));
    EXPECT(has_ranks(ranks, ::fixy::FixedArray<uint8_t, 3>{1, 2, 3}),
           "the View holds each record of the caller, in order");
}

// A C caller writes each record through CrucibleMeta, its own type, and the
// thunk reads the records in the same unit.  Each record that the thunk gives
// holds the fields that the caller wrote.
void test_ffi_records_read_the_c_records() {
    CrucibleMeta written{};
    written.sizes[0] = 12;
    written.strides[0] = 1;
    written.data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0x2000U));
    written.ndim = 1;
    written.dtype = static_cast<int8_t>(crucible::ScalarType::Float);
    written.requires_grad = 1;
    written.storage_nbytes = 444;
    written.grad_fn_hash = 0xABC2U;
    ::fixy::FixedArray<CrucibleMeta, 2> callers{};
    callers.at(::fixy::index<0>()) = written;
    callers.at(::fixy::index<1>()) = written;

    ::fixy::FixedArray<crucible::TensorMeta, 2> slots{};
    const ::fixy::View<const crucible::TensorMeta> records =
        crucible::vessel::decode_ffi_records(crucible::vessel::as_meta_typed(&callers.at(::fixy::index<0>()), 2),
                                             slots.view())
            .value_or(::fixy::View<const crucible::TensorMeta>{});
    EXPECT(records.size() == 2, "the View holds each record of the caller");
    for (const crucible::TensorMeta& read : records) {
        EXPECT(read.ndim == 1, "a record holds the rank that the caller wrote");
        EXPECT(crucible::raw_tensor_dim(read.sizes[0]) == 12, "a record holds the size that the caller wrote");
        EXPECT(read.dtype == crucible::ScalarType::Float, "a record holds the element type that the caller wrote");
        EXPECT(read.requires_grad, "a record holds the gradient flag that the caller wrote");
        EXPECT(read.data_ptr.value() == written.data_ptr, "a record holds the data pointer that the caller wrote");
        EXPECT(read.storage_nbytes == 444, "a record holds the storage size that the caller wrote");
        EXPECT(crucible::raw_grad_fn_hash(read) == 0xABC2U, "a record holds the hash that the caller wrote");
    }
}

// True when decode_ffi_meta gives a TensorMeta for the record.
[[nodiscard]] bool is_decoded(const CrucibleMeta& record) noexcept {
    return crucible::vessel::decode_ffi_meta(record).is_some();
}

// decode_ffi_meta refuses each byte of a record that breaks a claim of
// TensorMeta, and decode_ffi_records then records nothing for the op.
void test_ffi_decode_refuses_each_broken_claim() {
    CrucibleMeta held{};
    held.ndim = 2;
    held.sizes[0] = 3;
    held.sizes[1] = 5;
    held.strides[0] = 5;
    held.strides[1] = 1;
    held.dtype = static_cast<int8_t>(crucible::ScalarType::Float);
    held.device_type = static_cast<int8_t>(crucible::DeviceType::CPU);
    held.device_idx = -1;
    held.layout = static_cast<int8_t>(crucible::Layout::Strided);
    held.requires_grad = 1;
    held.flags = 0x3F;
    EXPECT(is_decoded(held), "a record that holds each claim decodes");

    CrucibleMeta wide_rank = held;
    wide_rank.ndim = crucible::kMaxTensorNDim + 1;
    EXPECT(!is_decoded(wide_rank), "a rank past kMaxTensorNDim is refused");

    CrucibleMeta wide_size = held;
    wide_size.sizes[1] = crucible::kMaxTensorDimExtent + 1;
    EXPECT(!is_decoded(wide_size), "a size past kMaxTensorDimExtent is refused");

    CrucibleMeta wide_stride = held;
    wide_stride.strides[0] = crucible::kMaxTensorDimExtent + 1;
    EXPECT(!is_decoded(wide_stride), "a stride past kMaxTensorDimExtent is refused");

    CrucibleMeta size_tail = held;
    size_tail.sizes[2] = 7;
    EXPECT(!is_decoded(size_tail), "a size past the rank that is not zero is refused");

    CrucibleMeta stride_tail = held;
    stride_tail.strides[7] = 1;
    EXPECT(!is_decoded(stride_tail), "a stride past the rank that is not zero is refused");

    CrucibleMeta unnamed_dtype = held;
    unnamed_dtype.dtype = 27;
    EXPECT(!is_decoded(unnamed_dtype), "an element type that names no enumerator is refused");

    CrucibleMeta unnamed_device = held;
    unnamed_device.device_type = 12;
    EXPECT(!is_decoded(unnamed_device), "a device that names no enumerator is refused");

    CrucibleMeta unnamed_layout = held;
    unnamed_layout.layout = 7;
    EXPECT(!is_decoded(unnamed_layout), "a layout that names no enumerator is refused");

    CrucibleMeta negative_device = held;
    negative_device.device_idx = -2;
    EXPECT(!is_decoded(negative_device), "a device index below -1 is refused");

    CrucibleMeta wide_grad = held;
    wide_grad.requires_grad = 2;
    EXPECT(!is_decoded(wide_grad), "a gradient byte other than 0 and 1 is refused");

    CrucibleMeta reserved_flag = held;
    reserved_flag.flags = 0x40;
    EXPECT(!is_decoded(reserved_flag), "a reserved bit of the flags is refused");

    ::fixy::FixedArray<CrucibleMeta, 2> callers{held, wide_grad};
    ::fixy::FixedArray<crucible::TensorMeta, 2> slots{};
    EXPECT(crucible::vessel::decode_ffi_records(crucible::vessel::as_meta_typed(&callers.at(::fixy::index<0>()), 2),
                                                slots.view())
               .is_none(),
           "an op with a refused record is refused");
}

// The ranks of the records that a thunk decodes into a frame of count slots
// on the stack, or no value when the frame does not fit or the decode
// refuses a record.  The frame closes at the return, as at the end of a
// thunk.
[[nodiscard]] ::fixy::Option<Ranks> decode_in_frame(crucible::OpRecordStack& stack, TypedMeta typed,
                                                    uint32_t count) noexcept {
    const crucible::OpRecordFrame frame{stack, count};
    for (const ::fixy::View<crucible::TensorMeta> slots : frame.slots()) {
        for (const ::fixy::View<const crucible::TensorMeta> records :
             crucible::vessel::decode_ffi_records(typed, slots)) {
            return ::fixy::Option<Ranks>::some(ranks_of(records));
        }
    }
    return ::fixy::none;
}

// The record stack of a Vigil holds the records of the ops in flight, and
// each thunk decodes into a frame at its top.  A closed frame gives its slots
// back, so the next call writes the same slots, and its View reaches only the
// slots of its own frame.  So a call with fewer records than the call before
// reads no record of the call before.  A thunk that runs while a frame is
// open decodes past that frame, and writes no record of it.
//
// The test makes a Vigil, and the start of its background thread seals the
// schema table.  So it runs after test_schema_name_typed.
void test_ffi_records_reuse_the_slots_of_the_producer() {
    // Each record has a different rank, which names it.  The record at
    // position 5 breaks a claim of TensorMeta.
    ::fixy::FixedArray<CrucibleMeta, 6> sources{};
    uint8_t rank = 1;
    for (CrucibleMeta& source : sources) {
        source.ndim = rank;
        ++rank;
    }
    sources.at(::fixy::index<5>()).requires_grad = 2;
    const CrucibleMeta& first = sources.at(::fixy::index<0>());
    const CrucibleMeta& fourth = sources.at(::fixy::index<3>());
    const auto typed_at = [](const CrucibleMeta& record, uint32_t count) noexcept {
        return crucible::vessel::as_meta_typed(&record, count);
    };

    crucible::Vigil vigil;
    crucible::OpRecordStack& stack = vigil.op_record_stack(vigil.mint_producer_context());

    EXPECT(has_ranks(decode_in_frame(stack, typed_at(first, 3), 3).value_or(Ranks{}),
                     ::fixy::FixedArray<uint8_t, 3>{1, 2, 3}),
           "the first call reads its three records");
    EXPECT(
        has_ranks(decode_in_frame(stack, typed_at(fourth, 1), 1).value_or(Ranks{}), ::fixy::FixedArray<uint8_t, 1>{4}),
        "a call with one record reads no record of the call before");

    // Positions 3, 4 and 5: the decode writes two slots and refuses the
    // third record, so the op records nothing.
    EXPECT(decode_in_frame(stack, typed_at(fourth, 3), 3).is_none(), "a call with a refused record gives no View");
    EXPECT(
        has_ranks(decode_in_frame(stack, typed_at(first, 1), 1).value_or(Ranks{}), ::fixy::FixedArray<uint8_t, 1>{1}),
        "a call after a refused call reads no record of the refused call");

    const auto none_read = decode_in_frame(stack, crucible::vessel::as_meta_typed(nullptr, 0), 0);
    const bool is_none_read_empty =
        none_read.match([](Ranks const& ranks) noexcept { return ranks.size() == 0; }, [] noexcept { return false; });
    EXPECT(is_none_read_empty, "an op with no tensor decodes, and reads no record of the call before");

    EXPECT(decode_in_frame(stack, typed_at(first, 3), crucible::kMaxOpRecords + 1).is_none(),
           "a count past the slots of the stack is refused");

    // A thunk that runs while the frame of an outer op is open.
    {
        const crucible::OpRecordFrame outer{stack, 3};
        const auto outer_slots = outer.slots();
        const auto outer_records = outer_slots.match(
            [&typed_at, &first](::fixy::View<crucible::TensorMeta> slots) noexcept {
                return crucible::vessel::decode_ffi_records(typed_at(first, 3), slots);
            },
            [] noexcept { return ::fixy::Option<::fixy::View<const crucible::TensorMeta>>{::fixy::none}; });
        EXPECT(has_ranks(decode_in_frame(stack, typed_at(fourth, 2), 2).value_or(Ranks{}),
                         ::fixy::FixedArray<uint8_t, 2>{4, 5}),
               "a nested call reads its own records");
        EXPECT(has_ranks(collect_ranks(outer_records), ::fixy::FixedArray<uint8_t, 3>{1, 2, 3}),
               "a nested call writes no record of the op around it");
        EXPECT(decode_in_frame(stack, typed_at(first, 3), crucible::kMaxOpRecords - 2).is_none(),
               "a nested call past the free slots is refused");
    }
    EXPECT(stack.held() == 0, "each frame gives its slots back");
}

void test_schema_name_typed() {
    // This target links the core library rather than the vessel one, so
    // the name is registered through the C++ API.  That reaches the same
    // global table the C thunk would use.  The view asks for the context
    // of a Vigil's producer claim, which the test takes from the test door.
    constexpr uint64_t hash_a = 0xA1A2A3A4A5A6A7A8ULL;
    constexpr uint64_t hash_b = 0xB1B2B3B4B5B6B7B8ULL;

    constexpr crucible::VigilFgCtx fg = ::foundation::effects::testing::foreground<crucible::Vigil>();
    for (const crucible::SchemaTable::MutableView& view : crucible::global_schema_table().mint_mutable_view(fg)) {
        const bool was_a_registered =
            crucible::register_schema_name(
                ::foundation::effects::testing::test().alloc, view, crucible::SchemaHash{hash_a},
                ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(::fixy::TextView{"aten::test_op_a"}))
                .is_ok();
        EXPECT(was_a_registered, "an open schema table must take the first registration");
        const bool was_b_registered =
            crucible::register_schema_name(
                ::foundation::effects::testing::test().alloc, view, crucible::SchemaHash{hash_b},
                ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(::fixy::TextView{"aten::test_op_b"}))
                .is_ok();
        EXPECT(was_b_registered, "an open schema table must take the second registration");
    }

    // The characters of a name that the table gives, or no character for an
    // unknown hash.
    const auto view_of = [](::fixy::Option<TypedSchemaName> const& name) noexcept {
        return name.match([](TypedSchemaName const& found) noexcept { return found.value().view(); },
                          [] noexcept { return ::fixy::TextView{}; });
    };

    const auto a = crucible::vessel::schema_name_typed(crucible::SchemaHash{hash_a});
    const auto b = crucible::vessel::schema_name_typed(crucible::SchemaHash{hash_b});
    EXPECT(a.is_some() && view_of(a) == ::fixy::TextView{"aten::test_op_a"}, "registered name a must lookup");
    EXPECT(b.is_some() && view_of(b) == ::fixy::TextView{"aten::test_op_b"}, "registered name b must lookup");

    const auto a2 = crucible::vessel::schema_name_typed(crucible::SchemaHash{hash_a});
    EXPECT(view_of(a2) == view_of(a), "a repeated lookup must give the same name");

    // An unknown hash gives no name, and the C boundary gives NULL for it.
    const auto missing = crucible::vessel::schema_name_typed(crucible::SchemaHash{0xDEADC0DEDEADC0DEULL});
    EXPECT(missing.is_none(), "an unknown schema hash must give no name");
}

// The codes of crucible_register_schema_name, through the typed door that
// the C entry calls.  A first name and the same name again register.  A
// different name of the held hash gives the collision code, and the table
// keeps the first name.  A zero hash and an empty name give the refusal code.
// The process lives through each case.
void test_schema_name_registration_codes() {
    constexpr uint64_t held_hash = 0xC1C2C3C4C5C6C7C8ULL;
    crucible::Vigil vigil;
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, held_hash, ::fixy::TextView{"aten::first"})
               == CRUCIBLE_SCHEMA_NAME_REGISTERED,
           "a first name registers");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, held_hash, ::fixy::TextView{"aten::first"})
               == CRUCIBLE_SCHEMA_NAME_REGISTERED,
           "the same name again registers");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, held_hash, ::fixy::TextView{"aten::second"})
               == CRUCIBLE_SCHEMA_NAME_COLLISION,
           "a different name of a held hash gives the collision code");
    const auto held_name = crucible::vessel::schema_name_typed(crucible::SchemaHash{held_hash});
    const bool keeps_first =
        held_name.match([](TypedSchemaName const& found) noexcept { return found.value().view() == "aten::first"; },
                        [] noexcept { return false; });
    EXPECT(keeps_first, "the table keeps the first name of a held hash");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, 0, ::fixy::TextView{"aten::first"})
               == CRUCIBLE_SCHEMA_NAME_REFUSED,
           "a zero hash gives the refusal code");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, held_hash + 1, ::fixy::TextView{})
               == CRUCIBLE_SCHEMA_NAME_REFUSED,
           "an empty name gives the refusal code");
}

// A table that holds SCHEMA_TABLE_CAP names gives the code of a full table
// for each new hash, and the process lives.  A Vigil seals the global table,
// so the names go to the late table, and other cases of this test hold some of
// its places.  So the case registers new hashes until the code changes, and
// that takes at most SCHEMA_TABLE_CAP of them.  A held hash of the full table
// still gives its own codes.  A full late table would refuse each later name
// of this process, so the case clears it after the end of its Vigil.
void test_schema_name_full_code() {
    constexpr uint64_t first_hash = 0xF0F0000000000000ULL;
    {
        crucible::Vigil vigil;
        uint32_t registered = 0;
        int code = CRUCIBLE_SCHEMA_NAME_REGISTERED;
        while (code == CRUCIBLE_SCHEMA_NAME_REGISTERED && registered <= crucible::SCHEMA_TABLE_CAP) {
            code = crucible::vessel::register_schema_name_typed(vigil, first_hash + registered,
                                                                ::fixy::TextView{"aten::filler"});
            if (code == CRUCIBLE_SCHEMA_NAME_REGISTERED) ++registered;
        }
        EXPECT(code == CRUCIBLE_SCHEMA_NAME_FULL, "a new hash at a full table gives the code of a full table");
        EXPECT(registered >= 1 && registered <= crucible::SCHEMA_TABLE_CAP,
               "the table took each name until it was full");
        EXPECT(crucible::vessel::register_schema_name_typed(vigil, first_hash, ::fixy::TextView{"aten::filler"})
                   == CRUCIBLE_SCHEMA_NAME_REGISTERED,
               "the same name of a held hash registers at a full table");
        EXPECT(crucible::vessel::register_schema_name_typed(vigil, first_hash, ::fixy::TextView{"aten::other"})
                   == CRUCIBLE_SCHEMA_NAME_COLLISION,
               "a different name of a held hash gives the collision code at a full table");
        EXPECT(crucible::vessel::register_schema_name_typed(vigil, first_hash + registered + 1,
                                                            ::fixy::TextView{"aten::filler"})
                   == CRUCIBLE_SCHEMA_NAME_FULL,
               "each new hash at a full table gives the code of a full table");
    }
    crucible::late_schema_table().clear(::foundation::effects::testing::test());
}

void test_abi_version_constant() {
    // The shared library's version accessor returns this same macro, so
    // checking the macro covers the translation unit.  Cross-checking the
    // built library happens at load time.
    static_assert(CRUCIBLE_VESSEL_ABI_VERSION != 0, "ABI version must be non-zero (zero is reserved)");
    EXPECT(CRUCIBLE_VESSEL_ABI_VERSION >= 1, "ABI version must be a positive integer");
}

void test_handle_distinct_pointers() {
    // Two storage blocks give two pointers, and the wrapper must keep
    // them apart rather than collapse or reuse a value.
    alignas(crucible::Vigil) std::array<std::byte, sizeof(crucible::Vigil)> storage_a{};
    alignas(crucible::Vigil) std::array<std::byte, sizeof(crucible::Vigil)> storage_b{};
    auto* a = std::bit_cast<crucible::Vigil*>(storage_a.data());
    auto* b = std::bit_cast<crucible::Vigil*>(storage_b.data());
    EXPECT(a != b, "test setup: distinct storage must yield distinct pointers");

    auto ta = crucible::vessel::as_vigil_typed(static_cast<CrucibleHandle>(a));
    auto tb = crucible::vessel::as_vigil_typed(static_cast<CrucibleHandle>(b));
    EXPECT(ta.value() == a, "typed handle for a must alias a");
    EXPECT(tb.value() == b, "typed handle for b must alias b");
    EXPECT(ta.value() != tb.value(), "distinct inputs must yield distinct typed handle values");
}

// True when the ladder accepts the one meta.
[[nodiscard]] bool is_well_formed(const crucible::TensorMeta& meta) noexcept {
    const ::fixy::FixedArray<crucible::TensorMeta, 1> records{meta};
    return crucible::vessel::metas_are_well_formed(records.view());
}

// A meta of the C ABI holds the bytes that a foreign caller wrote.  The
// ladder accepts it only when its element type, device and layout each name
// a crucible enumerator.  An element type that names none reaches
// element_size in the background pipeline, which ends the process.
void test_ladder_refuses_unnamed_enumerations() {
    crucible::TensorMeta named{};
    named.ndim = 1;
    named.dtype = crucible::ScalarType::Float;
    named.device_type = crucible::DeviceType::CUDA;
    named.layout = crucible::Layout::SparseBsc;
    EXPECT(is_well_formed(named), "a meta with named enumerations is well formed");

    crucible::TensorMeta narrow_integer = named;
    narrow_integer.dtype = static_cast<crucible::ScalarType>(27);
    EXPECT(!is_well_formed(narrow_integer), "the c10 ordinal of uint16 names no crucible element type");

    crucible::TensorMeta quantized = named;
    quantized.dtype = static_cast<crucible::ScalarType>(12);
    EXPECT(!is_well_formed(quantized), "the c10 ordinal of qint8 names no crucible element type");

    crucible::TensorMeta new_device = named;
    new_device.device_type = static_cast<crucible::DeviceType>(12);
    EXPECT(!is_well_formed(new_device), "the c10 ordinal of the XPU device names no crucible device");

    crucible::TensorMeta jagged = named;
    jagged.layout = static_cast<crucible::Layout>(7);
    EXPECT(!is_well_formed(jagged), "the ordinal 7 names no crucible layout");
}

// True when the ladder gives an op for the entry and the records.
[[nodiscard]] bool is_certified(const crucible::TraceRing::Entry& entry,
                                ::fixy::View<const crucible::TensorMeta> metas) noexcept {
    return crucible::vessel::validate_ffi_op(crucible::mint_ffi_entry(entry), metas).is_some();
}

// The records of an entry are its inputs and then its outputs.  The ladder
// refuses a count of records that is not their sum, because the build would
// then read the records of a different op.
void test_ladder_refuses_a_count_that_disagrees_with_the_entry() {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x61};
    entry.shape_hash = crucible::ShapeHash{0x62};
    entry.num_inputs = 1;
    entry.num_outputs = 1;
    ::fixy::FixedArray<crucible::TensorMeta, 3> metas{};
    for (crucible::TensorMeta& meta : metas) {
        meta.ndim = 1;
        meta.dtype = crucible::ScalarType::Float;
    }
    const ::fixy::View<const crucible::TensorMeta> all_records{metas.view()};
    EXPECT(is_certified(entry, all_records.window(0, 2).expect("two records fit")),
           "a count of records that is the sum of the inputs and the outputs is certified");
    EXPECT(!is_certified(entry, all_records), "one record more than the inputs and the outputs is refused");
    EXPECT(!is_certified(entry, all_records.window(0, 1).expect("one record fits")),
           "one record less than the inputs and the outputs is refused");
    EXPECT(!is_certified(entry, ::fixy::View<const crucible::TensorMeta>{}),
           "no record for an entry with an input and an output is refused");
}

// The word of the data slots of an entry holds one bit for each inline slot.
// The ladder refuses a bit past the five slots also when the entry holds five
// scalars, so no entry with a data slot that the entry does not have reaches
// the Vigil.
void test_ladder_refuses_a_data_slot_past_the_inline_slots() {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x71};
    entry.num_scalar_args = 5;
    entry.data_slot_bits = 0x11;
    EXPECT(crucible::vessel::entry_is_well_formed(entry), "data slots below the count of five are well formed");
    entry.data_slot_bits = 0x21;
    EXPECT(!crucible::vessel::entry_is_well_formed(entry), "a data slot past the five inline slots is refused");
    entry.data_slot_bits = 0x80;
    EXPECT(!crucible::vessel::entry_is_well_formed(entry), "the high bit of the word is refused");
    entry.num_scalar_args = 2;
    entry.data_slot_bits = 0x04;
    EXPECT(!crucible::vessel::entry_is_well_formed(entry), "a data slot past the count is refused");
}

// Each decoder gives the crucible enumerator of the same name, and no value
// for an ordinal that crucible does not name.  The layouts of c10 and of
// crucible have different ordinals from SparseCsc on, so the layout decoder
// maps by name.
void test_c10_decoders_map_by_name() {
    using crucible::vessel::decode_c10_device_type;
    using crucible::vessel::decode_c10_layout;
    using crucible::vessel::decode_c10_scalar_type;

    EXPECT(decode_c10_scalar_type(int8_t{6}).value_or(crucible::ScalarType::Undefined) == crucible::ScalarType::Float,
           "the c10 ordinal of float decodes to Float");
    EXPECT(decode_c10_scalar_type(int8_t{15}).value_or(crucible::ScalarType::Undefined)
               == crucible::ScalarType::BFloat16,
           "the c10 ordinal of bfloat16 decodes to BFloat16");
    EXPECT(decode_c10_scalar_type(int8_t{12}).is_none(), "the c10 ordinal of qint8 decodes to no value");
    EXPECT(decode_c10_scalar_type(int8_t{27}).is_none(), "the c10 ordinal of uint16 decodes to no value");
    EXPECT(decode_c10_scalar_type(int8_t{-1}).is_none(), "a negative ordinal decodes to no value");
    EXPECT(decode_c10_scalar_type(int8_t{44}).value_or(crucible::ScalarType::Undefined)
               == crucible::ScalarType::Float8_e8m0fnu,
           "the c10 ordinal of float8_e8m0fnu decodes to Float8_e8m0fnu");
    EXPECT(decode_c10_scalar_type(int8_t{45}).value_or(crucible::ScalarType::Undefined)
               == crucible::ScalarType::Float4_e2m1fn_x2,
           "the c10 ordinal of float4_e2m1fn_x2 decodes to Float4_e2m1fn_x2");
    EXPECT(crucible::element_size(crucible::ScalarType::Float8_e8m0fnu).raw() == 1,
           "one element of float8_e8m0fnu is one byte");
    EXPECT(crucible::element_size(crucible::ScalarType::Float4_e2m1fn_x2).raw() == 1,
           "one element of float4_e2m1fn_x2 is the byte that holds two values");

    EXPECT(decode_c10_device_type(int8_t{1}).value_or(crucible::DeviceType::CPU) == crucible::DeviceType::CUDA,
           "the c10 ordinal of CUDA decodes to CUDA");
    EXPECT(decode_c10_device_type(int8_t{20}).value_or(crucible::DeviceType::CPU) == crucible::DeviceType::PrivateUse1,
           "the c10 ordinal of PrivateUse1 decodes to PrivateUse1");
    EXPECT(decode_c10_device_type(int8_t{12}).is_none(), "the c10 ordinal of XPU decodes to no value");

    EXPECT(decode_c10_layout(int8_t{0}).value_or(crucible::Layout::Sparse) == crucible::Layout::Strided,
           "the c10 ordinal of Strided decodes to Strided");
    EXPECT(decode_c10_layout(int8_t{2}).value_or(crucible::Layout::Strided) == crucible::Layout::SparseCsr,
           "the c10 ordinal of SparseCsr decodes to SparseCsr");
    EXPECT(decode_c10_layout(int8_t{4}).value_or(crucible::Layout::Strided) == crucible::Layout::SparseCsc,
           "the c10 ordinal of SparseCsc decodes to SparseCsc");
    EXPECT(decode_c10_layout(int8_t{5}).value_or(crucible::Layout::Strided) == crucible::Layout::SparseBsr,
           "the c10 ordinal of SparseBsr decodes to SparseBsr");
    EXPECT(decode_c10_layout(int8_t{6}).value_or(crucible::Layout::Strided) == crucible::Layout::SparseBsc,
           "the c10 ordinal of SparseBsc decodes to SparseBsc");
    EXPECT(decode_c10_layout(int8_t{3}).is_none(), "the c10 ordinal of Mkldnn decodes to no value");
    EXPECT(decode_c10_layout(int8_t{7}).is_none(), "the c10 ordinal of Jagged decodes to no value");
    EXPECT(decode_c10_layout(int8_t{-1}).is_none(), "a negative ordinal decodes to no value");
}

}  // namespace

int main() {
    test_handle_roundtrip();
    test_meta_roundtrip();
    test_meta_zero_n_with_null();
    test_data_ptr_typed();
    test_ffi_records_copy();
    test_ffi_records_read_the_c_records();
    test_ffi_decode_refuses_each_broken_claim();
    test_schema_name_typed();
    test_schema_name_registration_codes();
    test_schema_name_full_code();
    test_abi_version_constant();
    test_handle_distinct_pointers();
    test_ladder_refuses_unnamed_enumerations();
    test_ladder_refuses_a_count_that_disagrees_with_the_entry();
    test_ladder_refuses_a_data_slot_past_the_inline_slots();
    test_c10_decoders_map_by_name();
    test_ffi_records_reuse_the_slots_of_the_producer();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_vessel_api_typed: FAIL (%d)\n", g_failures);
        return EXIT_FAILURE;
    }

    crucible::test::pass("test_vessel_api_typed: PASS\n");
    return EXIT_SUCCESS;
}
