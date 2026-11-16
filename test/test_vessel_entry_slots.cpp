// The scalar slots of a trace entry and the overflow digest of the vessel.
//
// The content hash of a region folds the scalar slots, their tags and the
// input metadata of each entry.  An operation with more scalars than the
// inline slots, or more input tensors than the metadata limit, must still
// give two different entries when only a value or a tag past a limit
// differs.  An operation that fits the limits must give the entry that a path
// with no digest gives.  The integer 1 and the double 4.9e-324 have one bit
// pattern, and their tags tell them apart.  The content hash and the digest
// read the device of a tensor through device_of, so the two names of the host
// give one digest.

#include "../vessel/torch/entry_slots.h"

#include <crucible/MerkleDag.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <fixy/Core.h>

#include <cstdint>

#include "test_assert.h"

namespace {

using crucible::ScalarType2;
using crucible::TraceRing;
using crucible::vessel::InputOverflow;
using crucible::vessel::kInlineScalars;
using crucible::vessel::ScalarArgs;
using crucible::vessel::write_scalar_slots;

// The entry of an operation whose scalars are the integers 1, 2, ... count,
// with the last scalar replaced by last of the tag last_tag.  O(count).
[[nodiscard]] TraceRing::Entry entry_of(uint32_t count, int64_t last, InputOverflow const& inputs,
                                        ScalarType2 last_tag = ScalarType2::INT) {
    ScalarArgs<true> scalars{};
    for (uint32_t index = 1; index < count; index++)
        scalars.push(static_cast<int64_t>(index), ScalarType2::INT);
    if (count != 0) scalars.push(last, last_tag);
    TraceRing::Entry entry{};
    write_scalar_slots(entry, scalars, inputs);
    return entry;
}

[[nodiscard]] bool same_slots(TraceRing::Entry const& left, TraceRing::Entry const& right) {
    if (left.num_scalar_args != right.num_scalar_args) return false;
    if (left.packed_scalar_types() != right.packed_scalar_types()) return false;
    return left.scalar_values == right.scalar_values;
}

// The input tensor of rank one with the given extent.
[[nodiscard]] crucible::TensorMeta vector_meta(int64_t extent) {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(extent);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    return meta;
}

void test_fitting_operation_keeps_its_scalars() {
    ScalarArgs<false> bounded{};
    for (int64_t value = 1; value <= kInlineScalars + 1; value++)
        bounded.push(value, ScalarType2::INT);
    TraceRing::Entry plain{};
    write_scalar_slots(plain, bounded);
    assert(plain.num_scalar_args == kInlineScalars);

    TraceRing::Entry const digested = entry_of(kInlineScalars, kInlineScalars, InputOverflow{});
    assert(same_slots(plain, digested));
    assert(digested.num_scalar_args == kInlineScalars);
    assert(digested.scalar_values.at(::fixy::index<kInlineScalars - 1>()) == kInlineScalars);
}

void test_scalar_past_the_slots_reaches_the_digest() {
    constexpr uint32_t kStepSizes = 136;
    TraceRing::Entry const first = entry_of(kStepSizes, 7, InputOverflow{});
    TraceRing::Entry const second = entry_of(kStepSizes, 8, InputOverflow{});
    TraceRing::Entry const again = entry_of(kStepSizes, 7, InputOverflow{});
    assert(first.num_scalar_args == kInlineScalars);
    assert(!same_slots(first, second));
    assert(same_slots(first, again));
    for (uint16_t slot = 0; slot + 1 < kInlineScalars; slot++)
        assert(crucible::test::element_at(first.scalar_values, slot) == static_cast<int64_t>(slot) + 1);
}

void test_scalar_count_reaches_the_digest() {
    TraceRing::Entry const shorter = entry_of(6, 6, InputOverflow{});
    TraceRing::Entry const longer = entry_of(7, 6, InputOverflow{});
    assert(!same_slots(shorter, longer));
}

// The integer 1 and the smallest positive double have one bit pattern.  The
// slot keeps the bits, and the tag tells the two apart, inline and past the
// slots.  The tag of slot 4 goes into the two high bits of op_flags, and the
// other bits of op_flags stay.
void test_tag_tells_an_integer_from_a_double() {
    constexpr int64_t kOneBits = 1;
    constexpr uint8_t kOtherFlags = crucible::op_flag::GRAD_ENABLED | crucible::op_flag::IS_MUTABLE;
    TraceRing::Entry integer{};
    integer.op_flags = kOtherFlags;
    TraceRing::Entry floating{};
    floating.op_flags = kOtherFlags;
    ScalarArgs<false> integer_scalars{};
    ScalarArgs<false> floating_scalars{};
    for (uint16_t slot = 0; slot < kInlineScalars; slot++) {
        integer_scalars.push(kOneBits, ScalarType2::INT);
        floating_scalars.push(kOneBits, slot + 1 == kInlineScalars ? ScalarType2::FLOAT : ScalarType2::INT);
    }
    write_scalar_slots(integer, integer_scalars);
    write_scalar_slots(floating, floating_scalars);
    assert(integer.scalar_values == floating.scalar_values);
    assert(!same_slots(integer, floating) && "the tag of slot 4 tells the two entries apart");
    assert(floating.get_scalar_type(kInlineScalars - 1) == ScalarType2::FLOAT);
    assert(integer.get_scalar_type(kInlineScalars - 1) == ScalarType2::INT);
    assert((floating.op_flags & static_cast<uint8_t>(~crucible::op_flag::SCALAR4_TYPE_MASK)) == kOtherFlags);

    constexpr uint32_t kStepSizes = 40;
    TraceRing::Entry const past_integer = entry_of(kStepSizes, kOneBits, InputOverflow{}, ScalarType2::INT);
    TraceRing::Entry const past_floating = entry_of(kStepSizes, kOneBits, InputOverflow{}, ScalarType2::FLOAT);
    assert(!same_slots(past_integer, past_floating) && "the digest folds the tag of each scalar");
    assert(past_integer.get_scalar_type(kInlineScalars - 1) == ScalarType2::INT && "the digest slot is an integer");
}

// An argument that boxes into None takes one slot with the value zero and
// the tag NONE, so an absent first optional and an absent second optional
// give two entries.
void test_absent_optional_takes_a_slot() {
    ScalarArgs<false> min_only{};
    min_only.push(::fixy::bit_cast<int64_t>(1.0), ScalarType2::FLOAT);
    min_only.push_absent();
    ScalarArgs<false> max_only{};
    max_only.push_absent();
    max_only.push(::fixy::bit_cast<int64_t>(1.0), ScalarType2::FLOAT);
    TraceRing::Entry first{};
    TraceRing::Entry second{};
    write_scalar_slots(first, min_only);
    write_scalar_slots(second, max_only);
    assert(first.num_scalar_args == 2 && second.num_scalar_args == 2);
    assert(second.scalar_values.at(::fixy::index<0>()) == 0);
    assert(second.get_scalar_type(0) == ScalarType2::NONE);
    assert(!same_slots(first, second));
}

void test_input_past_the_limit_reaches_the_digest() {
    InputOverflow narrow{};
    narrow.fold(vector_meta(3));
    InputOverflow wide{};
    wide.fold(vector_meta(5));

    TraceRing::Entry const first = entry_of(2, 2, narrow);
    TraceRing::Entry const second = entry_of(2, 2, wide);
    assert(first.num_scalar_args == 3);
    assert(first.scalar_values.at(::fixy::index<0>()) == 1 && first.scalar_values.at(::fixy::index<1>()) == 2);
    assert(!same_slots(first, second));

    TraceRing::Entry const no_scalars = entry_of(0, 0, narrow);
    assert(no_scalars.num_scalar_args == 1);
}

// The input tensor of rank one with extent 4 on the device of the type and
// the index.
[[nodiscard]] crucible::TensorMeta meta_on(crucible::DeviceType type, int8_t index) {
    crucible::TensorMeta meta = vector_meta(4);
    meta.device_type = type;
    meta.device_idx = index;
    return meta;
}

// The content hash of a region of one op whose one input is meta.
[[nodiscard]] crucible::ContentHash content_hash_of(crucible::TensorMeta const& meta) {
    ::fixy::FixedArray<crucible::TensorMeta, 1> metas{meta};
    ::fixy::FixedArray<crucible::OpIndex, 1> producers{crucible::OpIndex{}};
    ::fixy::FixedArray<crucible::SlotId, 1> slot_ids{crucible::SlotId{}};
    ::fixy::FixedArray<crucible::TraceEntry, 1> ops{};
    crucible::TraceEntry& op = ops.at(::fixy::index<0>());
    op.schema_hash = crucible::SchemaHash{42};
    op.set_inputs(metas.view(), producers.view(), slot_ids.view());
    return crucible::compute_content_hash(ops.view());
}

// The content hash of a region of one op with the one scalar of the bits 1
// and the tags of the scalars.
[[nodiscard]] crucible::ContentHash content_hash_of_scalar(uint16_t scalar_types) {
    ::fixy::FixedArray<int64_t, 1> scalars{int64_t{1}};
    ::fixy::FixedArray<crucible::TraceEntry, 1> ops{};
    crucible::TraceEntry& op = ops.at(::fixy::index<0>());
    op.schema_hash = crucible::SchemaHash{42};
    op.set_scalar_args(scalars.view());
    op.scalar_types = scalar_types;
    return crucible::compute_content_hash(ops.view());
}

// alpha=1 and alpha=4.9e-324 have one bit pattern, and their regions hash
// apart.  A tag bit of a slot past the count moves no hash.
void test_content_hash_folds_the_tags() {
    const auto integer = static_cast<uint16_t>(ScalarType2::INT);
    const auto floating = static_cast<uint16_t>(ScalarType2::FLOAT);
    CRUCIBLE_FATAL_INVARIANT(!(content_hash_of_scalar(integer) == content_hash_of_scalar(floating)));
    const auto past_the_count = static_cast<uint16_t>(static_cast<uint16_t>(ScalarType2::NONE) << 2);
    CRUCIBLE_FATAL_INVARIANT(content_hash_of_scalar(integer) == content_hash_of_scalar(integer | past_the_count));
}

// The overflow digest of the vessel after the fold of meta.
[[nodiscard]] uint64_t overflow_digest_of(crucible::TensorMeta const& meta) {
    InputOverflow overflow{};
    overflow.fold(meta);
    return overflow.digest;
}

// The fields of a C record of a tensor of rank one on the device.
[[nodiscard]] crucible::UncheckedTensorMeta fields_on(crucible::DeviceType type, int8_t index) {
    crucible::UncheckedTensorMeta fields{};
    fields.ndim = 1;
    fields.sizes.at(::fixy::index<0>()) = 4;
    fields.strides.at(::fixy::index<0>()) = 1;
    fields.dtype = static_cast<int8_t>(crucible::ScalarType::Float);
    fields.device_type = static_cast<int8_t>(type);
    fields.device_idx = index;
    return fields;
}

// The host has one device, and PyTorch names it with the index -1 or 0.  The
// equality of a device, the content hash of a region and the overflow digest
// of the vessel give one answer for the two names.  A device of a different
// type keeps its index, so two such devices stay two devices.
void test_host_index_is_one_device() {
    crucible::TensorMeta const host = meta_on(crucible::DeviceType::CPU, -1);
    crucible::TensorMeta const host_zero = meta_on(crucible::DeviceType::CPU, 0);
    crucible::TensorMeta const cuda_zero = meta_on(crucible::DeviceType::CUDA, 0);
    crucible::TensorMeta const cuda_one = meta_on(crucible::DeviceType::CUDA, 1);

    CRUCIBLE_FATAL_INVARIANT(crucible::device_of(host) == crucible::device_of(host_zero));
    CRUCIBLE_FATAL_INVARIANT(crucible::device_of(host_zero) == crucible::kHostDevice);
    CRUCIBLE_FATAL_INVARIANT(!(crucible::device_of(cuda_zero) == crucible::device_of(cuda_one)));
    CRUCIBLE_FATAL_INVARIANT(crucible::device_of(cuda_zero).index() == 0);

    // Two data addresses of one run on the two names of the host are equal.
    ::fixy::FixedArray<uint8_t, 8> bytes{};
    ::fixy::RunAddress const address = ::fixy::mint_run_address(bytes.view(), 0).expect("the run holds a byte");
    crucible::TensorData const on_host(address, crucible::device_of(host));
    crucible::TensorData const on_host_zero(address, crucible::device_of(host_zero));
    crucible::TensorData const on_cuda_zero(address, crucible::device_of(cuda_zero));
    crucible::TensorData const on_cuda_one(address, crucible::device_of(cuda_one));
    CRUCIBLE_FATAL_INVARIANT(on_host == on_host_zero);
    CRUCIBLE_FATAL_INVARIANT(!(on_cuda_zero == on_cuda_one));

    CRUCIBLE_FATAL_INVARIANT(content_hash_of(host) == content_hash_of(host_zero));
    CRUCIBLE_FATAL_INVARIANT(!(content_hash_of(host) == content_hash_of(cuda_zero)));
    CRUCIBLE_FATAL_INVARIANT(!(content_hash_of(cuda_zero) == content_hash_of(cuda_one)));

    CRUCIBLE_FATAL_INVARIANT(overflow_digest_of(host) == overflow_digest_of(host_zero));
    CRUCIBLE_FATAL_INVARIANT(overflow_digest_of(host) != overflow_digest_of(cuda_zero));
    CRUCIBLE_FATAL_INVARIANT(overflow_digest_of(cuda_zero) != overflow_digest_of(cuda_one));

    // The decode door of a C record stores -1 for each index of the host, and
    // it keeps the index of a different device.
    crucible::TensorMeta const decoded_host =
        crucible::decode_tensor_meta(fields_on(crucible::DeviceType::CPU, 0), crucible::ExternalDataPtr{})
            .expect("the fields of a host tensor decode");
    crucible::TensorMeta const decoded_cuda =
        crucible::decode_tensor_meta(fields_on(crucible::DeviceType::CUDA, 1), crucible::ExternalDataPtr{})
            .expect("the fields of a CUDA tensor decode");
    CRUCIBLE_FATAL_INVARIANT(decoded_host.device_idx == -1);
    CRUCIBLE_FATAL_INVARIANT(decoded_cuda.device_idx == 1);
}

// A device that a caller builds from its two parts has one index for the
// host, the index that device_of gives.  A device of a different type keeps
// its index.  A copy into a device of a different type keeps the one index.
void test_built_device_has_one_host_index() {
    crucible::TensorDevice const host_zero{crucible::DeviceType::CPU, 0};
    crucible::TensorDevice const cuda_zero{crucible::DeviceType::CUDA, 0};
    CRUCIBLE_FATAL_INVARIANT(host_zero == crucible::kHostDevice);
    CRUCIBLE_FATAL_INVARIANT(host_zero == crucible::device_of(meta_on(crucible::DeviceType::CPU, 0)));
    CRUCIBLE_FATAL_INVARIANT(cuda_zero == crucible::device_of(meta_on(crucible::DeviceType::CUDA, 0)));
    CRUCIBLE_FATAL_INVARIANT(!(cuda_zero == crucible::kHostDevice));
    crucible::TensorDevice held = cuda_zero;
    held = host_zero;
    CRUCIBLE_FATAL_INVARIANT(held == crucible::kHostDevice);
}

}  // namespace

int main() {
    test_fitting_operation_keeps_its_scalars();
    test_scalar_past_the_slots_reaches_the_digest();
    test_scalar_count_reaches_the_digest();
    test_tag_tells_an_integer_from_a_double();
    test_absent_optional_takes_a_slot();
    test_input_past_the_limit_reaches_the_digest();
    test_host_index_is_one_device();
    test_content_hash_folds_the_tags();
    test_built_device_has_one_host_index();
    crucible::test::pass("test_vessel_entry_slots: PASS\n");
    return 0;
}
