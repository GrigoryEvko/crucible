// The bits of the packed words of the record path.
//
// Each word of the record path packs its fields by a fixed layout, and each
// layout is part of a format or of a key:
//
//   - The generation of a storage (StorageGeneration): the count or the
//     period in the low 31 bits, and the unknown bit at bit 31.
//   - The word of a recipe (compute_recipe_hash) and of a numerics policy
//     (compute_numerics_hash), which their finalizer mixes.
//   - The word of a tensor (pack_tensor_word), which the content hash and the
//     guard word fold, and the placement word of the overflow digest.
//   - The header of the guard word of an op (compute_shape_hash).
//   - The flags and the tags of the scalar slots of a ring entry.
//
// Each check gives the fields of one word and compares the word with a
// literal.  The literals come from the layouts, not from a run of the code.
// So a change of a layout, or of a field width, shows here first.

#include "../vessel/torch/entry_slots.h"

#include <crucible/DimHash.h>
#include <crucible/MerkleDag.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <foundation/reflect/Mix.h>

#include <cstdint>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using crucible::ScalarType;
using crucible::ScalarType2;
using crucible::TraceRing;
using ::foundation::reflect::combine_ids;

// The generation of a storage: the low 31 bits of the count of a known
// storage, or the unknown bit and the low 31 bits of the period.  A known
// count with no low bit set becomes 1, so a known generation is never zero.
void test_storage_generation_word() {
    using crucible::AllocationCount;
    using crucible::AllocatorEpoch;
    using crucible::StorageGeneration;

    CRUCIBLE_TEST_ASSERT(kTest, StorageGeneration{}.raw() == 0x00000000U);
    CRUCIBLE_TEST_ASSERT(kTest, StorageGeneration::known(AllocationCount{uint32_t{5}}).raw() == 0x00000005U);
    CRUCIBLE_TEST_ASSERT(kTest, StorageGeneration::known(AllocationCount{uint32_t{0x7FFFFFFF}}).raw() == 0x7FFFFFFFU);
    CRUCIBLE_TEST_ASSERT(kTest, StorageGeneration::known(AllocationCount{uint32_t{0x80000005}}).raw() == 0x00000005U);
    CRUCIBLE_TEST_ASSERT(kTest, StorageGeneration::known(AllocationCount{uint32_t{0x80000000}}).raw() == 0x00000001U);
    CRUCIBLE_TEST_ASSERT(kTest, StorageGeneration::known(AllocationCount{uint32_t{0}}).raw() == 0x00000001U);
    CRUCIBLE_TEST_ASSERT(kTest, StorageGeneration::unknown(AllocatorEpoch{uint32_t{0}}).raw() == 0x80000000U);
    CRUCIBLE_TEST_ASSERT(kTest, StorageGeneration::unknown(AllocatorEpoch{uint32_t{7}}).raw() == 0x80000007U);
    CRUCIBLE_TEST_ASSERT(kTest, StorageGeneration::unknown(AllocatorEpoch{uint32_t{0xFFFFFFFF}}).raw() == 0xFFFFFFFFU);

    CRUCIBLE_TEST_ASSERT(kTest, !StorageGeneration{}.is_known() && !StorageGeneration{}.is_unknown());
    const StorageGeneration known = StorageGeneration::known(AllocationCount{uint32_t{0x80000005}});
    CRUCIBLE_TEST_ASSERT(kTest, known.is_known() && !known.is_unknown());
    const StorageGeneration unknown = StorageGeneration::unknown(AllocatorEpoch{uint32_t{0}});
    CRUCIBLE_TEST_ASSERT(kTest, !unknown.is_known() && unknown.is_unknown());
    // The order of two generations is the order of their words.
    CRUCIBLE_TEST_ASSERT(kTest, StorageGeneration{} < known && known < unknown);
    ::fixy::check::pass(kTest, "  test_storage_generation_word: PASSED\n");
}

// The hash of a word that no finalizer result reserves: the finalizer of the
// word.  The finalizer is a bijection, so the hash pins each bit of the word.
[[nodiscard]] uint64_t plain_hash_of(uint64_t word) {
    const uint64_t mixed = ::foundation::reflect::fmix64(word);
    CRUCIBLE_TEST_ASSERT(kTest, mixed != 0 && mixed != UINT64_MAX);
    return mixed;
}

// The word of a recipe: one byte for each of the seven enumerations, from bit
// 0, and the flags in the high byte.  The dtype Undefined is the byte 0xFF.
void test_recipe_word() {
    crucible::NumericalRecipe recipe{};
    recipe.accum_dtype = ScalarType::Double;
    recipe.out_dtype = ScalarType::Undefined;
    recipe.reduction_algo = crucible::ReductionAlgo::KAHAN;
    recipe.rounding = ::foundation::core::RoundingMode::RM;
    recipe.scale_policy = crucible::ScalePolicy::PER_CHANNEL;
    recipe.softmax = crucible::SoftmaxRecurrence::FLASH3;
    recipe.determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
    recipe.flags.set(crucible::RecipeFlags::FLUSH_TO_ZERO);
    recipe.flags.set(crucible::RecipeFlags::ATTN_MASK_ADD_IN_FP32);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::compute_recipe_hash(recipe).raw() == plain_hash_of(0x090303050202FF07ULL)
                                    && "the layout of the word of a recipe moved");

    // The two dtype bytes at 0xFF and each flag set.
    crucible::NumericalRecipe edge = recipe;
    edge.accum_dtype = ScalarType::Undefined;
    edge.rounding = ::foundation::core::RoundingMode::RP;
    edge.reduction_algo = crucible::ReductionAlgo::BLOCK_STABLE;
    edge.flags.set(crucible::RecipeFlags::SPLIT_K_ATOMIC_OK);
    edge.flags.set(crucible::RecipeFlags::ALLOW_DENORMAL);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::compute_recipe_hash(edge).raw() == plain_hash_of(0x0F0303050303FFFFULL)
                                    && "the layout of the word of a recipe moved");
    ::fixy::check::pass(kTest, "  test_recipe_word: PASSED\n");
}

// The word of a numerics policy: the determinism, the accumulator of the 16-bit
// formats, the accumulator of the fp8 formats and the flags, one byte each from
// bit 0, and the eager word in the high 32 bits.  The salt of a policy goes in
// before the finalizer.
void test_numerics_policy_word() {
    constexpr uint64_t kNumericsSalt = 0x4E554D4552494353ULL;
    crucible::NumericsPolicy policy{};
    policy.determinism = crucible::ReductionDeterminism::BITEXACT_TC;
    policy.accum_16bit = ScalarType::Float;
    policy.accum_8bit = ScalarType::Half;
    policy.flags.set(crucible::RecipeFlags::SPLIT_K_ATOMIC_OK);
    crucible::EagerNumerics eager{};
    eager.bits = crucible::EagerNumerics::MATMUL_TF32 | crucible::EagerNumerics::FLUSH_DENORMAL | 1U;
    CRUCIBLE_TEST_ASSERT(kTest, crucible::compute_numerics_hash(policy, eager).raw()
                                        == plain_hash_of(0x0000001502050602ULL ^ kNumericsSalt)
                                    && "the layout of the word of a numerics policy moved");

    // Each bit of the eager word reaches the high half, and the dtype
    // Undefined is the byte 0xFF.
    policy.accum_8bit = ScalarType::Undefined;
    eager.bits = 0xFFFFFFFFU;
    CRUCIBLE_TEST_ASSERT(kTest, crucible::compute_numerics_hash(policy, eager).raw()
                                        == plain_hash_of(0xFFFFFFFF02FF0602ULL ^ kNumericsSalt)
                                    && "the layout of the word of a numerics policy moved");
    ::fixy::check::pass(kTest, "  test_numerics_policy_word: PASSED\n");
}

// A tensor of rank two on a device, with each flag that a word reads.  The
// flags IS_LEAF and IS_VIEW reach no word.
[[nodiscard]] crucible::TensorMeta device_matrix() {
    crucible::TensorMeta meta{};
    meta.dtype = ScalarType::Float;
    meta.device_type = crucible::DeviceType::CUDA;
    meta.device_idx = 3;
    meta.ndim = 2;
    meta.sizes[0] = crucible::tensor_dim(3);
    meta.sizes[1] = crucible::tensor_dim(4);
    meta.strides[0] = crucible::tensor_dim(4);
    meta.strides[1] = crucible::tensor_dim(1);
    meta.layout = crucible::Layout::SparseCsr;
    meta.flags = static_cast<uint8_t>(crucible::meta_flags::IS_NEG | crucible::meta_flags::IS_CONJ
                                      | crucible::meta_flags::IS_LEAF | crucible::meta_flags::IS_VIEW);
    meta.storage_offset = 5;
    return meta;
}

// The word of the fields of a tensor in the word of pack_tensor_word: the word
// with the dims and the storage offset taken out.
[[nodiscard]] uint64_t packed_fields_of(crucible::TensorMeta const& meta) {
    const crucible::DimHashDet dims = crucible::detail::dim_hash_scalar_det(meta);
    const uint64_t offset_word = static_cast<uint64_t>(meta.storage_offset) * crucible::detail::kStorageOffsetMix;
    return crucible::pack_tensor_word(meta, dims) ^ crucible::raw_dim_hash(dims) ^ offset_word;
}

// The word of a tensor: the dtype, the device type, the device index, the
// rank, the layout and the negative and conjugate bits, one byte each from bit
// 0.  The host has the one device index -1, the byte 0xFF.
void test_tensor_word() {
    CRUCIBLE_TEST_ASSERT(kTest, packed_fields_of(device_matrix()) == 0x0000300202030106ULL
                                    && "the layout of the word of a tensor moved");

    crucible::TensorMeta host{};
    host.dtype = ScalarType::Bool;
    host.device_type = crucible::DeviceType::CPU;
    host.device_idx = 0;
    CRUCIBLE_TEST_ASSERT(kTest,
                         packed_fields_of(host) == 0x0000000000FF000BULL && "the layout of the word of a tensor moved");

    crucible::TensorMeta edge{};
    edge.dtype = ScalarType::Float8_e4m3fn;
    edge.device_type = crucible::DeviceType::PrivateUse1;
    edge.device_idx = 127;
    edge.ndim = crucible::kMaxTensorNDim;
    edge.layout = crucible::Layout::SparseBsc;
    edge.flags = 0x3F;
    CRUCIBLE_TEST_ASSERT(kTest,
                         packed_fields_of(edge) == 0x00003005087F1418ULL && "the layout of the word of a tensor moved");
    ::fixy::check::pass(kTest, "  test_tensor_word: PASSED\n");
}

// The element type Undefined is the byte 0xFF of the word of a tensor, and it
// sets no bit of another field.  So two tensors of that element type that
// differ only in the layout, the device or the rank give two words.
void test_undefined_dtype_keeps_each_field() {
    const crucible::TensorMeta undefined{};
    CRUCIBLE_TEST_ASSERT(kTest, undefined.dtype == ScalarType::Undefined);
    CRUCIBLE_TEST_ASSERT(kTest, packed_fields_of(undefined) == 0x0000000000FF00FFULL
                                    && "the element type Undefined sets the bits of another field");

    crucible::TensorMeta sparse = undefined;
    sparse.layout = crucible::Layout::SparseCsr;
    crucible::TensorMeta on_device = undefined;
    on_device.device_type = crucible::DeviceType::CUDA;
    on_device.device_idx = 1;
    crucible::TensorMeta conjugate = undefined;
    conjugate.flags = crucible::meta_flags::IS_CONJ;
    const ::fixy::FixedArray<crucible::TensorMeta, 3> others{sparse, on_device, conjugate};
    const uint64_t undefined_word =
        crucible::pack_tensor_word(undefined, crucible::detail::dim_hash_scalar_det(undefined));
    for (const crucible::TensorMeta& other : others.view()) {
        CRUCIBLE_TEST_ASSERT(kTest, crucible::pack_tensor_word(other, crucible::detail::dim_hash_scalar_det(other))
                                            != undefined_word
                                        && "two tensors of the element type Undefined give one word");
    }
    CRUCIBLE_TEST_ASSERT(kTest, packed_fields_of(on_device) == 0x00000000000101FFULL);
    ::fixy::check::pass(kTest, "  test_undefined_dtype_keeps_each_field: PASSED\n");
}

// The placement word of the overflow digest: the word of a tensor with no rank
// field.  The digest folds the rank, each extent and stride, and then the
// placement word with the storage offset.
void test_overflow_placement_word() {
    crucible::vessel::InputOverflow overflow{};
    overflow.fold(device_matrix());
    const uint64_t offset_word = uint64_t{5} * crucible::detail::kStorageOffsetMix;
    uint64_t expected = combine_ids(crucible::vessel::kOverflowSeed, 2);
    expected = combine_ids(expected, 3);
    expected = combine_ids(expected, 4);
    expected = combine_ids(expected, 4);
    expected = combine_ids(expected, 1);
    expected = combine_ids(expected, 0x0000300200030106ULL ^ offset_word);
    CRUCIBLE_TEST_ASSERT(kTest, overflow.digest == expected && "the layout of the placement word moved");
    CRUCIBLE_TEST_ASSERT(kTest, overflow.count == 1);
    ::fixy::check::pass(kTest, "  test_overflow_placement_word: PASSED\n");
}

// The header of the guard word: the input count in bits 0 to 15, the scalar
// count in bits 16 to 31, the tags of the stored slots from bit 32, the data
// slots from bit 42, and the depth in bits 48 to 63.  The guard word folds the
// header with its seed, and then each stored scalar, with zero for a data slot.
void test_guard_header_word() {
    crucible::vessel::ScalarArgs<false> scalars{};
    scalars.push(9, ScalarType2::INT, crucible::ScalarRole::STRUCTURAL);
    scalars.push(0x4004000000000000LL, ScalarType2::FLOAT, crucible::ScalarRole::DATA);
    scalars.push(1, ScalarType2::BOOL, crucible::ScalarRole::STRUCTURAL);
    TraceRing::Entry entry{};
    crucible::vessel::write_scalar_slots(entry, scalars);
    entry.num_inputs = 2;
    CRUCIBLE_TEST_ASSERT(kTest, entry.num_scalar_args == 3);
    CRUCIBLE_TEST_ASSERT(kTest, entry.packed_scalar_types() == 0x0024);
    CRUCIBLE_TEST_ASSERT(kTest, entry.data_slot_bits == 0x02);

    uint64_t expected = combine_ids(crucible::vessel::kGuardSeed, 0xFFFF082400030002ULL);
    expected = combine_ids(expected, 9);
    expected = combine_ids(expected, 0);
    expected = combine_ids(expected, 1);
    const crucible::ShapeHash guard = crucible::vessel::compute_shape_hash(::fixy::View<const crucible::TensorMeta>{},
                                                                           entry, crucible::OpDepth{uint16_t{0xFFFF}});
    CRUCIBLE_TEST_ASSERT(kTest, guard.raw() == expected && "the layout of the header of the guard word moved");
    ::fixy::check::pass(kTest, "  test_guard_header_word: PASSED\n");
}

// The flags of a ring entry: the inference mode at bit 0, the mutability at
// bit 1, the phase in bits 2 and 3, the torch function mode at bit 4, the
// gradient mode at bit 5, and the tag of slot 4 in bits 6 and 7.  The tags of
// slots 0 to 3 take two bits each in scalar_types, slot 0 in the low bits.
void test_entry_flags_word() {
    using crucible::OpFlags;
    using crucible::TrainingPhase;
    CRUCIBLE_TEST_ASSERT(kTest, OpFlags{}.bits() == 0x00);
    CRUCIBLE_TEST_ASSERT(kTest, OpFlags{}.with_inference_mode().bits() == 0x01);
    CRUCIBLE_TEST_ASSERT(kTest, OpFlags{}.with_mutable().bits() == 0x02);
    CRUCIBLE_TEST_ASSERT(kTest, OpFlags{}.with_phase(TrainingPhase::BACKWARD).bits() == 0x04);
    CRUCIBLE_TEST_ASSERT(kTest, OpFlags{}.with_phase(TrainingPhase::OPTIMIZER).bits() == 0x08);
    CRUCIBLE_TEST_ASSERT(kTest, OpFlags{}.with_phase(TrainingPhase::OTHER).bits() == 0x0C);
    CRUCIBLE_TEST_ASSERT(kTest, OpFlags{}.with_torch_function().bits() == 0x10);
    CRUCIBLE_TEST_ASSERT(kTest, OpFlags{}.with_grad_enabled().bits() == 0x20);
    CRUCIBLE_TEST_ASSERT(kTest, OpFlags{}.with_fifth_tag(ScalarType2::FLOAT).bits() == 0x40);
    CRUCIBLE_TEST_ASSERT(kTest, OpFlags{}.with_fifth_tag(ScalarType2::NONE).bits() == 0xC0);

    const OpFlags each = OpFlags{}
                             .with_inference_mode()
                             .with_mutable()
                             .with_phase(TrainingPhase::OPTIMIZER)
                             .with_torch_function()
                             .with_grad_enabled();
    CRUCIBLE_TEST_ASSERT(kTest, each.bits() == 0x3B);
    CRUCIBLE_TEST_ASSERT(kTest, each.is_inference_mode() && each.is_mutable() && each.is_torch_function()
                                    && each.is_grad_enabled() && each.phase() == TrainingPhase::OPTIMIZER
                                    && each.fifth_tag() == ScalarType2::INT);

    TraceRing::Entry entry{};
    entry.op_flags = OpFlags{}.with_grad_enabled().with_mutable();
    CRUCIBLE_TEST_ASSERT(kTest, entry.op_flags.bits() == 0x22);
    entry.set_scalar_type(0, ScalarType2::FLOAT);
    entry.set_scalar_type(1, ScalarType2::NONE);
    entry.set_scalar_type(2, ScalarType2::BOOL);
    entry.set_scalar_type(3, ScalarType2::INT);
    entry.set_scalar_type(4, ScalarType2::NONE);
    CRUCIBLE_TEST_ASSERT(kTest, entry.scalar_types.bits() == 0x2D);
    CRUCIBLE_TEST_ASSERT(kTest, entry.op_flags.bits() == 0xE2);
    CRUCIBLE_TEST_ASSERT(kTest, entry.packed_scalar_types() == 0x032D);
    CRUCIBLE_TEST_ASSERT(kTest, entry.get_scalar_type(4) == ScalarType2::NONE);
    CRUCIBLE_TEST_ASSERT(kTest, entry.get_scalar_type(2) == ScalarType2::BOOL);

    entry.set_packed_scalar_types(0x0114);
    CRUCIBLE_TEST_ASSERT(kTest, entry.scalar_types.bits() == 0x14);
    CRUCIBLE_TEST_ASSERT(kTest, entry.op_flags.bits() == 0x62);
    CRUCIBLE_TEST_ASSERT(kTest, entry.get_scalar_type(4) == ScalarType2::FLOAT);
    // The bits of packed past the five tags go.
    entry.set_packed_scalar_types(0xFC00);
    CRUCIBLE_TEST_ASSERT(kTest, entry.packed_scalar_types() == 0x0000 && entry.op_flags.bits() == 0x22);
    ::fixy::check::pass(kTest, "  test_entry_flags_word: PASSED\n");
}

// The doors of a byte from outside.  A caller of the C entries gives the five
// fields of the front end, and the tag of slot 4 is the entry's own.  A stored
// byte of a trace record or of an image of a region holds the four fields of
// the context, and a trace record keeps the gradient mode in a byte of 0 or 1.
// Each other pattern gives its refusal.
void test_entry_flags_doors() {
    using crucible::OpFlags;
    using crucible::OpFlagsRefusal;
    const auto is_refused = [](::fixy::Result<OpFlags, OpFlagsRefusal> got, OpFlagsRefusal want) noexcept {
        return std::move(got).is_err_and([want](OpFlagsRefusal refusal) noexcept { return refusal == want; });
    };
    const auto bits_of = [](::fixy::Result<OpFlags, OpFlagsRefusal> got) noexcept {
        return std::move(got).expect("the door gives the flags").bits();
    };

    CRUCIBLE_TEST_ASSERT(kTest, bits_of(OpFlags::from_caller_byte(0x3F)) == 0x3F);
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(OpFlags::from_caller_byte(0x40), OpFlagsRefusal::TagOfAScalarSlot));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(OpFlags::from_caller_byte(0x80), OpFlagsRefusal::TagOfAScalarSlot));

    CRUCIBLE_TEST_ASSERT(kTest, bits_of(OpFlags::from_stored_byte(0x1F)) == 0x1F);
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(OpFlags::from_stored_byte(0x20), OpFlagsRefusal::GradientInStoredFlags));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(OpFlags::from_stored_byte(0xC0), OpFlagsRefusal::TagOfAScalarSlot));

    CRUCIBLE_TEST_ASSERT(kTest, bits_of(OpFlags::from_record(0x1F, 1)) == 0x3F);
    CRUCIBLE_TEST_ASSERT(kTest, bits_of(OpFlags::from_record(0x06, 0)) == 0x06);
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(OpFlags::from_record(0x00, 2), OpFlagsRefusal::GradientByteNotABit));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(OpFlags::from_record(0x20, 0), OpFlagsRefusal::GradientInStoredFlags));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(OpFlags::from_record(0x40, 1), OpFlagsRefusal::TagOfAScalarSlot));
    ::fixy::check::pass(kTest, "  test_entry_flags_doors: PASSED\n");
}

}  // namespace

int main() {
    test_storage_generation_word();
    test_recipe_word();
    test_numerics_policy_word();
    test_tensor_word();
    test_undefined_dtype_keeps_each_field();
    test_overflow_placement_word();
    test_guard_header_word();
    test_entry_flags_word();
    test_entry_flags_doors();
    ::fixy::check::pass(kTest, "test_record_packed_words: PASS\n");
    return 0;
}
