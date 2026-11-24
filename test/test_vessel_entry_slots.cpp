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
//
// The guard word of an entry (compute_shape_hash) must change with each field
// that changes the computation of the op: a stride, an element type, a device,
// a rank, a layout, the negative and conjugate bits, a storage offset, a
// structural scalar, a tag, a role, a count and the depth.  It must not change
// with a data address, a version counter, an autograd field or the value of a
// data scalar, which change at each iteration.  The content hash of a region
// changes with the rank, the layout, the two bits and the storage offset of a
// tensor too, and not with the value of a data scalar.

#include "../vessel/torch/entry_slots.h"

#include <crucible/MerkleDag.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <fixy/Core.h>

#include <cstdint>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using crucible::ScalarRole;
using crucible::ScalarType2;
using crucible::TraceRing;
using crucible::vessel::compute_shape_hash;
using crucible::vessel::InputOverflow;
using crucible::vessel::kInlineScalars;
using crucible::vessel::ScalarArgs;
using crucible::vessel::write_scalar_slots;

constexpr ScalarRole kStructural = ScalarRole::STRUCTURAL;
constexpr ScalarRole kData = ScalarRole::DATA;

// The role of the inline slot Slot of an entry.
template <std::size_t Slot>
[[nodiscard]] ScalarRole role_of(TraceRing::Entry const& entry) {
    return entry.data_slots()
        .expect("a written entry holds only inline data slots")
        .role(crucible::ScalarSlot{::fixy::index<Slot>()});
}

// The entry of an operation whose scalars are the integers 1, 2, ... count,
// with the last scalar replaced by last of the tag last_tag and the role
// last_role.  O(count).
[[nodiscard]] TraceRing::Entry entry_of(uint32_t count, int64_t last, InputOverflow const& inputs,
                                        ScalarType2 last_tag = ScalarType2::INT, ScalarRole last_role = kStructural) {
    ScalarArgs<true> scalars{};
    for (uint32_t index = 1; index < count; index++)
        scalars.push(static_cast<int64_t>(index), ScalarType2::INT, kStructural);
    if (count != 0) scalars.push(last, last_tag, last_role);
    TraceRing::Entry entry{};
    write_scalar_slots(entry, scalars, inputs);
    return entry;
}

[[nodiscard]] bool same_slots(TraceRing::Entry const& left, TraceRing::Entry const& right) {
    if (left.num_scalar_args != right.num_scalar_args) return false;
    if (left.packed_scalar_types() != right.packed_scalar_types()) return false;
    if (!(left.data_slots() == right.data_slots())) return false;
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
        bounded.push(value, ScalarType2::INT, kStructural);
    TraceRing::Entry plain{};
    write_scalar_slots(plain, bounded);
    CRUCIBLE_TEST_ASSERT(kTest, plain.num_scalar_args == kInlineScalars);

    TraceRing::Entry const digested = entry_of(kInlineScalars, kInlineScalars, InputOverflow{});
    CRUCIBLE_TEST_ASSERT(kTest, same_slots(plain, digested));
    CRUCIBLE_TEST_ASSERT(kTest, digested.num_scalar_args == kInlineScalars);
    CRUCIBLE_TEST_ASSERT(kTest, digested.scalar_values.at(::fixy::index<kInlineScalars - 1>()) == kInlineScalars);
}

void test_scalar_past_the_slots_reaches_the_digest() {
    constexpr uint32_t kStepSizes = 136;
    TraceRing::Entry const first = entry_of(kStepSizes, 7, InputOverflow{});
    TraceRing::Entry const second = entry_of(kStepSizes, 8, InputOverflow{});
    TraceRing::Entry const again = entry_of(kStepSizes, 7, InputOverflow{});
    CRUCIBLE_TEST_ASSERT(kTest, first.num_scalar_args == kInlineScalars);
    CRUCIBLE_TEST_ASSERT(kTest, !same_slots(first, second));
    CRUCIBLE_TEST_ASSERT(kTest, same_slots(first, again));
    for (uint16_t slot = 0; slot + 1 < kInlineScalars; slot++)
        CRUCIBLE_TEST_ASSERT(
            kTest, *first.scalar_values.get(slot).expect("a test reads an element at or past the count of its array")
                       == static_cast<int64_t>(slot) + 1);
}

void test_scalar_count_reaches_the_digest() {
    TraceRing::Entry const shorter = entry_of(6, 6, InputOverflow{});
    TraceRing::Entry const longer = entry_of(7, 6, InputOverflow{});
    CRUCIBLE_TEST_ASSERT(kTest, !same_slots(shorter, longer));
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
        integer_scalars.push(kOneBits, ScalarType2::INT, kStructural);
        floating_scalars.push(kOneBits, slot + 1 == kInlineScalars ? ScalarType2::FLOAT : ScalarType2::INT,
                              kStructural);
    }
    write_scalar_slots(integer, integer_scalars);
    write_scalar_slots(floating, floating_scalars);
    CRUCIBLE_TEST_ASSERT(kTest, integer.scalar_values == floating.scalar_values);
    CRUCIBLE_TEST_ASSERT(kTest, !same_slots(integer, floating) && "the tag of slot 4 tells the two entries apart");
    CRUCIBLE_TEST_ASSERT(kTest, floating.get_scalar_type(kInlineScalars - 1) == ScalarType2::FLOAT);
    CRUCIBLE_TEST_ASSERT(kTest, integer.get_scalar_type(kInlineScalars - 1) == ScalarType2::INT);
    CRUCIBLE_TEST_ASSERT(kTest, (floating.op_flags & static_cast<uint8_t>(~crucible::op_flag::SCALAR4_TYPE_MASK))
                                    == kOtherFlags);

    constexpr uint32_t kStepSizes = 40;
    TraceRing::Entry const past_integer = entry_of(kStepSizes, kOneBits, InputOverflow{}, ScalarType2::INT);
    TraceRing::Entry const past_floating = entry_of(kStepSizes, kOneBits, InputOverflow{}, ScalarType2::FLOAT);
    CRUCIBLE_TEST_ASSERT(kTest, !same_slots(past_integer, past_floating) && "the digest folds the tag of each scalar");
    CRUCIBLE_TEST_ASSERT(kTest, past_integer.get_scalar_type(kInlineScalars - 1) == ScalarType2::INT
                                    && "the digest slot is an integer");
}

// An argument that boxes into None takes one slot with the value zero and
// the tag NONE, so an absent first optional and an absent second optional
// give two entries.
void test_absent_optional_takes_a_slot() {
    ScalarArgs<false> min_only{};
    min_only.push(::fixy::bit_cast<int64_t>(1.0), ScalarType2::FLOAT, kData);
    min_only.push_absent();
    ScalarArgs<false> max_only{};
    max_only.push_absent();
    max_only.push(::fixy::bit_cast<int64_t>(1.0), ScalarType2::FLOAT, kData);
    TraceRing::Entry first{};
    TraceRing::Entry second{};
    write_scalar_slots(first, min_only);
    write_scalar_slots(second, max_only);
    CRUCIBLE_TEST_ASSERT(kTest, first.num_scalar_args == 2 && second.num_scalar_args == 2);
    CRUCIBLE_TEST_ASSERT(kTest, second.scalar_values.at(::fixy::index<0>()) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, second.get_scalar_type(0) == ScalarType2::NONE);
    CRUCIBLE_TEST_ASSERT(kTest, role_of<0>(second) == kStructural && "an absent optional is structural");
    CRUCIBLE_TEST_ASSERT(kTest, role_of<1>(second) == kData);
    CRUCIBLE_TEST_ASSERT(kTest, !same_slots(first, second));
}

void test_input_past_the_limit_reaches_the_digest() {
    InputOverflow narrow{};
    narrow.fold(vector_meta(3));
    InputOverflow wide{};
    wide.fold(vector_meta(5));

    TraceRing::Entry const first = entry_of(2, 2, narrow);
    TraceRing::Entry const second = entry_of(2, 2, wide);
    CRUCIBLE_TEST_ASSERT(kTest, first.num_scalar_args == 3);
    CRUCIBLE_TEST_ASSERT(kTest, first.scalar_values.at(::fixy::index<0>()) == 1
                                    && first.scalar_values.at(::fixy::index<1>()) == 2);
    CRUCIBLE_TEST_ASSERT(kTest, !same_slots(first, second));

    TraceRing::Entry const no_scalars = entry_of(0, 0, narrow);
    CRUCIBLE_TEST_ASSERT(kTest, no_scalars.num_scalar_args == 1);
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
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<crucible::DagArena> arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{4096});
    const auto metas = ::fixy::check::run_of(kTest, test.alloc, arena, meta);
    const auto producers = ::fixy::check::run_of(kTest, test.alloc, arena, crucible::OpIndex{});
    const auto slot_ids = ::fixy::check::run_of(kTest, test.alloc, arena, crucible::SlotId{});
    ::fixy::FixedArray<crucible::TraceEntry, 1> ops{};
    ops.with_element(::fixy::index<0>(),
                     [metas = ::fixy::ArenaRun<crucible::TensorMeta const, crucible::DagArena>{metas}, producers,
                      slot_ids](crucible::TraceEntry& op) noexcept {
                         op.schema_hash = crucible::SchemaHash{42};
                         op.set_inputs(metas, producers, slot_ids);
                     });
    return crucible::compute_content_hash(ops.view());
}

// The content hash of a region of one op with the one scalar of the bits and
// the word of the tags and the data slots of the scalars.
[[nodiscard]] crucible::ContentHash content_hash_of_scalar(uint16_t scalar_kinds, int64_t bits = 1) {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<crucible::DagArena> arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{4096});
    const auto scalars = ::fixy::check::run_of(kTest, test.alloc, arena, bits);
    const crucible::ScalarSlotKinds kinds =
        crucible::ScalarSlotKinds::from_bits(scalar_kinds).expect("the test gives a word of the kinds");
    ::fixy::FixedArray<crucible::TraceEntry, 1> ops{};
    ops.with_element(::fixy::index<0>(), [scalars, kinds](crucible::TraceEntry& op) noexcept {
        op.schema_hash = crucible::SchemaHash{42};
        op.set_scalar_args(scalars);
        op.scalar_kinds = kinds;
    });
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

// The word of the one slot of an op: the tag, and the bit of the data slot.
[[nodiscard]] uint16_t kinds_of_slot(ScalarType2 tag, ScalarRole role) {
    return crucible::ScalarSlotKinds::of(static_cast<uint16_t>(tag),
                                         crucible::DataSlots{}.with(crucible::ScalarSlot{::fixy::index<0>()}, role))
        .bits();
}

// The content hash of a region reads the tag and the role of a data slot but
// not its value, so a loop whose step size changes keeps one region.  The value
// of a structural slot moves the hash.  The old hash folded each value, so the
// first check gave two hashes.
void test_content_hash_reads_no_data_value() {
    const uint16_t data = kinds_of_slot(ScalarType2::FLOAT, kData);
    const uint16_t structural = kinds_of_slot(ScalarType2::FLOAT, kStructural);
    const int64_t first_lr = ::fixy::bit_cast<int64_t>(1e-3);
    const int64_t second_lr = ::fixy::bit_cast<int64_t>(2e-3);
    CRUCIBLE_FATAL_INVARIANT(content_hash_of_scalar(data, first_lr) == content_hash_of_scalar(data, second_lr)
                             && "the value of a data slot moves no content hash");
    CRUCIBLE_FATAL_INVARIANT(
        !(content_hash_of_scalar(structural, first_lr) == content_hash_of_scalar(structural, second_lr))
        && "the value of a structural slot moves the content hash");
    CRUCIBLE_FATAL_INVARIANT(!(content_hash_of_scalar(data, first_lr) == content_hash_of_scalar(structural, first_lr))
                             && "the role moves the content hash");
    CRUCIBLE_FATAL_INVARIANT(!(content_hash_of_scalar(data, first_lr)
                               == content_hash_of_scalar(kinds_of_slot(ScalarType2::INT, kData), first_lr))
                             && "the tag of a data slot moves the content hash");
    // A structural zero and a data slot fold one value, and the role tells
    // them apart.
    CRUCIBLE_FATAL_INVARIANT(!(content_hash_of_scalar(data, 0) == content_hash_of_scalar(structural, 0)));
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
    fields.sizes.with_element(::fixy::index<0>(), [](int64_t& element) noexcept { element = 4; });
    fields.strides.with_element(::fixy::index<0>(), [](int64_t& element) noexcept { element = 1; });
    fields.dtype = static_cast<int8_t>(crucible::ScalarType::Float);
    fields.device_type = static_cast<int8_t>(type);
    fields.device_idx = index;
    return fields;
}

// The host has one device, and PyTorch names it with the index -1 or 0.  The
// equality of a device, the content hash of a region and the overflow digest
// of the vessel give one answer for the two names.  A device of a different
// type keeps its index, so two such devices stay two devices.
template <class Arena>
void check_host_index_is_one_device(Arena& arena) {
    auto test = ::foundation::effects::testing::test();
    crucible::TensorMeta const host = meta_on(crucible::DeviceType::CPU, -1);
    crucible::TensorMeta const host_zero = meta_on(crucible::DeviceType::CPU, 0);
    crucible::TensorMeta const cuda_zero = meta_on(crucible::DeviceType::CUDA, 0);
    crucible::TensorMeta const cuda_one = meta_on(crucible::DeviceType::CUDA, 1);

    CRUCIBLE_FATAL_INVARIANT(crucible::device_of(host) == crucible::device_of(host_zero));
    CRUCIBLE_FATAL_INVARIANT(crucible::device_of(host_zero) == crucible::kHostDevice);
    CRUCIBLE_FATAL_INVARIANT(!(crucible::device_of(cuda_zero) == crucible::device_of(cuda_one)));
    CRUCIBLE_FATAL_INVARIANT(crucible::device_of(cuda_zero).index() == 0);

    // Two data addresses of one run on the two names of the host are equal.
    auto const bytes_run = arena.array(test.alloc, 8, uint8_t{0});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<uint8_t> const bytes = bytes_run.view();
    ::fixy::RunAddress const address = ::fixy::mint_run_address(bytes, 0).expect("the run holds a byte");
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

// The two names of the host, with the bytes of the data addresses in an
// arena of the scope of the test.
void test_host_index_is_one_device() {
    ::fixy::with_bump_arena(::foundation::effects::testing::test().alloc,
                            ::fixy::Quantity<::fixy::unit::Byte, size_t>{4096},
                            [](auto& arena) noexcept { check_host_index_is_one_device(arena); });
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

// A matrix of 3 by 4 with the strides of its rows.
[[nodiscard]] crucible::TensorMeta matrix_meta() {
    crucible::TensorMeta meta{};
    meta.ndim = 2;
    meta.sizes[0] = crucible::tensor_dim(3);
    meta.sizes[1] = crucible::tensor_dim(4);
    meta.strides[0] = crucible::tensor_dim(4);
    meta.strides[1] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    return meta;
}

// The scalars of an op whose one scalar has the bits, the tag and the role.
[[nodiscard]] ScalarArgs<false> one_scalar(int64_t bits, ScalarType2 tag, ScalarRole role = kStructural) {
    ScalarArgs<false> scalars{};
    scalars.push(bits, tag, role);
    return scalars;
}

// The guard word of an op with the inputs, the scalars and the depth.
[[nodiscard]] crucible::ShapeHash guard_word_of(::fixy::View<const crucible::TensorMeta> inputs,
                                                ScalarArgs<false> const& scalars, uint16_t depth = 0) {
    TraceRing::Entry entry{};
    entry.num_inputs = ::fixy::narrow<uint16_t>(inputs.size()).expect("a test op has few inputs");
    write_scalar_slots(entry, scalars);
    return compute_shape_hash(inputs, entry, crucible::OpDepth{depth});
}

// The guard word of an op with the two inputs, the scalars and the depth.
[[nodiscard]] crucible::ShapeHash guard_word_of(crucible::TensorMeta const& first, crucible::TensorMeta const& second,
                                                ScalarArgs<false> const& scalars, uint16_t depth = 0) {
    const ::fixy::FixedArray<crucible::TensorMeta, 2> inputs{first, second};
    return guard_word_of(inputs.view(), scalars, depth);
}

// The guard word of an op with no input and the scalar slots of entry.
[[nodiscard]] crucible::ShapeHash guard_word_of(TraceRing::Entry const& entry) {
    return compute_shape_hash(::fixy::View<const crucible::TensorMeta>{}, entry, crucible::OpDepth{});
}

// Each field that changes the computation of the op changes its guard word.
// The old word folded only the rank and the extents, so each check below of a
// stride, an element type, a device, a layout, a bit, a scalar, a tag or the
// depth gave one word for two different computations.
void test_guard_word_reads_each_field_of_the_computation() {
    const crucible::TensorMeta vector = vector_meta(4);
    const crucible::TensorMeta matrix = matrix_meta();
    const ScalarArgs<false> alpha_one = one_scalar(1, ScalarType2::INT);
    const crucible::ShapeHash base = guard_word_of(vector, matrix, alpha_one);
    CRUCIBLE_FATAL_INVARIANT(base == guard_word_of(vector, matrix, alpha_one));

    // The transposed matrix of 4 by 3 has the extents of no other input here,
    // so the strides are the only difference of this matrix of 3 by 4.
    crucible::TensorMeta columns = matrix;
    columns.strides[0] = crucible::tensor_dim(1);
    columns.strides[1] = crucible::tensor_dim(3);
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, columns, alpha_one) == base) && "a stride moves the word");

    crucible::TensorMeta doubles = matrix;
    doubles.dtype = crucible::ScalarType::Double;
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, doubles, alpha_one) == base) && "an element type moves the word");

    crucible::TensorMeta on_cuda = matrix;
    on_cuda.device_type = crucible::DeviceType::CUDA;
    on_cuda.device_idx = 0;
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, on_cuda, alpha_one) == base) && "a device moves the word");

    crucible::TensorMeta sparse = matrix;
    sparse.layout = crucible::Layout::Sparse;
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, sparse, alpha_one) == base) && "a layout moves the word");

    crucible::TensorMeta conjugate = matrix;
    conjugate.flags = crucible::meta_flags::IS_CONJ;
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, conjugate, alpha_one) == base) && "the conjugate bit moves it");

    crucible::TensorMeta negative = matrix;
    negative.flags = crucible::meta_flags::IS_NEG;
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, negative, alpha_one) == base) && "the negative bit moves it");

    // A lane of extent zero and stride zero adds nothing to the XOR fold of
    // the content hash, so only the rank tells [4, 0] from [4].
    crucible::TensorMeta empty_rows = vector;
    empty_rows.ndim = 2;
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(empty_rows, matrix, alpha_one) == base) && "the rank moves the word");

    // A view at another offset of one storage reads other elements.
    crucible::TensorMeta shifted = matrix;
    shifted.storage_offset = 4;
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, shifted, alpha_one) == base) && "the storage offset moves it");

    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(matrix, vector, alpha_one) == base) && "the order of the inputs moves it");
    const ::fixy::FixedArray<crucible::TensorMeta, 1> only_vector{vector};
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(only_vector.view(), alpha_one) == base) && "the input count moves it");

    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, matrix, one_scalar(2, ScalarType2::INT)) == base)
                             && "a structural scalar moves the word");
    // alpha=1 and alpha=4.9e-324 have one bit pattern, and the tag tells them
    // apart.
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, matrix, one_scalar(1, ScalarType2::FLOAT)) == base)
                             && "the tag moves the word");
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, matrix, alpha_one, 1) == base) && "the depth moves the word");

    // An absent optional is the value zero with the tag NONE, and the integer
    // zero has the tag INT.  An op with no scalar has one slot less.
    const crucible::ShapeHash absent = guard_word_of(vector, matrix, one_scalar(0, ScalarType2::NONE));
    const crucible::ShapeHash zero = guard_word_of(vector, matrix, one_scalar(0, ScalarType2::INT));
    const crucible::ShapeHash none = guard_word_of(vector, matrix, ScalarArgs<false>{});
    CRUCIBLE_FATAL_INVARIANT(!(absent == zero) && !(absent == none) && !(zero == none));
}

// A scalar or an input past the slots reaches the guard word through the
// overflow digest of the last slot.
void test_guard_word_reads_the_overflow_digest() {
    constexpr uint32_t kStepSizes = 136;
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(entry_of(kStepSizes, 7, InputOverflow{}))
                               == guard_word_of(entry_of(kStepSizes, 8, InputOverflow{}))));
    InputOverflow narrow{};
    narrow.fold(vector_meta(3));
    InputOverflow wide{};
    wide.fold(vector_meta(5));
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(entry_of(2, 2, narrow)) == guard_word_of(entry_of(2, 2, wide))));
}

// The fields that change at each iteration or in each process leave the
// guard word as it is, or no region could replay twice.  The two names of the
// host give one word, and a tag bit past the count moves no word.
void test_guard_word_ignores_what_changes_at_each_iteration() {
    const crucible::TensorMeta vector = vector_meta(4);
    const crucible::TensorMeta matrix = matrix_meta();
    const ScalarArgs<false> alpha_one = one_scalar(1, ScalarType2::INT);
    const crucible::ShapeHash base = guard_word_of(vector, matrix, alpha_one);

    crucible::TensorMeta live = matrix;
    live.data_ptr = crucible::external_data_ptr(&live);
    live.version = 7;
    live.requires_grad = true;
    live.flags = static_cast<uint8_t>(crucible::meta_flags::IS_LEAF | crucible::meta_flags::HAS_GRAD_FN
                                      | crucible::meta_flags::IS_VIEW | crucible::meta_flags::IS_CONTIGUOUS);
    live.grad_fn_hash = crucible::grad_fn_hash(0x51DE);
    live.output_nr = 1;
    live.storage_nbytes = 4096;
    CRUCIBLE_FATAL_INVARIANT(guard_word_of(vector, live, alpha_one) == base);

    crucible::TensorMeta host_zero = matrix;
    host_zero.device_idx = 0;
    crucible::TensorMeta host_none = matrix;
    host_none.device_idx = -1;
    CRUCIBLE_FATAL_INVARIANT(guard_word_of(vector, host_zero, alpha_one)
                             == guard_word_of(vector, host_none, alpha_one));

    TraceRing::Entry tagged{};
    write_scalar_slots(tagged, alpha_one);
    TraceRing::Entry stray = tagged;
    stray.set_packed_scalar_types(
        static_cast<uint16_t>(tagged.packed_scalar_types() | (static_cast<uint16_t>(ScalarType2::NONE) << 2)));
    CRUCIBLE_FATAL_INVARIANT(guard_word_of(tagged) == guard_word_of(stray));
}

// The guard word reads the tag and the role of a data slot, but not its value.
// A loop whose alpha or step size changes at each iteration thus keeps one
// word for each op, and the detector finds its period.  The old word folded
// each value, so the first two checks gave two words.
void test_guard_word_reads_no_data_value() {
    const crucible::TensorMeta vector = vector_meta(4);
    const crucible::TensorMeta matrix = matrix_meta();
    const crucible::ShapeHash alpha_one = guard_word_of(vector, matrix, one_scalar(1, ScalarType2::INT, kData));
    CRUCIBLE_FATAL_INVARIANT(guard_word_of(vector, matrix, one_scalar(2, ScalarType2::INT, kData)) == alpha_one
                             && "the value of a data slot moves no word");
    const int64_t first_lr = ::fixy::bit_cast<int64_t>(1e-3);
    const int64_t second_lr = ::fixy::bit_cast<int64_t>(2e-3);
    CRUCIBLE_FATAL_INVARIANT(guard_word_of(vector, matrix, one_scalar(first_lr, ScalarType2::FLOAT, kData))
                             == guard_word_of(vector, matrix, one_scalar(second_lr, ScalarType2::FLOAT, kData)));
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, matrix, one_scalar(1, ScalarType2::FLOAT, kData)) == alpha_one)
                             && "the tag of a data slot moves the word");
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, matrix, one_scalar(1, ScalarType2::INT)) == alpha_one)
                             && "the role moves the word");
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(vector, matrix, one_scalar(0, ScalarType2::INT, kData))
                               == guard_word_of(vector, matrix, one_scalar(0, ScalarType2::INT)))
                             && "a data slot and a structural zero give two words");
}

// The entry of an op with count scalars: slot 0 is a data scalar of the
// value, and the others are the structural values 1 to count - 1.
// O(count).
[[nodiscard]] TraceRing::Entry entry_with_data_first(uint32_t count, int64_t value) {
    ScalarArgs<true> scalars{};
    scalars.push(value, ScalarType2::FLOAT, kData);
    for (uint32_t index = 1; index < count; index++)
        scalars.push(index, ScalarType2::FLOAT, kStructural);
    TraceRing::Entry entry{};
    write_scalar_slots(entry, scalars, InputOverflow{});
    return entry;
}

// An op with more scalars than the slots keeps a data scalar of the first four
// slots inline, and the digest folds no value of it.  A data scalar that can go
// into the digest slot folds its value, so it is structural in an entry that
// overflows.  The overflow digest of the old code folded each value, so the
// first check gave two words.
void test_overflow_keeps_the_first_data_slots() {
    constexpr uint32_t kCount = 7;
    const TraceRing::Entry first = entry_with_data_first(kCount, 100);
    const TraceRing::Entry second = entry_with_data_first(kCount, 200);
    CRUCIBLE_FATAL_INVARIANT(first.num_scalar_args == kInlineScalars);
    CRUCIBLE_FATAL_INVARIANT(role_of<0>(first) == kData);
    CRUCIBLE_FATAL_INVARIANT(role_of<4>(first) == kStructural && "the digest is structural");
    CRUCIBLE_FATAL_INVARIANT(first.scalar_values.at(::fixy::index<4>()) == second.scalar_values.at(::fixy::index<4>())
                             && "the digest folds no value of the first data slots");
    CRUCIBLE_FATAL_INVARIANT(guard_word_of(first) == guard_word_of(second)
                             && "a data value of an inline slot moves no word of an op that overflows");

    // A data scalar at the last slot or past it folds its value into the
    // digest.
    ScalarArgs<true> late{};
    ScalarArgs<true> late_moved{};
    for (uint32_t index = 0; index < kCount; index++) {
        late.push(index, ScalarType2::FLOAT, index == 5 ? kData : kStructural);
        late_moved.push(index == 5 ? 99 : index, ScalarType2::FLOAT, index == 5 ? kData : kStructural);
    }
    TraceRing::Entry late_entry{};
    TraceRing::Entry late_moved_entry{};
    write_scalar_slots(late_entry, late, InputOverflow{});
    write_scalar_slots(late_moved_entry, late_moved, InputOverflow{});
    CRUCIBLE_FATAL_INVARIANT(!(guard_word_of(late_entry) == guard_word_of(late_moved_entry))
                             && "a data value in the digest moves the word");

    // With no overflow, a data scalar of the last slot stays a data slot, and
    // the two scalar sets give one entry.
    ScalarArgs<true> fits{};
    ScalarArgs<false> bounded{};
    for (uint32_t index = 0; index < kInlineScalars; index++) {
        const ScalarRole role = index + 1 == kInlineScalars ? kData : kStructural;
        fits.push(index, ScalarType2::FLOAT, role);
        bounded.push(index, ScalarType2::FLOAT, role);
    }
    TraceRing::Entry fits_entry{};
    TraceRing::Entry bounded_entry{};
    write_scalar_slots(fits_entry, fits, InputOverflow{});
    write_scalar_slots(bounded_entry, bounded);
    CRUCIBLE_FATAL_INVARIANT(same_slots(fits_entry, bounded_entry));
    CRUCIBLE_FATAL_INVARIANT(role_of<4>(fits_entry) == kData);
}

// is_structural_scalar finds each row of kStructuralScalars, and no other
// argument of the schemas of the rows.  The vessel test
// test_guard_divergence.py checks each row against the types of the schemas
// of PyTorch.
void test_is_structural_scalar_reads_each_row() {
    for (const crucible::vessel::StructuralScalar& row : crucible::vessel::kStructuralScalars) {
        const crucible::SchemaHash schema_hash = crucible::vessel::schema_hash_of_name(row.schema).value();
        const crucible::vessel::ArgumentPosition position =
            crucible::vessel::argument_position_at(row.position).expect("a row names a position of the schema");
        CRUCIBLE_FATAL_INVARIANT(crucible::vessel::is_structural_scalar(schema_hash, position));
    }

    const crucible::SchemaHash pow = crucible::vessel::schema_hash_of_name("aten::pow.Tensor_Scalar").value();
    const crucible::SchemaHash add = crucible::vessel::schema_hash_of_name("aten::add.Tensor").value();
    CRUCIBLE_FATAL_INVARIANT(
        !crucible::vessel::is_structural_scalar(pow, crucible::vessel::ArgumentPosition{::fixy::index<0>()}));
    CRUCIBLE_FATAL_INVARIANT(
        !crucible::vessel::is_structural_scalar(add, crucible::vessel::ArgumentPosition{::fixy::index<2>()})
        && "the alpha of add is data");
}

// The door of the schema hash maps the fold zero to kZeroFoldSchemaHash and
// keeps each other fold, so the schema hash of a name is never zero, the
// sentinel of no schema.  The fold of "aten::mm" is a witness of the fold of
// the fallback, and the empty text folds to the mapped value.
static_assert(crucible::vessel::schema_hash_of_fold(crucible::SchemaHash{0}).value()
              == crucible::SchemaHash{crucible::vessel::kZeroFoldSchemaHash});
static_assert(crucible::vessel::schema_hash_of_fold(crucible::SchemaHash{1}).value() == crucible::SchemaHash{1});
static_assert(crucible::vessel::schema_hash_of_fold(crucible::SchemaHash{UINT64_MAX}).value()
              == crucible::SchemaHash{UINT64_MAX});
static_assert(crucible::vessel::schema_hash_of_name("aten::mm").value() == crucible::SchemaHash{0x983B9200D566222DULL});
static_assert(crucible::vessel::schema_hash_of_name("").value()
              == crucible::SchemaHash{crucible::vessel::kZeroFoldSchemaHash});

// The context of a Vigil's producer claim, which a registration of a name
// takes.
constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

// A name whose fold is zero registers as an internal name, and the process
// lives.  The test forces the fold zero through the door that the fallback
// and crucible_hash_string take their hash from.  The door gives
// kZeroFoldSchemaHash, so the table checks a hash that is not zero.  The
// fallback once gave the fold itself, and the table ended the process on the
// hash zero of a name of libtorch.
void test_name_of_zero_fold_registers() {
    crucible::SchemaTable table;
    const auto view = table.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    constexpr crucible::SchemaHash hash = crucible::vessel::schema_hash_of_fold(crucible::SchemaHash{0}).value();
    const bool is_registered =
        table
            .register_name(kTest, ::foundation::effects::testing::test().alloc, view,
                           crucible::InternalSchemaName{hash, ::fixy::TextView{"aten::zero_fold"}})
            .is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_registered && "the table takes a name whose fold is zero");
    const ::fixy::Option<crucible::InternedSchemaName> stored = table.lookup(hash);
    const bool is_found = stored.match(
        [](crucible::InternedSchemaName const& found) noexcept {
            return found.value().view() == ::fixy::TextView{"aten::zero_fold"};
        },
        [] noexcept { return false; });
    CRUCIBLE_FATAL_INVARIANT(is_found && "the table gives the name of the mapped hash");
}

// Each fact of a tensor that changes the elements that an op reads moves the
// content hash of its region and the overflow digest of an input past the
// limit: the rank, the layout, the negative bit, the conjugate bit and the
// storage offset.  The old content hash folded none of the five, and the old
// digest folded only the rank, so each pair below gave one hash for two
// regions.  A lane of extent zero and stride zero adds nothing to the XOR fold
// of the dims, so only the rank tells [4, 0] from [4] in the content hash.
// The test reports each fact that does not move a hash, and then stops.  The
// flags that no op reads move no hash.
void test_each_fact_of_a_tensor_moves_the_hash_and_the_digest() {
    const crucible::TensorMeta vector = vector_meta(4);
    const crucible::ContentHash base = content_hash_of(vector);
    const uint64_t base_digest = overflow_digest_of(vector);
    uint32_t unmoved = 0;
    const auto expect_moved = [&unmoved, &base, base_digest](::fixy::TextView fact,
                                                             crucible::TensorMeta const& changed) {
        if (content_hash_of(changed) == base) {
            ++unmoved;
            ::fixy::report(::fixy::Sink::Err, "  the {} does not move the content hash\n", fact);
        }
        if (overflow_digest_of(changed) == base_digest) {
            ++unmoved;
            ::fixy::report(::fixy::Sink::Err, "  the {} does not move the digest of an input past the limit\n", fact);
        }
    };

    crucible::TensorMeta empty_rows = vector;
    empty_rows.ndim = 2;
    expect_moved("rank", empty_rows);

    crucible::TensorMeta sparse = vector;
    sparse.layout = crucible::Layout::Sparse;
    expect_moved("layout", sparse);

    crucible::TensorMeta negative = vector;
    negative.flags = crucible::meta_flags::IS_NEG;
    expect_moved("negative bit", negative);

    crucible::TensorMeta conjugate = vector;
    conjugate.flags = crucible::meta_flags::IS_CONJ;
    expect_moved("conjugate bit", conjugate);

    crucible::TensorMeta shifted = vector;
    shifted.storage_offset = 4;
    expect_moved("storage offset", shifted);

    CRUCIBLE_FATAL_INVARIANT(unmoved == 0 && "each fact of a tensor that an op reads moves the hash and the digest");

    crucible::TensorMeta live = vector;
    live.flags = static_cast<uint8_t>(crucible::meta_flags::IS_LEAF | crucible::meta_flags::HAS_GRAD_FN
                                      | crucible::meta_flags::IS_VIEW | crucible::meta_flags::IS_CONTIGUOUS);
    CRUCIBLE_FATAL_INVARIANT(content_hash_of(live) == base && "the flags that no op reads move no hash");
    CRUCIBLE_FATAL_INVARIANT(overflow_digest_of(live) == base_digest && "the flags that no op reads move no digest");
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
    test_content_hash_reads_no_data_value();
    test_each_fact_of_a_tensor_moves_the_hash_and_the_digest();
    test_built_device_has_one_host_index();
    test_guard_word_reads_each_field_of_the_computation();
    test_guard_word_reads_the_overflow_digest();
    test_guard_word_ignores_what_changes_at_each_iteration();
    test_guard_word_reads_no_data_value();
    test_overflow_keeps_the_first_data_slots();
    test_is_structural_scalar_reads_each_row();
    test_name_of_zero_fold_registers();
    ::fixy::check::pass(kTest, "test_vessel_entry_slots: PASS\n");
    return 0;
}
