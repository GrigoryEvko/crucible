// The fixed-seed weights and the direct CPU reference pass of
// test_compute_vit.

#include "compute_vit.h"

#include "cpu_kernels.h"

#include <crucible/Philox.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The position of a draw in the Philox stream of a block.
using Draw = ::fixy::Id<struct DrawTag, std::uint64_t>;

// The value of the draw of a weight in [-0.3, 0.3), from the first word of
// the Philox block of the counter of the draw, with Key.
template <std::uint64_t Key>
[[nodiscard]] float draw_weight(Draw draw) noexcept {
    const ::fixy::Philox4x32::Counter word = ::fixy::Philox4x32::generate(draw.raw(), Key);
    return -0.3f + 0.6f * crucible::Philox::to_uniform(word.at(::fixy::index<0>()));
}

// A block of count floats on the host at an alignment of 64.  The float at
// position i is value_of(draw_weight<Key>(i)), so each run and each host
// draws the same floats.
template <std::uint64_t Key, class ValueOf>
[[nodiscard]] ::foundation::AlignedBytes make_drawn_block(::fixy::ElementCount count, ValueOf const& value_of) {
    using Start = ::foundation::core::side::WindowStart;
    using Length = ::foundation::core::side::WindowLength;
    const ::fixy::ByteCount bytes{count.raw() * sizeof(float)};
    ::foundation::AlignedBytes block = ::foundation::mint_aligned_bytes_zeroed(
        ::foundation::effects::testing::test().alloc,
        ::fixy::mint_layout(bytes, ::fixy::mint_alignment(::fixy::ByteCount{64}).expect("64 is a power of two"))
            .expect("a block of floats of the test is a layout"));
    ::fixy::FixedList<::foundation::ByteWindow, 1> windows{};
    CRUCIBLE_TEST_ASSERT(kTest, windows
                                    .append(::foundation::ByteWindow{::fixy::side_as<Start>(::fixy::ByteCount{0}),
                                                                     ::fixy::side_as<Length>(bytes)})
                                    .is_ok());
    const auto drawn = ::foundation::with_mut_windows<float>(block, windows, [&value_of](auto& views) noexcept {
        std::uint64_t draw = 0;
        for (auto& view : views) {
            for (float& element : view) {
                element = value_of(draw_weight<Key>(Draw{draw}));
                ++draw;
            }
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, drawn.is_ok());
    return block;
}

}  // namespace

using namespace crucible;
using ::foundation::core::side::LeftFactor;
using ::foundation::core::side::RightFactor;
using cpu::make_dim;
using cpu::make_input;
namespace roles = cpu::side;

namespace test_compute_vit {

void fill_weights(Weights& weights) {
    const auto as_drawn = [](float value) noexcept { return value; };
    const auto as_scale = [](float value) noexcept { return 1.0f + value * 0.1f; };
    const auto as_shift = [](float value) noexcept { return value * 0.1f; };

    weights.X = make_drawn_block<1>(::fixy::ElementCount{B * S * D}, as_drawn);
    weights.W_q = make_drawn_block<2>(::fixy::ElementCount{D * D}, as_drawn);
    weights.W_k = make_drawn_block<3>(::fixy::ElementCount{D * D}, as_drawn);
    weights.W_v = make_drawn_block<4>(::fixy::ElementCount{D * D}, as_drawn);
    weights.W_out = make_drawn_block<5>(::fixy::ElementCount{D * D}, as_drawn);
    weights.W_ff1 = make_drawn_block<6>(::fixy::ElementCount{D * D_FF}, as_drawn);
    weights.W_ff2 = make_drawn_block<7>(::fixy::ElementCount{D_FF * D}, as_drawn);
    weights.W_head = make_drawn_block<8>(::fixy::ElementCount{D * N_CLS}, as_drawn);

    weights.gamma1 = make_drawn_block<9>(::fixy::ElementCount{D}, as_scale);
    weights.beta1 = make_drawn_block<10>(::fixy::ElementCount{D}, as_shift);
    weights.gamma2 = make_drawn_block<11>(::fixy::ElementCount{D}, as_scale);
    weights.beta2 = make_drawn_block<12>(::fixy::ElementCount{D}, as_shift);
}

void compute_reference(const Weights& weights, Reference& reference) {
    ::fixy::FixedArray<float, B * S * D> ref_norm1{};
    ::fixy::FixedArray<float, B * S * D> ref_Q{};
    ::fixy::FixedArray<float, B * S * D> ref_K{};
    ::fixy::FixedArray<float, B * S * D> ref_V{};
    ::fixy::FixedArray<float, B * S * D> ref_attn{};
    ::fixy::FixedArray<float, B * S * D> ref_proj{};
    ::fixy::FixedArray<float, B * S * D> ref_res1{};
    ::fixy::FixedArray<float, B * S * D> ref_norm2{};
    ::fixy::FixedArray<float, B * S * D_FF> ref_ff1{};
    ::fixy::FixedArray<float, B * S * D_FF> ref_relu{};
    ::fixy::FixedArray<float, B * S * D> ref_ff2{};
    ::fixy::FixedArray<float, B * D> ref_cls{};
    ::fixy::FixedArray<float, B * N_CLS> ref_logits{};

    // A matrix product of [B*S, D] with [D, D].
    const auto square_mm = [](::fixy::Side<LeftFactor, ::fixy::View<float const>> left,
                              ::fixy::Side<RightFactor, ::fixy::View<float const>> right,
                              ::fixy::View<float> out) noexcept {
        cpu::mm(kTest, left, right, out, make_dim<roles::Rows>(B * S), make_dim<roles::Columns>(D),
                make_dim<roles::Inner>(D));
    };

    cpu::layer_norm(kTest, read_floats(weights.X), make_input<roles::Scale>(read_floats(weights.gamma1)),
                    make_input<roles::Shift>(read_floats(weights.beta1)), ref_norm1.view(),
                    make_dim<roles::Rows>(B * S), make_dim<roles::Width>(D));
    square_mm(make_input<LeftFactor>(ref_norm1.view()), make_input<RightFactor>(read_floats(weights.W_q)),
              ref_Q.view());
    square_mm(make_input<LeftFactor>(ref_norm1.view()), make_input<RightFactor>(read_floats(weights.W_k)),
              ref_K.view());
    square_mm(make_input<LeftFactor>(ref_norm1.view()), make_input<RightFactor>(read_floats(weights.W_v)),
              ref_V.view());
    cpu::sdpa(kTest, ref_Q.view(), make_input<roles::Keys>(ref_K.view()), make_input<roles::Values>(ref_V.view()),
              ref_attn.view(), make_dim<roles::Batch>(B), make_dim<roles::Sequence>(S), make_dim<roles::Width>(D));
    square_mm(make_input<LeftFactor>(ref_attn.view()), make_input<RightFactor>(read_floats(weights.W_out)),
              ref_proj.view());
    cpu::add(kTest, make_input<roles::Augend>(ref_proj.view()), make_input<roles::Addend>(read_floats(weights.X)),
             ref_res1.view(), make_dim<roles::Count>(B * S * D));
    cpu::layer_norm(kTest, ref_res1.view(), make_input<roles::Scale>(read_floats(weights.gamma2)),
                    make_input<roles::Shift>(read_floats(weights.beta2)), ref_norm2.view(),
                    make_dim<roles::Rows>(B * S), make_dim<roles::Width>(D));
    cpu::mm(kTest, make_input<LeftFactor>(ref_norm2.view()), make_input<RightFactor>(read_floats(weights.W_ff1)),
            ref_ff1.view(), make_dim<roles::Rows>(B * S), make_dim<roles::Columns>(D_FF), make_dim<roles::Inner>(D));
    cpu::relu(kTest, ref_ff1.view(), ref_relu.view(), make_dim<roles::Count>(B * S * D_FF));
    cpu::mm(kTest, make_input<LeftFactor>(ref_relu.view()), make_input<RightFactor>(read_floats(weights.W_ff2)),
            ref_ff2.view(), make_dim<roles::Rows>(B * S), make_dim<roles::Columns>(D), make_dim<roles::Inner>(D_FF));
    cpu::add(kTest, make_input<roles::Augend>(ref_ff2.view()), make_input<roles::Addend>(ref_res1.view()),
             reference.res2.view(), make_dim<roles::Count>(B * S * D));
    cpu::index_select(kTest, reference.res2.view(), ref_cls.view(), make_dim<roles::Batch>(B),
                      make_dim<roles::Sequence>(S), make_dim<roles::Width>(D), make_dim<roles::Selected>(0));
    cpu::mm(kTest, make_input<LeftFactor>(ref_cls.view()), make_input<RightFactor>(read_floats(weights.W_head)),
            ref_logits.view(), make_dim<roles::Rows>(B), make_dim<roles::Columns>(N_CLS), make_dim<roles::Inner>(D));
    cpu::softmax(kTest, ref_logits.view(), reference.probs.view(), make_dim<roles::Rows>(B),
                 make_dim<roles::Width>(N_CLS));
}

}  // namespace test_compute_vit
