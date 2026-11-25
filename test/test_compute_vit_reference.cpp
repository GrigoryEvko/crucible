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
    auto [drawn, drawn_block] =
        ::foundation::with_mut_windows<float>(std::move(block), windows, [&value_of](auto& views) noexcept {
            std::uint64_t draw = 0;
            std::move(views).drain([&draw, &value_of](auto view) noexcept {
                for (float& element : view) {
                    element = value_of(draw_weight<Key>(Draw{draw}));
                    ++draw;
                }
            });
        });
    CRUCIBLE_TEST_ASSERT(kTest, drawn.is_ok());
    return std::move(drawn_block);
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
    // A matrix product of [B*S, D] with [D, D].
    const auto square_mm = [](::fixy::Side<LeftFactor, ::fixy::View<float const>> left,
                              ::fixy::Side<RightFactor, ::fixy::View<float const>> right, auto out) noexcept {
        cpu::mm(kTest, left, right, std::move(out), make_dim<roles::Rows>(B * S), make_dim<roles::Columns>(D),
                make_dim<roles::Inner>(D));
    };

    ::fixy::FixedArray<float, B * S * D> const ref_norm1 =
        ::fixy::build_fixed_array<float, B * S * D>([&weights](auto out) noexcept {
            cpu::layer_norm(kTest, read_floats(weights.X), make_input<roles::Scale>(read_floats(weights.gamma1)),
                            make_input<roles::Shift>(read_floats(weights.beta1)), std::move(out),
                            make_dim<roles::Rows>(B * S), make_dim<roles::Width>(D));
        });
    ::fixy::FixedArray<float, B * S * D> const ref_Q =
        ::fixy::build_fixed_array<float, B * S * D>([&](auto out) noexcept {
            square_mm(make_input<LeftFactor>(ref_norm1.view()), make_input<RightFactor>(read_floats(weights.W_q)),
                      std::move(out));
        });
    ::fixy::FixedArray<float, B * S * D> const ref_K =
        ::fixy::build_fixed_array<float, B * S * D>([&](auto out) noexcept {
            square_mm(make_input<LeftFactor>(ref_norm1.view()), make_input<RightFactor>(read_floats(weights.W_k)),
                      std::move(out));
        });
    ::fixy::FixedArray<float, B * S * D> const ref_V =
        ::fixy::build_fixed_array<float, B * S * D>([&](auto out) noexcept {
            square_mm(make_input<LeftFactor>(ref_norm1.view()), make_input<RightFactor>(read_floats(weights.W_v)),
                      std::move(out));
        });
    ::fixy::FixedArray<float, B * S * D> const ref_attn =
        ::fixy::build_fixed_array<float, B * S * D>([&](auto out) noexcept {
            cpu::sdpa(kTest, ref_Q.view(), make_input<roles::Keys>(ref_K.view()),
                      make_input<roles::Values>(ref_V.view()), std::move(out), make_dim<roles::Batch>(B),
                      make_dim<roles::Sequence>(S), make_dim<roles::Width>(D));
        });
    ::fixy::FixedArray<float, B * S * D> const ref_proj =
        ::fixy::build_fixed_array<float, B * S * D>([&](auto out) noexcept {
            square_mm(make_input<LeftFactor>(ref_attn.view()), make_input<RightFactor>(read_floats(weights.W_out)),
                      std::move(out));
        });
    ::fixy::FixedArray<float, B * S * D> const ref_res1 =
        ::fixy::build_fixed_array<float, B * S * D>([&](auto out) noexcept {
            cpu::add(kTest, make_input<roles::Augend>(ref_proj.view()),
                     make_input<roles::Addend>(read_floats(weights.X)), std::move(out),
                     make_dim<roles::Count>(B * S * D));
        });
    ::fixy::FixedArray<float, B * S * D> const ref_norm2 =
        ::fixy::build_fixed_array<float, B * S * D>([&](auto out) noexcept {
            cpu::layer_norm(kTest, ref_res1.view(), make_input<roles::Scale>(read_floats(weights.gamma2)),
                            make_input<roles::Shift>(read_floats(weights.beta2)), std::move(out),
                            make_dim<roles::Rows>(B * S), make_dim<roles::Width>(D));
        });
    ::fixy::FixedArray<float, B * S * D_FF> const ref_ff1 =
        ::fixy::build_fixed_array<float, B * S * D_FF>([&](auto out) noexcept {
            cpu::mm(kTest, make_input<LeftFactor>(ref_norm2.view()),
                    make_input<RightFactor>(read_floats(weights.W_ff1)), std::move(out), make_dim<roles::Rows>(B * S),
                    make_dim<roles::Columns>(D_FF), make_dim<roles::Inner>(D));
        });
    ::fixy::FixedArray<float, B * S * D_FF> const ref_relu =
        ::fixy::build_fixed_array<float, B * S * D_FF>([&ref_ff1](auto out) noexcept {
            cpu::relu(kTest, ref_ff1.view(), std::move(out), make_dim<roles::Count>(B * S * D_FF));
        });
    ::fixy::FixedArray<float, B * S * D> const ref_ff2 =
        ::fixy::build_fixed_array<float, B * S * D>([&](auto out) noexcept {
            cpu::mm(kTest, make_input<LeftFactor>(ref_relu.view()), make_input<RightFactor>(read_floats(weights.W_ff2)),
                    std::move(out), make_dim<roles::Rows>(B * S), make_dim<roles::Columns>(D),
                    make_dim<roles::Inner>(D_FF));
        });
    reference.res2 = ::fixy::build_fixed_array<float, B * S * D>([&](auto out) noexcept {
        cpu::add(kTest, make_input<roles::Augend>(ref_ff2.view()), make_input<roles::Addend>(ref_res1.view()),
                 std::move(out), make_dim<roles::Count>(B * S * D));
    });
    ::fixy::FixedArray<float, B * D> const ref_cls =
        ::fixy::build_fixed_array<float, B * D>([&reference](auto out) noexcept {
            cpu::index_select(kTest, reference.res2.view(), std::move(out), make_dim<roles::Batch>(B),
                              make_dim<roles::Sequence>(S), make_dim<roles::Width>(D), make_dim<roles::Selected>(0));
        });
    ::fixy::FixedArray<float, B * N_CLS> const ref_logits =
        ::fixy::build_fixed_array<float, B * N_CLS>([&](auto out) noexcept {
            cpu::mm(kTest, make_input<LeftFactor>(ref_cls.view()), make_input<RightFactor>(read_floats(weights.W_head)),
                    std::move(out), make_dim<roles::Rows>(B), make_dim<roles::Columns>(N_CLS),
                    make_dim<roles::Inner>(D));
        });
    reference.probs = ::fixy::build_fixed_array<float, B * N_CLS>([&ref_logits](auto out) noexcept {
        cpu::softmax(kTest, ref_logits.view(), std::move(out), make_dim<roles::Rows>(B), make_dim<roles::Width>(N_CLS));
    });
}

}  // namespace test_compute_vit
