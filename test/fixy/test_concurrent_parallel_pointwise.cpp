// Tests of the parallel form of the pointwise walk
// (fixy/concurrent/ParallelPointwise.h).
//
// Each case puts the output and the inputs of one walk in one run, with NaNs
// of different payloads and signs among finite values.  The test writes one
// copy of the run with write_pointwise on the calling thread, and a second
// copy with the parallel form, and compares the two runs bit for bit.
// WaitOnEachOther gives each shard its own thread, so the shards run at the
// same time, for 1, 2, 3 and 8 shards.  Independent shards take the threads
// of the parallelism rule, which gives more than one thread only to the walk
// of past_private_cache.  The cases hold a first axis with fewer positions
// than shards, a transposed input of the tile path, a broadcast input, a
// step-two input, an input in place over a dense and over a transposed
// output, ranks one and three, and an input that shares elements with the
// output in a different place, which keeps the stated order on the calling
// thread.  The run ends at the last element of
// its last view, so a read past a view is a read past the allocation, which
// the address sanitizer of a Debug build finds.
//
// This unit holds main and each case that the units of
// parallel_pointwise_rig.h do not hold.

#include "parallel_pointwise_rig.h"

#include <cstddef>
#include <utility>

namespace {

namespace eff = ::foundation::effects;

using ::test_parallel_pointwise::Add;
using ::test_parallel_pointwise::BgCtx;
using ::test_parallel_pointwise::BodyCoupling;
using ::test_parallel_pointwise::Case;
using ::test_parallel_pointwise::each_double_split_agrees;
using ::test_parallel_pointwise::each_float_split_agrees;
using ::test_parallel_pointwise::each_split_agrees;
using ::test_parallel_pointwise::Form;
using ::test_parallel_pointwise::Layout;
using ::test_parallel_pointwise::MultiplyAdd;
using ::test_parallel_pointwise::Negate;
using ::test_parallel_pointwise::pass;
using ::test_parallel_pointwise::Plane;
using ::test_parallel_pointwise::same_bits;
using ::test_parallel_pointwise::two_inputs;
using ::test_parallel_pointwise::value_at;
using ::test_parallel_pointwise::write_case;
using ::test_parallel_pointwise::Written;

// The cases of other ranks and other counts of inputs.
void other_shapes(::fixy::TestRunnerCtx const& test, BgCtx const& ctx) noexcept {
    using Line = Layout<1>;
    using Sizes1 = ::fixy::FixedArray<std::size_t, 1>;
    // Rank one: a step-three input and a dense one, 100 positions.
    Case<1, 2> const line{
        Line{Sizes1{100}, Sizes1{1}, 0}, {Line{Sizes1{100}, Sizes1{3}, 100}, Line{Sizes1{100}, Sizes1{1}, 398}}, 498};
    CRUCIBLE_TEST_ASSERT(test, each_float_split_agrees(ctx, line, Add{}));
    // Rank one with an input in place: adjacent shards meet inside a vector
    // of lanes.
    Case<1, 2> const line_in_place{
        Line{Sizes1{100}, Sizes1{1}, 0}, {Line{Sizes1{100}, Sizes1{1}, 0}, Line{Sizes1{100}, Sizes1{3}, 100}}, 398};
    CRUCIBLE_TEST_ASSERT(test, each_float_split_agrees(ctx, line_in_place, Add{}));
    // Rank three: an input with its axes in a different order.
    using Cube = Layout<3>;
    using Sizes3 = ::fixy::FixedArray<std::size_t, 3>;
    Case<3, 2> const cube{Cube{Sizes3{5, 4, 9}, Sizes3{36, 9, 1}, 0},
                          {Cube{Sizes3{5, 4, 9}, Sizes3{1, 45, 5}, 180}, Cube{Sizes3{5, 4, 9}, Sizes3{36, 9, 1}, 360}},
                          540};
    CRUCIBLE_TEST_ASSERT(test, each_float_split_agrees(ctx, cube, Add{}));
    // Three inputs, one of them a broadcast row.
    Case<2, 3> const three{Plane{.sizes = {10, 24}, .strides = {24, 1}, .offset = 0},
                           {Plane{.sizes = {10, 24}, .strides = {24, 1}, .offset = 240},
                            Plane{.sizes = {10, 24}, .strides = {0, 1}, .offset = 480},
                            Plane{.sizes = {10, 24}, .strides = {24, 1}, .offset = 504}},
                           744};
    CRUCIBLE_TEST_ASSERT(test, each_float_split_agrees(ctx, three, MultiplyAdd{}));
    CRUCIBLE_TEST_ASSERT(test, each_double_split_agrees(ctx, three, MultiplyAdd{}));
    // One input, transposed.
    Case<2, 1> const one{Plane{.sizes = {16, 16}, .strides = {16, 1}, .offset = 0},
                         ::fixy::FixedArray<Plane, 1>{Plane{.sizes = {16, 16}, .strides = {1, 16}, .offset = 256}},
                         512};
    CRUCIBLE_TEST_ASSERT(test, each_split_agrees<float>(ctx, one, Negate{}));
    pass(test, "  other_shapes: PASSED\n");
}

// A walk of 4.5 MiB, more than the private L2 of a core of each host of the
// project.  The parallelism rule then gives the Independent shards more than
// one thread, so the four shards run on threads of their own and cut the
// tiles of the transposed input.
void past_private_cache(::fixy::TestRunnerCtx const& test, BgCtx const& ctx) noexcept {
    Case<2, 2> const wide{Plane{.sizes = {512, 768}, .strides = {768, 1}, .offset = 0},
                          {Plane{.sizes = {512, 768}, .strides = {1, 512}, .offset = 393216},
                           Plane{.sizes = {512, 768}, .strides = {768, 1}, .offset = 786432}},
                          1179648};
    CRUCIBLE_TEST_ASSERT(test, each_split_agrees<float>(ctx, wide, Add{}));
    pass(test, "  past_private_cache: PASSED\n");
}

// An input of other sizes gives ShapeMismatch, and the form writes nothing.
void shape_mismatch(::fixy::TestRunnerCtx const& test, BgCtx const& ctx) noexcept {
    auto const alloc = eff::testing::test().alloc;
    auto const fill = [](std::size_t position) noexcept { return value_at<float>(position); };
    ::fixy::HeapArray<float> run = ::fixy::mint_heap_array_with<float>(alloc, 44, fill);
    ::fixy::HeapArray<float> const before = ::fixy::mint_heap_array_with<float>(alloc, 44, fill);
    Plane const out_layout{.sizes = {4, 5}, .strides = {5, 1}, .offset = 0};
    Plane const in_layout{.sizes = {4, 6}, .strides = {6, 1}, .offset = 20};
    auto [is_refused, after] = ::fixy::with_mut(std::move(run), [&](auto frame_run) noexcept {
        ::fixy::View<float const> const whole{frame_run.view()};
        auto const input =
            ::fixy::mint_strided_view(whole, in_layout.sizes.view(), in_layout.strides.view(), in_layout.offset)
                .expect("the input fits the run");
        auto out = ::fixy::mint_strided_view_mut(std::move(frame_run), out_layout.sizes.view(),
                                                 out_layout.strides.view(), out_layout.offset)
                       .expect("the output has no overlap");
        return ::fixy::concurrent::write_pointwise_in_parallel<BodyCoupling::WaitOnEachOther, 4>(ctx, out, Negate{},
                                                                                                 input)
            .is_err();
    });
    CRUCIBLE_TEST_ASSERT(test, is_refused);
    CRUCIBLE_TEST_ASSERT(test,
                         same_bits(::fixy::View<float const>{after.view()}, ::fixy::View<float const>{before.view()}));
    pass(test, "  shape_mismatch: PASSED\n");
}

// The form with the options of the threads: four shards that wait on each
// other, each on its own CPU of the affinity of the test under the placement
// OneCpuEach, write the bits of write_pointwise on the calling thread.
void placed_shards(::fixy::TestRunnerCtx const& test, BgCtx const& ctx) noexcept {
    auto const alloc = eff::testing::test().alloc;
    auto const fill = [](std::size_t position) noexcept { return value_at<float>(position); };
    Case<2, 2> const dense{Plane{.sizes = {64, 96}, .strides = {96, 1}, .offset = 0},
                           {Plane{.sizes = {64, 96}, .strides = {1, 64}, .offset = 6144},
                            Plane{.sizes = {64, 96}, .strides = {96, 1}, .offset = 12288}},
                           18432};
    Written<float> const expected = write_case<Form::OneThread, BodyCoupling::WaitOnEachOther, 4>(
        ctx, dense, Add{}, ::fixy::mint_heap_array_with<float>(alloc, dense.total, fill),
        std::make_index_sequence<2>{});
    ::fixy::thread::ThreadOptions const options{
        .affinity = ::fixy::sched::read_affinity(ctx).value_or(::fixy::sched::AffinityMask{}),
        .placement = ::fixy::thread::ThreadPlacement::OneCpuEach};
    auto [is_written, placed] =
        ::fixy::with_mut(::fixy::mint_heap_array_with<float>(alloc, dense.total, fill), [&](auto frame_run) noexcept {
            ::fixy::View<float const> const whole{frame_run.view()};
            auto const input_of = [&dense, whole](std::size_t input) noexcept {
                Layout<2> const& layout = *dense.inputs.get(input).expect("an input is below the count");
                return ::fixy::mint_strided_view(whole, layout.sizes.view(), layout.strides.view(), layout.offset)
                    .expect("an input fits the run");
            };
            auto out = ::fixy::mint_strided_view_mut(std::move(frame_run), dense.out.sizes.view(),
                                                     dense.out.strides.view(), dense.out.offset)
                           .expect("the output has no overlap");
            return ::fixy::concurrent::write_pointwise_in_parallel_with_thread_options<BodyCoupling::WaitOnEachOther,
                                                                                       4>(
                       ctx, test, options, out, Add{}, input_of(0), input_of(1))
                .is_ok();
        });
    CRUCIBLE_TEST_ASSERT(test, expected.is_written && is_written);
    CRUCIBLE_TEST_ASSERT(
        test, same_bits(::fixy::View<float const>{placed.view()}, ::fixy::View<float const>{expected.run.view()}));
    pass(test, "  placed_shards: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    BgCtx const ctx{eff::testing::bg()};
    two_inputs<float>(test, ctx, "float");
    ::test_parallel_pointwise::test_the_double_two_inputs(test, ctx);
    other_shapes(test, ctx);
    past_private_cache(test, ctx);
    shape_mismatch(test, ctx);
    placed_shards(test, ctx);
    ::fixy::report(::fixy::Sink::Out, "test_concurrent_parallel_pointwise: all tests passed\n");
    return 0;
}
