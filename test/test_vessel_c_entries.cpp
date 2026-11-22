// The C entries of libcrucible_vessel.so judge each pointer and count pair of
// a caller once, at the boundary, and give their refusal for a pair that
// breaks the rule of the arrays of vessel_api.h: the action REFUSED, or 0 with
// no write for an entry that writes into an array of the caller.  No pair
// ends the process at a contract check, and no pair records an op.  The test
// calls the entries through the C header, as a ctypes caller does.
#include "../vessel/torch/vessel_api.h"

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

int g_failures = 0;

#define EXPECT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ::fixy::report(::fixy::Sink::Err, "FAIL: {} -- {} ({}:{})\n", ::fixy::TextView{#cond}, \
                           ::fixy::TextView{msg}, ::fixy::TextView{__FILE__}, __LINE__);           \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

// The schema hashes of the two ops of the loop.  The C entries take each hash
// other than zero.
constexpr uint64_t kScaleSchema = 0x5CA1E0001ULL;
constexpr uint64_t kAddSchema = 0xADD00002ULL;

// The crucible ordinal of float32, and the tag of a double scalar.
constexpr int8_t kFloat = 6;
constexpr uint16_t kDoubleTag = 1;

// The most iterations of the loop that forms a region.  The Vigil activates a
// region after two periods of the loop.
constexpr int kMaxIterations = 60;

// The two records of one op: an input and an output, each a contiguous float
// tensor of four elements.
using OpRecords = ::fixy::FixedArray<CrucibleMeta, 2>;

// The records of the two ops of the loop.
struct LoopRecords {
    OpRecords scale{};
    OpRecords add{};
};

// The state of the Vigil at a check of the out arrays.
enum class RegionPhase : uint8_t {
    NoRegion,
    Active,
};

// The bytes of an array of the caller, so that a case can give a pointer one
// byte past an aligned element.
template <class Element, std::size_t Count>
struct alignas(Element) CallerBytes {
    ::fixy::FixedArray<std::byte, sizeof(Element) * Count + 1> bytes{};
};

// The record of a contiguous float tensor of four elements at a synthetic
// address.  No code reads through the address.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the case writes a CrucibleMeta of vessel_api.h with a synthetic address")
[[nodiscard]] CrucibleMeta record_at(crucible::SyntheticAddress address) noexcept {
    CrucibleMeta record{};
    record.sizes[0] = 4;
    record.strides[0] = 1;
    record.data_ptr = std::bit_cast<void*>(std::to_underlying(address));
    record.ndim = 1;
    record.dtype = kFloat;
    record.device_idx = -1;
    record.flags = 0x02;
    record.storage_nbytes = 16;
    return record;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The C entries take and give the raw pointers of the C interface, so each
// call of an entry is in an ABI region.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the case calls the C entries of the vessel library with raw pointers")

// The place of the array of a caller in its bytes: the first byte, which has
// the alignment of the element, or one byte past it, which does not.
enum class Place : uint8_t {
    Aligned,
    OneBytePast,
};

// Calls entry with the array at place in bytes, and gives the result of
// entry.  The pointer stays in the call.  An entry that writes an element
// starts its life in the bytes, and no entry reads through a pointer one
// byte past the first byte.
template <class Element, std::size_t Count, class Entry>
[[nodiscard]] auto call_with_array(CallerBytes<Element, Count>& bytes, Place place, Entry&& entry) noexcept {
    return bytes.bytes.with_mut([place, &entry](auto run) noexcept {
        std::byte* const first = ::foundation::core::foreign_first(run.view());
        return entry(std::bit_cast<Element*>(place == Place::Aligned ? first : first + 1));
    });
}

// True when each byte of bytes is zero, so no entry wrote into them.
template <class Element, std::size_t Count>
[[nodiscard]] bool is_untouched(CallerBytes<Element, Count> const& bytes) noexcept {
    for (const std::byte byte : bytes.bytes.view()) {
        if (byte != std::byte{0}) return false;
    }
    return true;
}

// One iteration of the loop: a scale with one data scalar, then an add.
void run_iteration(CrucibleHandle handle, LoopRecords const& loop) noexcept {
    const ::fixy::FixedArray<int64_t, 5> step{std::bit_cast<int64_t>(0.5), 0, 0, 0, 0};
    static_cast<void>(crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, &loop.scale.at(::fixy::index<0>()), 2,
                                              &step.at(::fixy::index<0>()), 1, kDoubleTag, 1, 0, 0));
    static_cast<void>(crucible_dispatch_op(handle, kAddSchema, 1, 1, &loop.add.at(::fixy::index<0>()), 2));
}

// The pairs of the two dispatch entries.  The handle has no active region.
void test_dispatch_pairs(CrucibleHandle handle) {
    const OpRecords records{record_at(crucible::SyntheticAddress{0x10000}),
                            record_at(crucible::SyntheticAddress{0x20000})};
    const CrucibleMeta* const metas = &records.at(::fixy::index<0>());
    const ::fixy::FixedArray<int64_t, 5> values{1, 2, 3, 4, 5};
    const int64_t* const scalars = &values.at(::fixy::index<0>());
    CallerBytes<CrucibleMeta, 1> meta_bytes{};
    CallerBytes<int64_t, 1> scalar_bytes{};
    const auto misaligned_meta_action = [handle](CrucibleMeta* record) noexcept {
        return crucible_dispatch_op(handle, kAddSchema, 1, 0, record, 1).action;
    };
    const auto misaligned_meta_ex_action = [handle, scalars](CrucibleMeta* record) noexcept {
        return crucible_dispatch_op_ex(handle, kScaleSchema, 1, 0, record, 1, scalars, 1, 0, 0, 0, 0).action;
    };
    const auto misaligned_scalar_action = [handle, metas](int64_t* values_at) noexcept {
        return crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, metas, 2, values_at, 1, 0, 0, 0, 0).action;
    };

    // The cases that the old entries recorded or judged with a contract
    // check come first, in the order of their defect.
    EXPECT(crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, metas, 2, nullptr, 5, 0, 0, 0, 0).action
               == CRUCIBLE_ACTION_REFUSED,
           "a null scalar array with a count above 0 is refused, not recorded with zero values");
    EXPECT(call_with_array(scalar_bytes, Place::OneBytePast, misaligned_scalar_action) == CRUCIBLE_ACTION_REFUSED,
           "a misaligned scalar array with a count above 0 is refused");
    EXPECT(crucible_dispatch_op(handle, kAddSchema, 0, 0, metas, 0).action == CRUCIBLE_ACTION_RECORD,
           "a meta array with a count of 0 reads no record, and the op with no tensor records");
    EXPECT(call_with_array(meta_bytes, Place::OneBytePast, misaligned_meta_action) == CRUCIBLE_ACTION_REFUSED,
           "a misaligned meta array with a count above 0 is refused");
    EXPECT(crucible_dispatch_op(handle, kAddSchema, 1, 1, nullptr, 2).action == CRUCIBLE_ACTION_REFUSED,
           "a null meta array with a count above 0 is refused");
    EXPECT(crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, metas, 2, scalars, 0, 0, 0, 0, 0).action
               == CRUCIBLE_ACTION_RECORD,
           "a scalar buffer with a count of 0 reads no value, as crucible_mode.py gives it");
    EXPECT(crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, metas, 2, nullptr, 0, 0, 0, 0, 0).action
               == CRUCIBLE_ACTION_RECORD,
           "a null scalar array with a count of 0 records");
    EXPECT(crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, nullptr, 2, scalars, 1, 0, 0, 0, 0).action
               == CRUCIBLE_ACTION_REFUSED,
           "a null meta array with a count above 0 is refused by the extended entry");
    EXPECT(crucible_dispatch_op_ex(handle, kScaleSchema, 0, 0, metas, 0, scalars, 1, 0, 0, 0, 0).action
               == CRUCIBLE_ACTION_RECORD,
           "a meta array with a count of 0 reads no record in the extended entry");
    EXPECT(call_with_array(meta_bytes, Place::OneBytePast, misaligned_meta_ex_action) == CRUCIBLE_ACTION_REFUSED,
           "a misaligned meta array with a count above 0 is refused by the extended entry");
}

// The pairs of the two entries that write into an array of the caller, with
// and with no active region.
void test_out_pairs(CrucibleHandle handle, RegionPhase phase) {
    CallerBytes<CrucibleSlot, 8> slots{};
    CallerBytes<CrucibleBoundScalar, 4> bound{};
    CallerBytes<CrucibleSlot, 4> slot_bytes{};
    CallerBytes<CrucibleBoundScalar, 4> bound_bytes{};

    const auto slots_with = [handle](uint32_t capacity) noexcept {
        return [handle, capacity](CrucibleSlot* out) noexcept {
            return crucible_active_region_slots(handle, out, capacity);
        };
    };
    const auto bound_with = [handle](uint32_t capacity) noexcept {
        return [handle, capacity](CrucibleBoundScalar* out) noexcept {
            return crucible_bound_scalars(handle, out, capacity);
        };
    };

    const uint32_t slot_count = crucible_active_region_slots(handle, nullptr, 0);
    const uint32_t bound_count = crucible_bound_scalars(handle, nullptr, 0);
    EXPECT(crucible_active_region_slots(handle, nullptr, 4) == 0, "a null out array with a capacity above 0 gives 0");
    EXPECT(call_with_array(slot_bytes, Place::OneBytePast, slots_with(4)) == 0,
           "a misaligned out array with a capacity above 0 gives 0");
    EXPECT(is_untouched(slot_bytes), "the entry writes nothing into a misaligned out array");
    EXPECT(call_with_array(slots, Place::Aligned, slots_with(0)) == slot_count,
           "an out array with a capacity of 0 is a read of the count");
    EXPECT(call_with_array(slots, Place::Aligned, slots_with(8)) == slot_count,
           "an aligned out array with a capacity above 0 gives the count");

    EXPECT(crucible_bound_scalars(handle, nullptr, 4) == 0, "a null out array of bound scalars gives 0");
    EXPECT(call_with_array(bound_bytes, Place::OneBytePast, bound_with(4)) == 0,
           "a misaligned out array of bound scalars gives 0");
    EXPECT(is_untouched(bound_bytes), "the entry writes nothing into a misaligned out array of bound scalars");
    EXPECT(call_with_array(bound, Place::Aligned, bound_with(4)) == bound_count,
           "an aligned out array of bound scalars gives the count");

    // With an active region the plan has slots and the scale has a data slot,
    // so a refusal and a count of zero differ.
    if (phase == RegionPhase::Active) {
        EXPECT(slot_count > 0, "the plan of the active region has slots");
        EXPECT(bound_count > 0, "the active region has a data slot");
    }
}

// The pairs of a handle with no region, from create to destroy.
void test_pairs_with_no_region() {
    const CrucibleHandle handle = crucible_create(0);
    if (handle == nullptr) {
        EXPECT(handle != nullptr, "crucible_create gives a handle for the eager word 0");
        return;
    }
    test_dispatch_pairs(handle);
    test_out_pairs(handle, RegionPhase::NoRegion);
    crucible_destroy(handle);
}

// The pairs of the out arrays of a handle that replays a region.  A loop of
// two ops over three tensors runs until the Vigil replays its region.  The
// records of the loop are the same in each iteration.
void test_pairs_with_a_region() {
    const CrucibleHandle handle = crucible_create(0);
    if (handle == nullptr) {
        EXPECT(handle != nullptr, "crucible_create gives a handle for the eager word 0");
        return;
    }
    const LoopRecords loop{
        .scale = {record_at(crucible::SyntheticAddress{0x10000}), record_at(crucible::SyntheticAddress{0x20000})},
        .add = {record_at(crucible::SyntheticAddress{0x20000}), record_at(crucible::SyntheticAddress{0x30000})}};
    bool is_compiled = false;
    for (int iteration = 0; iteration < kMaxIterations && !is_compiled; ++iteration) {
        run_iteration(handle, loop);
        is_compiled = crucible_is_compiled(handle) == 1;
        if (!is_compiled) static_cast<void>(crucible_flush(handle));
    }
    EXPECT(is_compiled, "the loop of the C entries replays a region");
    if (is_compiled) test_out_pairs(handle, RegionPhase::Active);
    crucible_destroy(handle);
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace

int main() {
    test_pairs_with_no_region();
    test_pairs_with_a_region();
    if (g_failures != 0) {
        ::fixy::report(::fixy::Sink::Err, "test_vessel_c_entries: FAIL ({})\n", g_failures);
        return EXIT_FAILURE;
    }
    ::fixy::check::pass(kTest, "test_vessel_c_entries: PASS\n");
    return EXIT_SUCCESS;
}
