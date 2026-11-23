#include "../vessel/torch/vessel_api_typed.h"

#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Lifetime.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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

#define EXPECT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ::fixy::report(::fixy::Sink::Err, "FAIL: {} -- {} ({}:{})\n", ::fixy::TextView{#cond}, \
                           ::fixy::TextView{msg}, ::fixy::TextView{__FILE__}, __LINE__);           \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

// The bytes of a Vigil that no constructor starts: the target of a handle
// that the cases compare and never read.
using VigilBytes = ::fixy::FixedArray<std::byte, sizeof(crucible::Vigil)>;

// The typed records of a pair that the door admits.  Each case that calls it
// gives an aligned array of count records, so the door gives a value.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the case gives the records of a C caller as a pointer and a count")
[[nodiscard]] TypedMeta typed_records(const CrucibleMeta* metas, std::size_t count) noexcept {
    return crucible::vessel::as_meta_typed(metas, count).expect("the case gives an aligned array of count records");
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

void test_handle_roundtrip() {
    alignas(crucible::Vigil) VigilBytes storage{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the case gives a CrucibleHandle of vessel_api.h to the typed door")
    crucible::Vigil* const vigil_ptr = static_cast<crucible::Vigil*>(static_cast<void*>(&storage));
    CrucibleHandle const raw = static_cast<CrucibleHandle>(vigil_ptr);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    auto typed = crucible::vessel::as_vigil_typed(raw);
    EXPECT(typed.value() == vigil_ptr, "typed handle must preserve the Vigil pointer value");

    EXPECT(crucible::vessel::from_typed(typed) == raw, "typed handle must roundtrip to CrucibleHandle");

    // The wrapper must hold exactly the bytes of the raw pointer.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the case compares the bytes of a typed handle with the bytes of its pointer")
    EXPECT(std::memcmp(&typed, &raw, sizeof(raw)) == 0, "TypedHandle layout must be byte-identical to CrucibleHandle");
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
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
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the case writes the data pointer of a CrucibleMeta of vessel_api.h")
    meta.data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xCAFEBABE));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
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

    const ::fixy::FixedArray<CrucibleMeta, 2> arr{meta, meta};
    const CrucibleMeta& first = arr.at(::fixy::index<0>());

    // The count is passed explicitly.  A count above zero keeps the
    // pointer of the caller.
    auto typed_single = typed_records(&first, 1);
    EXPECT(typed_single.value() != nullptr, "as_meta_typed on non-null meta must return non-null pointer");

    auto typed_multi = typed_records(&first, 2);
    EXPECT(typed_multi.value() == &first, "as_meta_typed pointer must match the raw pointer");

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
    const bool is_decoded_whole =
        crucible::vessel::decode_ffi_meta(arr.at(::fixy::index<1>()))
            .match([&expected](crucible::TensorMeta decoded) noexcept { return decoded == expected; },
                   [] noexcept { return false; });
    EXPECT(is_decoded_whole, "the decode must give each field of the record");

    EXPECT(crucible::vessel::metas_from_typed(typed_multi) == &first,
           "metas_from_typed must round-trip to the original CrucibleMeta pointer");

    // The typed view must hold exactly the bytes of the pointer it
    // wraps.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the case compares the bytes of a typed meta with the bytes of its pointer")
    const CrucibleMeta* const raw_ptr = &first;
    EXPECT(std::memcmp(&typed_multi, &raw_ptr, sizeof(raw_ptr)) == 0,
           "TypedMeta layout must be byte-identical to const CrucibleMeta*");
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

void test_meta_zero_n_with_null() {
    // A zero count reads no record.  The door gives a typed null that
    // round-trips, which is what an op with no tensor arguments uses.
    auto typed = typed_records(nullptr, 0);
    EXPECT(typed.value() == nullptr, "as_meta_typed(nullptr, 0) must return null typed view");

    EXPECT(crucible::vessel::metas_from_typed(typed) == nullptr,
           "metas_from_typed of null typed view must round-trip to nullptr");
}

// The door of the arrays of a C caller tests each pointer and count pair, and
// gives no value for a pair that breaks the rule of the C interface.  A
// contract check judges no pointer of a caller, so each pair gives a value or
// no value, and the process lives.
void test_array_door_judges_each_pair() {
    const ::fixy::FixedArray<CrucibleMeta, 2> records{};
    const CrucibleMeta& first = records.at(::fixy::index<0>());
    const ::fixy::FixedArray<int64_t, 5> scalars{};
    const auto gives_null = [](TypedMeta typed) noexcept { return typed.value() == nullptr; };
    const auto gives_nothing = [] noexcept { return false; };

    // The pairs of a C caller hold raw pointers: a null pointer, a pointer
    // one byte past an aligned record, which does not have the alignment of
    // a CrucibleMeta, and the out arrays of the two entries that write.  The
    // door reads no byte through a pointer.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the case gives the door the pointer and count pairs of a C caller")
    const auto* const misaligned = std::bit_cast<const CrucibleMeta*>(std::bit_cast<std::uintptr_t>(&first) + 1U);
    EXPECT(crucible::vessel::as_meta_typed(nullptr, 2).is_none(), "a null array with a count above 0 is refused");
    EXPECT(crucible::vessel::as_meta_typed(&first, 2).is_some(), "an aligned array with a count above 0 is admitted");
    EXPECT(crucible::vessel::as_meta_typed(misaligned, 1).is_none(), "a misaligned array with a count is refused");

    // A pointer with a count of 0 reads no record, so the door admits each
    // pointer and gives the null pointer.  A ctypes caller gives the buffer
    // of an empty array in such a pair.
    EXPECT(crucible::vessel::as_meta_typed(&first, 0).match(gives_null, gives_nothing),
           "a pointer with a count of 0 gives the null pointer");
    EXPECT(crucible::vessel::as_meta_typed(misaligned, 0).match(gives_null, gives_nothing),
           "a misaligned pointer with a count of 0 gives the null pointer");

    // The same door takes each element type of an array of the C interface.
    EXPECT(crucible::vessel::as_array_typed(static_cast<const int64_t*>(nullptr), 5).is_none(),
           "a null scalar array with a count above 0 is refused");
    EXPECT(crucible::vessel::as_array_typed(&scalars.at(::fixy::index<0>()), 5).is_some(),
           "a scalar array with a count is admitted");
    EXPECT(crucible::vessel::as_array_typed(static_cast<CrucibleSlot*>(nullptr), 0).is_some(),
           "a null out array with a capacity of 0 is a read of the count");
    EXPECT(crucible::vessel::as_array_typed(static_cast<CrucibleSlot*>(nullptr), 3).is_none(),
           "a null out array with a capacity above 0 is refused");
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

void test_data_ptr_typed() {
    // The typed read must return the same bits the wire struct holds,
    // only carrying provenance.
    ::fixy::FixedArray<CrucibleMeta, 3> arr{};
    const CrucibleMeta& first = arr.at(::fixy::index<0>());
    const CrucibleMeta& second = arr.at(::fixy::index<1>());
    const CrucibleMeta& third = arr.at(::fixy::index<2>());
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the case writes the data pointers of the CrucibleMeta records of vessel_api.h")
    arr.with_element(::fixy::index<0>(), [](CrucibleMeta& record) noexcept {
        record.data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xAAAA0000));
    });
    arr.with_element(::fixy::index<1>(), [](CrucibleMeta& record) noexcept {
        record.data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xBBBB0000));
    });
    arr.with_element(::fixy::index<2>(), [](CrucibleMeta& record) noexcept {
        record.data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xCCCC0000));
    });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    auto typed_arr = typed_records(&first, 3);

    auto p0 = crucible::vessel::data_ptr_typed(typed_arr, 0);
    auto p1 = crucible::vessel::data_ptr_typed(typed_arr, 1);
    auto p2 = crucible::vessel::data_ptr_typed(typed_arr, 2);

    EXPECT(p0.value() == first.data_ptr, "data_ptr_typed[0] must read the wire pointer");
    EXPECT(p1.value() == second.data_ptr, "data_ptr_typed[1] must read the wire pointer");
    EXPECT(p2.value() == third.data_ptr, "data_ptr_typed[2] must read the wire pointer");

    // Distinct inputs stay distinct, so nothing is cached or aliased.
    EXPECT(p0.value() != p1.value(), "distinct meta data_ptrs must yield distinct typed values");
    EXPECT(p1.value() != p2.value(), "distinct meta data_ptrs must yield distinct typed values");

    // The wrapper must hold exactly the bytes of the bare pointer.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the case compares the bytes of a typed data pointer with the C pointer")
    EXPECT(std::memcmp(&p0, &first.data_ptr, sizeof(first.data_ptr)) == 0,
           "ExternalDataPtr layout must be byte-identical to void*");
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
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
    sources.with_element(::fixy::index<0>(), [](CrucibleMeta& source) noexcept { source.ndim = 1; });
    sources.with_element(::fixy::index<1>(), [](CrucibleMeta& source) noexcept { source.ndim = 2; });
    sources.with_element(::fixy::index<2>(), [](CrucibleMeta& source) noexcept { source.ndim = 3; });
    const TypedMeta typed = typed_records(&sources.at(::fixy::index<0>()), 3);

    ::fixy::FixedArray<crucible::TensorMeta, 3> slots{};
    const auto ranks = slots.with_mut(
        [typed](auto run) noexcept { return collect_ranks(crucible::vessel::decode_ffi_records(typed, run.view())); });
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
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the case writes the data pointer of a CrucibleMeta of vessel_api.h")
    written.data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0x2000U));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    written.ndim = 1;
    written.dtype = static_cast<int8_t>(crucible::ScalarType::Float);
    written.requires_grad = 1;
    written.storage_nbytes = 444;
    written.grad_fn_hash = 0xABC2U;
    ::fixy::FixedArray<CrucibleMeta, 2> callers{};
    callers.with_element(::fixy::index<0>(), [written](CrucibleMeta& caller) noexcept { caller = written; });
    callers.with_element(::fixy::index<1>(), [written](CrucibleMeta& caller) noexcept { caller = written; });

    // The View of the records borrows the slots, so the reads stay in the
    // frame of the slots.
    ::fixy::FixedArray<crucible::TensorMeta, 2> slots{};
    const TypedMeta typed = typed_records(&callers.at(::fixy::index<0>()), 2);
    slots.with_mut([typed, written](auto run) noexcept {
        const ::fixy::View<const crucible::TensorMeta> records =
            crucible::vessel::decode_ffi_records(typed, run.view())
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
    });
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
    const TypedMeta typed = typed_records(&callers.at(::fixy::index<0>()), 2);
    const bool is_refused = slots.with_mut(
        [typed](auto run) noexcept { return crucible::vessel::decode_ffi_records(typed, run.view()).is_none(); });
    EXPECT(is_refused, "an op with a refused record is refused");
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
    sources.with_mut([&rank](auto run) noexcept {
        for (CrucibleMeta& source : run) {
            source.ndim = rank;
            ++rank;
        }
    });
    sources.with_element(::fixy::index<5>(), [](CrucibleMeta& source) noexcept { source.requires_grad = 2; });
    const CrucibleMeta& first = sources.at(::fixy::index<0>());
    const CrucibleMeta& fourth = sources.at(::fixy::index<3>());
    const auto typed_at = [](const CrucibleMeta& record, uint32_t count) noexcept {
        return typed_records(&record, count);
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

    const auto none_read = decode_in_frame(stack, typed_records(nullptr, 0), 0);
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
    crucible::global_schema_table().mint_mutable_view(fg).match(
        [&](crucible::SchemaTable::MutableView view) {
            const bool was_a_registered =
                crucible::register_schema_name(
                    fg, ::foundation::effects::testing::test().alloc, view, crucible::SchemaHash{hash_a},
                    ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(::fixy::TextView{"aten::test_op_a"}))
                    .is_ok();
            EXPECT(was_a_registered, "an open schema table must take the first registration");
            const bool was_b_registered =
                crucible::register_schema_name(
                    fg, ::foundation::effects::testing::test().alloc, view, crucible::SchemaHash{hash_b},
                    ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(::fixy::TextView{"aten::test_op_b"}))
                    .is_ok();
            EXPECT(was_b_registered, "an open schema table must take the second registration");
        },
        [] {});

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

    // A table keeps the characters before the first zero of a name, so a
    // name that starts with a zero would register an empty name, and a name
    // with a zero inside would register other characters than it holds.
    ::fixy::FixedText<8> leading_zero{};
    ::fixy::FixedText<8> inner_zero{};
    for (const char character : ::fixy::TextView{"aten::"})
        EXPECT(inner_zero.append(character).is_ok(), "the text holds the characters of the case");
    EXPECT(leading_zero.append('\0').is_ok() && leading_zero.append('x').is_ok(), "the text holds a zero and a letter");
    EXPECT(inner_zero.append('\0').is_ok() && inner_zero.append('y').is_ok(), "the text holds a zero and a letter");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, held_hash + 2, leading_zero.view())
               == CRUCIBLE_SCHEMA_NAME_REFUSED,
           "a name that starts with a zero gives the refusal code");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, held_hash + 3, inner_zero.view())
               == CRUCIBLE_SCHEMA_NAME_REFUSED,
           "a name with a zero inside gives the refusal code");
    EXPECT(crucible::vessel::schema_name_typed(crucible::SchemaHash{held_hash + 2}).is_none()
               && crucible::vessel::schema_name_typed(crucible::SchemaHash{held_hash + 3}).is_none(),
           "a refused name leaves the tables as they were");

    // The C door takes a name of SCHEMA_NAME_MAX characters, the limit of the
    // validator that the trace loader calls too, and refuses one character
    // more.  The old door refused each name past 256 characters.
    ::fixy::FixedText<crucible::SCHEMA_NAME_MAX + 1> longest{};
    for (std::size_t character = 0; character < crucible::SCHEMA_NAME_MAX; ++character)
        EXPECT(longest.append('n').is_ok(), "the text holds the characters of the case");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, held_hash + 4, longest.view())
               == CRUCIBLE_SCHEMA_NAME_REGISTERED,
           "a name of SCHEMA_NAME_MAX characters registers");
    const auto longest_held = crucible::vessel::schema_name_typed(crucible::SchemaHash{held_hash + 4});
    const bool holds_longest = longest_held.match(
        [&longest](TypedSchemaName const& found) noexcept { return found.value().view() == longest.view(); },
        [] noexcept { return false; });
    EXPECT(holds_longest, "the table holds each character of the longest name");
    EXPECT(longest.append('n').is_ok(), "the text holds one character more");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, held_hash + 5, longest.view())
               == CRUCIBLE_SCHEMA_NAME_REFUSED,
           "a name of one character past SCHEMA_NAME_MAX gives the refusal code");
    EXPECT(crucible::vessel::schema_name_typed(crucible::SchemaHash{held_hash + 5}).is_none(),
           "a refused long name leaves the tables as they were");
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

// An operator of the generated table reserves its schema hash.  The door of
// an outside name refuses a different name of that hash with the collision
// code, and it takes the full name of the operator.  So the internal
// registration of the operator, which its unboxed kernel makes when the
// operator first records, finds no outside name on its hash, and the process
// lives.  A door with no reserved hashes registers the outside name, and the
// internal registration then ends the process at the collision.
void test_schema_name_reserved_hash() {
    constexpr crucible::vessel::OpEntry plain = crucible::vessel::aten_op_row<0>();
    constexpr crucible::vessel::OpEntry overloaded = crucible::vessel::aten_op_row<1>();
    static_assert(plain.overload.size() == 0 && overloaded.overload.size() != 0,
                  "the first row has no overload, and the second row has one");
    const crucible::SchemaHash plain_hash{plain.schema_hash};
    crucible::Vigil vigil;

    EXPECT(crucible::vessel::register_schema_name_typed(vigil, plain.schema_hash, ::fixy::TextView{"crucible::other"})
               == CRUCIBLE_SCHEMA_NAME_COLLISION,
           "a different name of the hash of an operator of the table gives the collision code");
    EXPECT(crucible::vessel::schema_name_typed(plain_hash).is_none(),
           "a refused name of a reserved hash leaves the tables as they were");
    vigil.register_schema_name(plain_hash, ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(plain.name));
    const auto held_name = crucible::vessel::schema_name_typed(plain_hash);
    const bool holds_operator =
        held_name.match([&plain](TypedSchemaName const& found) noexcept { return found.value().view() == plain.name; },
                        [] noexcept { return false; });
    EXPECT(holds_operator, "the internal registration of the operator lands, and the process lives");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, plain.schema_hash, plain.name)
               == CRUCIBLE_SCHEMA_NAME_REGISTERED,
           "the name of the operator registers from outside too");

    ::fixy::FixedText<crucible::SCHEMA_NAME_MAX> full_name{};
    EXPECT(full_name.append(overloaded.name).is_ok() && full_name.append('.').is_ok()
               && full_name.append(overloaded.overload).is_ok(),
           "the text holds the full name of the second row");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, overloaded.schema_hash, overloaded.name)
               == CRUCIBLE_SCHEMA_NAME_COLLISION,
           "the name of an operator with no overload is a different name of the hash of its overload");
    EXPECT(crucible::vessel::register_schema_name_typed(vigil, overloaded.schema_hash, full_name.view())
               == CRUCIBLE_SCHEMA_NAME_REGISTERED,
           "the full name of an operator with an overload registers from outside");
}

// The table index of the row of the hash by a walk of each row of the table,
// the oracle of the search of the hash order, or kAtenOpCount for no row.
// Complexity: linear in the rows.
[[nodiscard]] std::size_t walk_to_index_of(crucible::SchemaHash hash) noexcept {
    std::size_t index = 0;
    std::size_t found = crucible::vessel::kAtenOpCount;
    crucible::vessel::for_each_aten_op_row([hash, &index, &found](crucible::vessel::OpEntry const& row) noexcept {
        if (found == crucible::vessel::kAtenOpCount && row.schema_hash == hash.raw()) found = index;
        ++index;
    });
    return found;
}

// The table index that the search of the hash order gives, or kAtenOpCount
// for no row.
[[nodiscard]] std::size_t search_index_of(crucible::SchemaHash hash) noexcept {
    return crucible::vessel::aten_op_index_of(hash).match(
        [](crucible::vessel::AtenOpIndex index) noexcept { return std::size_t{index.raw()}; },
        [] noexcept { return std::size_t{crucible::vessel::kAtenOpCount}; });
}

// The hash order of the generated table: each hash of a part is above the
// hash before it, the last hash of each part is the last hash of the part,
// the order holds one place for each row, and the search finds the table
// index of each row, as the walk does.  A hash that no row has gives no row:
// below the first hash, above the last hash, between two neighbors of the
// order, the largest hash, and the hash one.
void test_hash_order_finds_each_row() {
    std::size_t places = 0;
    uint64_t earlier = 0;
    bool is_rising = true;
    bool is_each_last_hash_held = true;
    std::size_t part_number = 0;
    for (const ::fixy::View<uint64_t const> part : crucible::vessel::aten_op_hash_parts) {
        for (const uint64_t hash : part) {
            is_rising = is_rising && (places == 0 || hash > earlier);
            earlier = hash;
            ++places;
        }
        const uint64_t last_hash = *crucible::vessel::aten_op_last_hash_of_part.get(part_number).expect("a part");
        is_each_last_hash_held = is_each_last_hash_held && last_hash == earlier;
        ++part_number;
    }
    EXPECT(is_rising, "each hash of the order is above the hash before it");
    EXPECT(is_each_last_hash_held, "the last hash of each part is the last hash that the part holds");
    EXPECT(places == crucible::vessel::kAtenOpCount, "the order holds one place for each row");

    std::size_t index = 0;
    std::size_t found_rows = 0;
    std::size_t agreed_rows = 0;
    std::size_t read_rows = 0;
    auto check_row = [&index, &found_rows, &agreed_rows, &read_rows](crucible::vessel::OpEntry const& row) noexcept {
        const crucible::SchemaHash hash{row.schema_hash};
        const std::size_t searched = search_index_of(hash);
        if (searched == index) ++found_rows;
        if (searched == walk_to_index_of(hash)) ++agreed_rows;
        const bool is_same_row = crucible::vessel::aten_op_index_of(hash).match(
            [&row](crucible::vessel::AtenOpIndex held) noexcept {
                crucible::vessel::OpEntry const& read = crucible::vessel::aten_op_row_at(held);
                return read.schema_hash == row.schema_hash && read.name == row.name && read.overload == row.overload;
            },
            [] noexcept { return false; });
        if (is_same_row) ++read_rows;
        ++index;
    };
    crucible::vessel::for_each_aten_op_row(check_row);
    EXPECT(found_rows == crucible::vessel::kAtenOpCount, "the search finds the table index of each row");
    EXPECT(agreed_rows == crucible::vessel::kAtenOpCount, "the search and the walk give one index for each hash");
    EXPECT(read_rows == crucible::vessel::kAtenOpCount, "the row at the index of each hash is the row of the hash");

    const uint64_t first_hash =
        *(*crucible::vessel::aten_op_hash_parts.get(0).expect("a part")).get(0).expect("a hash");
    const uint64_t second_hash =
        *(*crucible::vessel::aten_op_hash_parts.get(0).expect("a part")).get(1).expect("a hash");
    const uint64_t last_hash =
        *crucible::vessel::aten_op_last_hash_of_part.get(crucible::vessel::aten_op_last_hash_of_part.size() - 1)
             .expect("a part");
    const ::fixy::FixedArray<crucible::SchemaHash, 5> misses{
        crucible::SchemaHash{first_hash - 1}, crucible::SchemaHash{last_hash + 1},
        crucible::SchemaHash{first_hash + (second_hash - first_hash) / 2}, crucible::SchemaHash{~uint64_t{0}},
        crucible::SchemaHash{1}};
    for (const crucible::SchemaHash miss : misses) {
        EXPECT(walk_to_index_of(miss) == crucible::vessel::kAtenOpCount, "the walk finds no row of the hash");
        EXPECT(crucible::vessel::aten_op_index_of(miss).is_none(), "the search finds no row of the hash");
    }
}

void test_handle_distinct_pointers() {
    // Two storage blocks give two pointers, and the wrapper must keep
    // them apart rather than collapse or reuse a value.
    alignas(crucible::Vigil) VigilBytes storage_a{};
    alignas(crucible::Vigil) VigilBytes storage_b{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the case gives two CrucibleHandle values of vessel_api.h to the typed door")
    crucible::Vigil* const vigil_a = static_cast<crucible::Vigil*>(static_cast<void*>(&storage_a));
    crucible::Vigil* const vigil_b = static_cast<crucible::Vigil*>(static_cast<void*>(&storage_b));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    EXPECT(vigil_a != vigil_b, "test setup: distinct storage must yield distinct pointers");

    auto ta = crucible::vessel::as_vigil_typed(static_cast<CrucibleHandle>(vigil_a));
    auto tb = crucible::vessel::as_vigil_typed(static_cast<CrucibleHandle>(vigil_b));
    EXPECT(ta.value() == vigil_a, "typed handle for a must alias a");
    EXPECT(tb.value() == vigil_b, "typed handle for b must alias b");
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
    metas.with_mut([](auto run) noexcept {
        for (crucible::TensorMeta& meta : run) {
            meta.ndim = 1;
            meta.dtype = crucible::ScalarType::Float;
        }
    });
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
    EXPECT(crucible::entry_is_well_formed(entry), "data slots below the count of five are well formed");
    entry.data_slot_bits = 0x21;
    EXPECT(!crucible::entry_is_well_formed(entry), "a data slot past the five inline slots is refused");
    entry.data_slot_bits = 0x80;
    EXPECT(!crucible::entry_is_well_formed(entry), "the high bit of the word is refused");
    entry.num_scalar_args = 2;
    entry.data_slot_bits = 0x04;
    EXPECT(!crucible::entry_is_well_formed(entry), "a data slot past the count is refused");
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
    test_array_door_judges_each_pair();
    test_data_ptr_typed();
    test_ffi_records_copy();
    test_ffi_records_read_the_c_records();
    test_ffi_decode_refuses_each_broken_claim();
    test_schema_name_typed();
    test_schema_name_registration_codes();
    test_schema_name_full_code();
    test_schema_name_reserved_hash();
    test_hash_order_finds_each_row();
    test_handle_distinct_pointers();
    test_ladder_refuses_unnamed_enumerations();
    test_ladder_refuses_a_count_that_disagrees_with_the_entry();
    test_ladder_refuses_a_data_slot_past_the_inline_slots();
    test_c10_decoders_map_by_name();
    test_ffi_records_reuse_the_slots_of_the_producer();

    if (g_failures != 0) {
        ::fixy::report(::fixy::Sink::Err, "test_vessel_api_typed: FAIL ({})\n", g_failures);
        return EXIT_FAILURE;
    }

    ::fixy::check::pass(kTest, "test_vessel_api_typed: PASS\n");
    return EXIT_SUCCESS;
}
