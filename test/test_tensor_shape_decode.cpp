// Tests of decode_tensor_shape, the door of the shape of a writer outside the
// process.  The door checks the two blocks of lanes in one vector compare.
// Each case compares its answer with the scalar predicate of the claims, bit
// for bit, and an admitted shape holds the rank and each lane of its fields.
// The build of each level of the gate runs the vector arm of that level.
// The sweep draws its fields from Philox4x32-10, so each run and each host
// draws the same fields.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

using ::crucible::kMaxTensorDimExtent;
using ::crucible::kMaxTensorNDim;
using ::crucible::TensorShape;
using ::crucible::UncheckedTensorMeta;
using ::fixy::check::pass;
using Lanes = ::fixy::FixedArray<std::int64_t, kMaxTensorNDim>;

// The block of the lane under test.
enum class DimBlock : std::uint8_t {
    Sizes,
    Strides,
};

// The values of a lane that each case writes: zero, one, minus one, the
// bound, one past the bound, and the two ends of int64_t.
constexpr ::fixy::FixedArray<std::int64_t, 7> kLaneValues{
    0, 1, -1, kMaxTensorDimExtent, kMaxTensorDimExtent + 1, ::fixy::min_of<std::int64_t>, ::fixy::max_of<std::int64_t>};

// The claims of the shape of the fields, one lane at a time: the rank is at
// most kMaxTensorNDim, each size and each stride below the rank is at most
// kMaxTensorDimExtent, and each lane at or past the rank is zero.
[[nodiscard]] bool holds_shape_claims(UncheckedTensorMeta const& fields) noexcept {
    if (fields.ndim > kMaxTensorNDim) return false;
    for (const ::crucible::TensorDimArray::LaneIndex lane : Lanes::indices()) {
        const std::int64_t size = fields.sizes.at(lane);
        const std::int64_t stride = fields.strides.at(lane);
        const bool is_below_rank = lane.raw() < fields.ndim;
        const bool is_lane_held =
            is_below_rank ? size <= kMaxTensorDimExtent && stride <= kMaxTensorDimExtent : size == 0 && stride == 0;
        if (!is_lane_held) return false;
    }
    return true;
}

// True when the shape holds the rank and each lane of the fields.
[[nodiscard]] bool holds_fields(TensorShape const& shape, UncheckedTensorMeta const& fields) noexcept {
    if (shape.ndim() != fields.ndim) return false;
    for (const ::crucible::TensorDimArray::LaneIndex lane : Lanes::indices()) {
        if (shape.sizes()[lane.raw()].value() != fields.sizes.at(lane)) return false;
        if (shape.strides()[lane.raw()].value() != fields.strides.at(lane)) return false;
    }
    return true;
}

// True when the door gives a shape exactly when the claims hold, the shape
// holds the fields, and decode_tensor_meta gives a descriptor exactly when
// the claims hold.  The other fields of each case name enumerators.
[[nodiscard]] bool agrees_with_claims(UncheckedTensorMeta const& fields) noexcept {
    const bool is_held = holds_shape_claims(fields);
    const ::fixy::Option<TensorShape> decoded = ::crucible::decode_tensor_shape(fields);
    const bool is_shape_right = decoded.match(
        [&fields, is_held](TensorShape const& shape) noexcept { return is_held && holds_fields(shape, fields); },
        [is_held] noexcept { return !is_held; });
    const bool is_meta_decoded =
        ::crucible::decode_tensor_meta(fields, ::fixy::Option<::crucible::TensorDataKey>{::fixy::none}).is_some();
    return is_shape_right && is_meta_decoded == is_held;
}

// The fields of rank ndim, with value in the lane of the block and zero in
// each other lane.
[[nodiscard]] UncheckedTensorMeta fields_with(std::uint8_t ndim, DimBlock block,
                                              ::crucible::TensorDimArray::LaneIndex lane, std::int64_t value) noexcept {
    UncheckedTensorMeta fields{};
    fields.ndim = ndim;
    Lanes& lanes = block == DimBlock::Sizes ? fields.sizes : fields.strides;
    lanes.set(lane, value);
    return fields;
}

// Each rank up to kMaxTensorNDim, each lane of each block, and each value of
// kLaneValues: a lane below the rank is admitted exactly when it is at most
// the bound, and a lane at or past the rank exactly when it is zero.
void each_lane_agrees(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<DimBlock, 2> const blocks{DimBlock::Sizes, DimBlock::Strides};
    for (std::uint8_t ndim = 0; ndim <= kMaxTensorNDim; ++ndim) {
        for (const ::crucible::TensorDimArray::LaneIndex lane : Lanes::indices()) {
            for (DimBlock const block : blocks.view()) {
                for (std::int64_t const value : kLaneValues.view()) {
                    UncheckedTensorMeta const fields = fields_with(ndim, block, lane, value);
                    const bool is_expected = lane.raw() < ndim ? value <= kMaxTensorDimExtent : value == 0;
                    CRUCIBLE_TEST_ASSERT(test, agrees_with_claims(fields));
                    CRUCIBLE_TEST_ASSERT(test, ::crucible::decode_tensor_shape(fields).is_some() == is_expected);
                }
            }
        }
    }
    pass(test, "  each_lane_agrees: PASSED\n");
}

// Each rank past kMaxTensorNDim is refused, with zero lanes and with lanes
// at the bound.  The door tests the rank before it loads a lane.
void each_rank_past_the_bound_is_refused(::fixy::TestRunnerCtx const& test) noexcept {
    for (unsigned rank = kMaxTensorNDim + 1u; rank <= 0xFFu; ++rank) {
        UncheckedTensorMeta zero_lanes{};
        zero_lanes.ndim = static_cast<std::uint8_t>(rank);
        UncheckedTensorMeta bound_lanes = zero_lanes;
        for (const ::crucible::TensorDimArray::LaneIndex lane : Lanes::indices()) {
            bound_lanes.sizes.set(lane, kMaxTensorDimExtent);
            bound_lanes.strides.set(lane, kMaxTensorDimExtent);
        }
        CRUCIBLE_TEST_ASSERT(test, ::crucible::decode_tensor_shape(zero_lanes).is_none());
        CRUCIBLE_TEST_ASSERT(test, ::crucible::decode_tensor_shape(bound_lanes).is_none());
        CRUCIBLE_TEST_ASSERT(test, agrees_with_claims(zero_lanes));
        CRUCIBLE_TEST_ASSERT(test, agrees_with_claims(bound_lanes));
    }
    pass(test, "  each_rank_past_the_bound_is_refused: PASSED\n");
}

// The Philox key of the sweep.
constexpr std::uint64_t kSweepKey = 0x5EEDC0DEFACE1357ULL;

// The number of the trials of the sweep.
constexpr std::uint64_t kSweepTrials = 4096;

// The blocks of one trial: one for the rank, and one for each lane.
constexpr std::uint64_t kBlocksPerTrial = 1 + kMaxTensorNDim;

// The word of the block at a position.
[[nodiscard]] std::uint32_t word_of(::fixy::Philox4x32::Counter const& block, ::fixy::Index<4> position) noexcept {
    return *block.get(position.raw()).expect("a block holds four words");
}

// A value of a lane from a word and a random value: one of kLaneValues, or
// the random value.
[[nodiscard]] std::int64_t pick_value(std::uint32_t word, std::int64_t random) noexcept {
    const std::uint32_t choice = word % 8;
    if (choice == 7) return random;
    return *kLaneValues.view().get(choice).expect("the choice is below the count of the values");
}

// A value of a lane at the position.  In seven cases of eight it is a value
// that the claims admit: below the rank the random value masked to the bound,
// which is one less than a power of two, and at or past the rank zero.  In
// the other case it is a value of pick_value.  So the sweep holds admitted
// shapes of each rank, and refused ones.
static_assert(::fixy::popcount(static_cast<std::uint64_t>(kMaxTensorDimExtent) + 1) == 1,
              "the mask of draw_lane needs a bound that is one less than a power of two");

[[nodiscard]] std::int64_t draw_lane(bool is_below_rank, std::uint32_t word, std::int64_t random) noexcept {
    const bool is_admitted_value = (word & 7) != 0;
    if (is_admitted_value) return is_below_rank ? (random & kMaxTensorDimExtent) : 0;
    return pick_value(word >> 3, random);
}

// The fields of one trial of the sweep: a rank from 0 to kMaxTensorNDim + 1,
// and a size and a stride for each lane.
[[nodiscard]] UncheckedTensorMeta sweep_fields(std::uint64_t trial) noexcept {
    const std::uint64_t first = trial * kBlocksPerTrial;
    ::fixy::Philox4x32::Counter const rank = ::fixy::Philox4x32::generate(first, kSweepKey);
    UncheckedTensorMeta fields{};
    fields.ndim = static_cast<std::uint8_t>(word_of(rank, ::fixy::index<0>()) % (kMaxTensorNDim + 2u));
    for (const ::crucible::TensorDimArray::LaneIndex lane : Lanes::indices()) {
        ::fixy::Philox4x32::Counter const block = ::fixy::Philox4x32::generate(first + 1 + lane.raw(), kSweepKey);
        const std::uint64_t high = word_of(block, ::fixy::index<2>());
        const std::uint64_t low = word_of(block, ::fixy::index<3>());
        const auto random = static_cast<std::int64_t>((high << 32) | low);
        const bool is_below_rank = lane.raw() < fields.ndim;
        fields.sizes.set(lane, draw_lane(is_below_rank, word_of(block, ::fixy::index<0>()), random));
        fields.strides.set(lane, draw_lane(is_below_rank, word_of(block, ::fixy::index<1>()), ~random));
    }
    return fields;
}

// Each trial of the sweep agrees with the claims.  The sweep holds admitted
// shapes and refused ones, so it is not vacuous.
void sweep_agrees(::fixy::TestRunnerCtx const& test) noexcept {
    std::uint64_t admitted = 0;
    std::uint64_t refused = 0;
    for (std::uint64_t trial = 0; trial < kSweepTrials; ++trial) {
        UncheckedTensorMeta const fields = sweep_fields(trial);
        CRUCIBLE_TEST_ASSERT(test, agrees_with_claims(fields));
        if (holds_shape_claims(fields)) {
            ++admitted;
        } else {
            ++refused;
        }
    }
    CRUCIBLE_TEST_ASSERT(test, admitted >= 256);
    CRUCIBLE_TEST_ASSERT(test, refused >= 256);
    pass(test, "  sweep_agrees: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    each_lane_agrees(test);
    each_rank_past_the_bound_is_refused(test);
    sweep_agrees(test);
    pass(test, "test_tensor_shape_decode: all tests passed\n");
    return 0;
}
