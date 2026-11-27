// Tests of the bound of a TensorDim: each lane of a TensorDimArray holds an
// extent of at most kMaxTensorDimExtent.  The one write of a lane takes a
// TensorDim, and no byte door makes a block (the fixtures of
// test/tensor_neg).  So lanes from outside reach a descriptor only through
// decode_tensor_meta, and each route from bytes to a descriptor refuses a
// size or a stride past the bound:
//
//   - decode_tensor_meta, the door of the fields
//   - read_meta of crucible/Serialize.h, the route of the Cipher store
//   - decode_trace_meta of crucible/TraceLoader.h, a record of today
//   - decode_first_trace_meta_ of crucible/TraceLoader.h, a record of the
//     first version.
//
// The C entry of the vessel has its own test of the two lanes
// (test_vessel_api_typed).

#include <crucible/Serialize.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceLoader.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::crucible::kMaxTensorDimExtent;
using ::crucible::kMaxTensorNDim;
using ::crucible::TensorMeta;
using ::crucible::UncheckedTensorMeta;
using ::fixy::check::pass;

// The block of a descriptor that holds the lane under test.
enum class DimBlock : std::uint8_t {
    Sizes,
    Strides,
};

// The value of the lane under test.
enum class LaneValue : std::uint8_t {
    // kMaxTensorDimExtent, the largest extent of a TensorDim.
    AtBound,
    // One past kMaxTensorDimExtent.
    PastBound,
    // The largest int64_t.
    Top,
};

[[nodiscard]] constexpr std::int64_t extent_of(LaneValue value) noexcept {
    switch (value) {
        case LaneValue::AtBound:
            return kMaxTensorDimExtent;
        case LaneValue::PastBound:
            return kMaxTensorDimExtent + 1;
        case LaneValue::Top:
            return ::fixy::max_of<std::int64_t>;
        default:
            ::fixy::unreachable();
    }
}

using Lanes = ::fixy::FixedArray<std::int64_t, kMaxTensorNDim>;

// The fields of a Float descriptor of rank 2 on the host, with the sizes
// {4, 3} and the strides {3, 1}, and with the value in lane 1 of the block.
[[nodiscard]] UncheckedTensorMeta fields_with(DimBlock block, LaneValue value) noexcept {
    UncheckedTensorMeta fields{};
    fields.ndim = 2;
    fields.dtype = std::to_underlying(::crucible::ScalarType::Float);
    fields.device_type = std::to_underlying(::crucible::DeviceType::CPU);
    fields.device_idx = -1;
    fields.layout = std::to_underlying(::crucible::Layout::Strided);
    fields.sizes.set(::fixy::index<0>(), 4);
    fields.sizes.set(::fixy::index<1>(), 3);
    fields.strides.set(::fixy::index<0>(), 3);
    fields.strides.set(::fixy::index<1>(), 1);
    std::int64_t const extent = extent_of(value);
    Lanes& lanes = block == DimBlock::Sizes ? fields.sizes : fields.strides;
    lanes.set(::fixy::index<1>(), extent);
    return fields;
}

// True when decode_tensor_meta gives a descriptor of the fields.
[[nodiscard]] bool is_decoded(UncheckedTensorMeta const& fields) noexcept {
    return ::crucible::decode_tensor_meta(fields, ::fixy::Option<::crucible::TensorDataKey>{::fixy::none}).is_some();
}

// True when read_meta reads the image of the fields with no refusal.  A
// refused read gives the default descriptor.
[[nodiscard]] bool is_read(UncheckedTensorMeta const& fields) noexcept {
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<UncheckedTensorMeta>> const image =
        ::fixy::build_fixed_array<std::byte, ::fixy::encoded_size_v<UncheckedTensorMeta>>(
            [&fields](auto run) noexcept { ::fixy::encode(fields, std::move(run)); });
    ::crucible::detail_ser::Reader reader{.image = image.view()};
    TensorMeta const meta = ::crucible::detail_ser::read_meta(reader);
    return reader.refusal.is_none() && meta.ndim == fields.ndim;
}

// The record of today of the fields, at the offsets of TensorMeta.
using TraceRecord = ::fixy::FixedArray<std::byte, ::crucible::TRACE_META_BYTES>;

[[nodiscard]] TraceRecord trace_record_of(UncheckedTensorMeta const& fields) noexcept {
    using ::crucible::detail::write_record_field_;
    return ::fixy::build_fixed_array<std::byte, ::crucible::TRACE_META_BYTES>([&fields](auto run) noexcept {
        write_record_field_<offsetof(TensorMeta, sizes)>(run, fields.sizes);
        write_record_field_<offsetof(TensorMeta, strides)>(run, fields.strides);
        write_record_field_<offsetof(TensorMeta, ndim)>(run, fields.ndim);
        write_record_field_<offsetof(TensorMeta, dtype)>(run, fields.dtype);
        write_record_field_<offsetof(TensorMeta, device_type)>(run, fields.device_type);
        write_record_field_<offsetof(TensorMeta, device_idx)>(run, fields.device_idx);
        write_record_field_<offsetof(TensorMeta, layout)>(run, fields.layout);
    });
}

// The record of the first version of the fields, at its fixed offsets.
using FirstTraceRecord = ::fixy::FixedArray<std::byte, ::crucible::TRACE_FIRST_META_BYTES>;

[[nodiscard]] FirstTraceRecord first_trace_record_of(UncheckedTensorMeta const& fields) noexcept {
    namespace detail = ::crucible::detail;
    using detail::write_record_field_;
    return ::fixy::build_fixed_array<std::byte, ::crucible::TRACE_FIRST_META_BYTES>([&fields](auto run) noexcept {
        write_record_field_<detail::kFirstMetaSizesAt>(run, fields.sizes);
        write_record_field_<detail::kFirstMetaStridesAt>(run, fields.strides);
        write_record_field_<detail::kFirstMetaNdimAt>(run, fields.ndim);
        write_record_field_<detail::kFirstMetaDtypeAt>(run, fields.dtype);
        write_record_field_<detail::kFirstMetaDeviceTypeAt>(run, fields.device_type);
        write_record_field_<detail::kFirstMetaDeviceIdxAt>(run, fields.device_idx);
        write_record_field_<detail::kFirstMetaLayoutAt>(run, fields.layout);
    });
}

[[nodiscard]] bool is_trace_decoded(UncheckedTensorMeta const& fields) noexcept {
    TraceRecord const record = trace_record_of(fields);
    return ::crucible::decode_trace_meta(record.view()).is_some();
}

[[nodiscard]] bool is_first_trace_decoded(UncheckedTensorMeta const& fields) noexcept {
    FirstTraceRecord const record = first_trace_record_of(fields);
    return ::crucible::detail::decode_first_trace_meta_(record.view()).is_some();
}

// A route from bytes to a descriptor.
enum class Route : std::uint8_t {
    Fields,
    Store,
    Trace,
    FirstTrace,
};

[[nodiscard]] bool is_admitted(Route route, UncheckedTensorMeta const& fields) noexcept {
    switch (route) {
        case Route::Fields:
            return is_decoded(fields);
        case Route::Store:
            return is_read(fields);
        case Route::Trace:
            return is_trace_decoded(fields);
        case Route::FirstTrace:
            return is_first_trace_decoded(fields);
        default:
            ::fixy::unreachable();
    }
}

// Each route admits a lane at the bound and refuses a lane past it, in the
// block of the sizes and in the block of the strides.
void routes_refuse_a_lane_past_the_bound(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<Route, 4> const routes{Route::Fields, Route::Store, Route::Trace, Route::FirstTrace};
    ::fixy::FixedArray<DimBlock, 2> const blocks{DimBlock::Sizes, DimBlock::Strides};
    for (Route const route : routes.view()) {
        for (DimBlock const block : blocks.view()) {
            CRUCIBLE_TEST_ASSERT(test, is_admitted(route, fields_with(block, LaneValue::AtBound)));
            CRUCIBLE_TEST_ASSERT(test, !is_admitted(route, fields_with(block, LaneValue::PastBound)));
            CRUCIBLE_TEST_ASSERT(test, !is_admitted(route, fields_with(block, LaneValue::Top)));
        }
    }
    pass(test, "  routes_refuse_a_lane_past_the_bound: PASSED\n");
}

// Each route from outside refuses a rank past kMaxTensorNDim and a lane past
// the rank that is not zero, as an error and not an end of the process.  The
// largest rank with zero lanes passes, and a descriptor that passes has the
// shape of its fields.
void routes_refuse_a_rank_past_the_bound(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<Route, 4> const routes{Route::Fields, Route::Store, Route::Trace, Route::FirstTrace};
    UncheckedTensorMeta const matrix = fields_with(DimBlock::Sizes, LaneValue::AtBound);
    UncheckedTensorMeta past_rank = matrix;
    past_rank.ndim = kMaxTensorNDim + 1;
    UncheckedTensorMeta top_rank = matrix;
    top_rank.ndim = 0xFF;
    UncheckedTensorMeta tail_lane = matrix;
    tail_lane.sizes.set(::fixy::index<2>(), 5);
    UncheckedTensorMeta tail_stride = matrix;
    tail_stride.strides.set(::fixy::index<7>(), 1);
    UncheckedTensorMeta largest_rank = matrix;
    largest_rank.ndim = kMaxTensorNDim;
    for (Route const route : routes.view()) {
        CRUCIBLE_TEST_ASSERT(test, is_admitted(route, matrix));
        CRUCIBLE_TEST_ASSERT(test, !is_admitted(route, past_rank));
        CRUCIBLE_TEST_ASSERT(test, !is_admitted(route, top_rank));
        CRUCIBLE_TEST_ASSERT(test, !is_admitted(route, tail_lane));
        CRUCIBLE_TEST_ASSERT(test, !is_admitted(route, tail_stride));
        CRUCIBLE_TEST_ASSERT(test, is_admitted(route, largest_rank));
    }

    ::fixy::Option<TensorMeta> decoded =
        ::crucible::decode_tensor_meta(matrix, ::fixy::Option<::crucible::TensorDataKey>{::fixy::none});
    CRUCIBLE_TEST_ASSERT(test, decoded.is_some());
    TensorMeta const meta = std::move(decoded).expect("the fields of a matrix decode");
    CRUCIBLE_TEST_ASSERT(test, meta.ndim == 2);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(meta.sizes[0]) == 4);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(meta.sizes[1]) == kMaxTensorDimExtent);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(meta.strides[0]) == 3);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(meta.strides[1]) == 1);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(meta.sizes[2]) == 0);
    pass(test, "  routes_refuse_a_rank_past_the_bound: PASSED\n");
}

// A lane of a descriptor reads back the TensorDim of its shape, through the
// subscript of a descriptor that is not const and through the subscript of a
// const descriptor.  A lane that the shape gives as zero reads zero.
void slots_read_the_lanes_of_the_shape(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<::crucible::TensorDim, kMaxTensorNDim> sizes = ::crucible::zero_tensor_dims();
    ::fixy::FixedArray<::crucible::TensorDim, kMaxTensorNDim> strides = ::crucible::zero_tensor_dims();
    sizes.set(::fixy::index<2>(), ::crucible::tensor_dim(kMaxTensorDimExtent));
    strides.set(::fixy::index<7>(), ::crucible::tensor_dim(5));
    TensorMeta meta{};
    meta.set_shape(
        ::crucible::mint_tensor_shape(::crucible::TensorSizes{sizes.view()}, ::crucible::TensorStrides{strides.view()})
            .expect("two counts of the rank bound make a shape"));
    TensorMeta const& held = meta;
    ::crucible::TensorDim const size = held.sizes[2];
    ::crucible::TensorDim const stride = meta.strides[7];
    CRUCIBLE_TEST_ASSERT(test, size.value() == kMaxTensorDimExtent);
    CRUCIBLE_TEST_ASSERT(test, stride.value() == 5);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(held.sizes[0]) == 0);
    pass(test, "  slots_read_the_lanes_of_the_shape: PASSED\n");
}

// A shape from the lanes of a descriptor gives a second descriptor the same
// lanes, and each lane past the rank of the new shape reads zero.
void lanes_copy_through_a_shape(::fixy::TestRunnerCtx const& test) noexcept {
    const ::fixy::FixedArray<::crucible::TensorDim, 3> sizes{::crucible::tensor_dim(7), ::crucible::tensor_dim(7),
                                                             ::crucible::tensor_dim(2)};
    TensorMeta source{};
    source.set_shape(::crucible::contiguous_tensor_shape(::crucible::TensorSizes{sizes.view()})
                         .expect("a contiguous shape of rank 3"));
    const ::fixy::FixedArray<::crucible::TensorDim, 2> copied_sizes{
        static_cast<::crucible::TensorDim>(source.sizes[0]), static_cast<::crucible::TensorDim>(source.sizes[1])};
    const ::fixy::FixedArray<::crucible::TensorDim, 2> copied_strides{
        static_cast<::crucible::TensorDim>(source.strides[0]), static_cast<::crucible::TensorDim>(source.strides[1])};
    TensorMeta meta{};
    meta.set_shape(::crucible::mint_tensor_shape(::crucible::TensorSizes{copied_sizes.view()},
                                                 ::crucible::TensorStrides{copied_strides.view()})
                       .expect("a shape of rank 2"));
    CRUCIBLE_TEST_ASSERT(test, meta.ndim == 2);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(meta.sizes[0]) == 7);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(meta.strides[0]) == 14);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(meta.strides[1]) == 2);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(meta.sizes[2]) == 0);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::raw_tensor_dim(meta.strides[2]) == 0);
    pass(test, "  lanes_copy_through_a_shape: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    routes_refuse_a_lane_past_the_bound(test);
    routes_refuse_a_rank_past_the_bound(test);
    slots_read_the_lanes_of_the_shape(test);
    lanes_copy_through_a_shape(test);
    pass(test, "test_tensor_dim_bound: all tests passed\n");
    return 0;
}
