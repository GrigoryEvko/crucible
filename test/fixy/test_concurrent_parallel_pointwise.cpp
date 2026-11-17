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

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/ParallelPointwise.h>
#include <fixy/os/Sched.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Pointwise.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace eff = ::foundation::effects;

using ::fixy::check::pass;
using ::fixy::spawn::BodyCoupling;

// The context of the parallel form: it owns Bg for the threads of the shards.
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

// The layout of a view of rank Rank in the run of a case.
template <std::size_t Rank>
struct Layout {
    ::fixy::FixedArray<std::size_t, Rank> sizes{};
    ::fixy::FixedArray<std::size_t, Rank> strides{};
    std::size_t offset = 0;
};

// The views of one walk: the output, the inputs, and the count of the
// elements of the run, which ends at the last element of a view.
template <std::size_t Rank, std::size_t Inputs>
struct Case {
    Layout<Rank> out{};
    ::fixy::FixedArray<Layout<Rank>, Inputs> inputs{};
    std::size_t total = 0;
};

// The unsigned word of the bits of a float or a double.
template <class E>
using Bits = std::conditional_t<sizeof(E) == 4, std::uint32_t, std::uint64_t>;

// The bits of the exponent of an infinity or a NaN.
template <class E>
[[nodiscard]] constexpr Bits<E> exponent_bits() noexcept {
    if constexpr (sizeof(E) == 4) {
        return 0x7f800000u;
    } else {
        return 0x7ff0000000000000u;
    }
}

// The element at a position of the run: a finite value, or at each thirteenth
// position a NaN whose payload and sign come from the position.
template <class E>
[[nodiscard]] E value_at(std::size_t position) noexcept {
    if (position % 13 == 5) {
        Bits<E> const exponent = exponent_bits<E>();
        Bits<E> const sign = position % 2 == 0 ? Bits<E>{1} << (8 * sizeof(E) - 1) : Bits<E>{0};
        return ::fixy::bit_cast<E>(static_cast<Bits<E>>(sign | exponent | static_cast<Bits<E>>(position + 1)));
    }
    return static_cast<E>(position % 1021) * static_cast<E>(0.25) + static_cast<E>(0.5);
}

// True when the two runs hold the same bits at each position.  O(n).
template <class E>
[[nodiscard]] bool same_bits(::fixy::View<E const> left, ::fixy::View<E const> right) noexcept {
    if (left.size() != right.size()) return false;
    auto right_cursor = right.begin();
    for (E const& element : left) {
        if (::fixy::bit_cast<Bits<E>>(element) != ::fixy::bit_cast<Bits<E>>(*right_cursor)) return false;
        ++right_cursor;
    }
    return true;
}

struct Add final {
    template <class X>
    constexpr X operator()(X left, X right) const noexcept {
        return static_cast<X>(left + right);
    }
};
struct MultiplyAdd final {
    template <class X>
    constexpr X operator()(X first, X second, X third) const noexcept {
        return static_cast<X>(first * second + third);
    }
};
struct Negate final {
    template <class X>
    constexpr X operator()(X value) const noexcept {
        return static_cast<X>(-value);
    }
};

// The two forms of the walk under test.
enum class Form : std::uint8_t {
    OneThread,
    Parallel,
};

// The run after a walk, and true when the walk gave no error.
template <class E>
struct Written {
    ::fixy::HeapArray<E> run;
    bool is_written = false;
};

// Writes the views of a case over run with the form, in a frame of the run.
// The inputs are plain views that the test makes before it mints the unique
// view of the output.  Each view has the static rank of the case.
template <Form Under, BodyCoupling Coupling, std::size_t Shards, class E, std::size_t Rank, class Op, std::size_t... Is>
[[nodiscard]] Written<E> write_case(BgCtx const& ctx, Case<Rank, sizeof...(Is)> const& drawn, Op op,
                                    ::fixy::HeapArray<E> run, std::index_sequence<Is...> /*inputs*/) noexcept {
    auto [is_written, written] = ::fixy::with_mut(std::move(run), [&](auto frame_run) noexcept {
        ::fixy::View<E const> const whole{frame_run.view()};
        auto const input_of = [&drawn, whole](std::size_t input) noexcept {
            Layout<Rank> const& layout = *drawn.inputs.get(input).expect("an input is below the count");
            return ::fixy::mint_strided_view(whole, layout.sizes.view(), layout.strides.view(), layout.offset)
                .expect("an input fits the run");
        };
        auto out = ::fixy::mint_strided_view_mut(std::move(frame_run), drawn.out.sizes.view(), drawn.out.strides.view(),
                                                 drawn.out.offset)
                       .expect("the output has no overlap");
        if constexpr (Under == Form::Parallel) {
            return ::fixy::concurrent::write_pointwise_in_parallel<Coupling, Shards>(ctx, out, op, input_of(Is)...)
                .is_ok();
        } else {
            static_cast<void>(ctx);
            return ::foundation::core::write_pointwise(out, op, input_of(Is)...).is_ok();
        }
    });
    return Written<E>{std::move(written), is_written};
}

// True when the parallel form with Shards shards writes the bits that
// write_pointwise writes on the calling thread, over the views of a case.
template <BodyCoupling Coupling, std::size_t Shards, class E, std::size_t Rank, std::size_t Inputs, class Op>
[[nodiscard]] bool parallel_agrees(BgCtx const& ctx, Case<Rank, Inputs> const& drawn, Op op) noexcept {
    auto const alloc = eff::testing::test().alloc;
    auto const fill = [](std::size_t position) noexcept { return value_at<E>(position); };
    using Sequence = std::make_index_sequence<Inputs>;
    Written<E> const expected = write_case<Form::OneThread, Coupling, Shards>(
        ctx, drawn, op, ::fixy::mint_heap_array_with<E>(alloc, drawn.total, fill), Sequence{});
    Written<E> const parallel = write_case<Form::Parallel, Coupling, Shards>(
        ctx, drawn, op, ::fixy::mint_heap_array_with<E>(alloc, drawn.total, fill), Sequence{});
    return expected.is_written && parallel.is_written
        && same_bits(::fixy::View<E const>{parallel.run.view()}, ::fixy::View<E const>{expected.run.view()});
}

// True when each count of shards agrees on the case.
template <class E, std::size_t Rank, std::size_t Inputs, class Op>
[[nodiscard]] bool each_split_agrees(BgCtx const& ctx, Case<Rank, Inputs> const& drawn, Op op) noexcept {
    return parallel_agrees<BodyCoupling::WaitOnEachOther, 1, E>(ctx, drawn, op)
        && parallel_agrees<BodyCoupling::WaitOnEachOther, 2, E>(ctx, drawn, op)
        && parallel_agrees<BodyCoupling::WaitOnEachOther, 3, E>(ctx, drawn, op)
        && parallel_agrees<BodyCoupling::WaitOnEachOther, 8, E>(ctx, drawn, op)
        && parallel_agrees<BodyCoupling::Independent, 4, E>(ctx, drawn, op);
}

using Plane = Layout<2>;

// The cases of two inputs over planes, for one element type.
template <class E>
void two_inputs(::fixy::TestRunnerCtx const& test, BgCtx const& ctx, ::fixy::TextView name) noexcept {
    ::fixy::FixedArray<Case<2, 2>, 10> const cases{
        // Dense views, and a first axis of fewer positions than shards.
        Case<2, 2>{Plane{.sizes = {7, 33}, .strides = {33, 1}, .offset = 0},
                   {Plane{.sizes = {7, 33}, .strides = {33, 1}, .offset = 231},
                    Plane{.sizes = {7, 33}, .strides = {33, 1}, .offset = 462}},
                   693},
        Case<2, 2>{Plane{.sizes = {3, 40}, .strides = {40, 1}, .offset = 0},
                   {Plane{.sizes = {3, 40}, .strides = {40, 1}, .offset = 120},
                    Plane{.sizes = {3, 40}, .strides = {40, 1}, .offset = 240}},
                   360},
        // A transposed input: the tile path, with whole tiles and edge tiles.
        Case<2, 2>{Plane{.sizes = {32, 48}, .strides = {48, 1}, .offset = 0},
                   {Plane{.sizes = {32, 48}, .strides = {1, 32}, .offset = 1536},
                    Plane{.sizes = {32, 48}, .strides = {48, 1}, .offset = 3072}},
                   4608},
        Case<2, 2>{Plane{.sizes = {19, 23}, .strides = {23, 1}, .offset = 0},
                   {Plane{.sizes = {19, 23}, .strides = {1, 19}, .offset = 437},
                    Plane{.sizes = {19, 23}, .strides = {23, 1}, .offset = 874}},
                   1311},
        // A broadcast row and a step-two input.
        Case<2, 2>{Plane{.sizes = {9, 33}, .strides = {33, 1}, .offset = 0},
                   {Plane{.sizes = {9, 33}, .strides = {33, 1}, .offset = 297},
                    Plane{.sizes = {9, 33}, .strides = {0, 1}, .offset = 594}},
                   627},
        Case<2, 2>{Plane{.sizes = {9, 17}, .strides = {17, 1}, .offset = 0},
                   {Plane{.sizes = {9, 17}, .strides = {34, 2}, .offset = 153},
                    Plane{.sizes = {9, 17}, .strides = {17, 1}, .offset = 459}},
                   612},
        // An input in place.
        Case<2, 2>{Plane{.sizes = {11, 21}, .strides = {21, 1}, .offset = 0},
                   {Plane{.sizes = {11, 21}, .strides = {21, 1}, .offset = 0},
                    Plane{.sizes = {11, 21}, .strides = {21, 1}, .offset = 231}},
                   462},
        // An input in place over a transposed output: the positions of
        // adjacent shards alternate in memory, so two threads write the
        // elements of one cache line.
        Case<2, 2>{Plane{.sizes = {12, 10}, .strides = {1, 12}, .offset = 0},
                   {Plane{.sizes = {12, 10}, .strides = {1, 12}, .offset = 0},
                    Plane{.sizes = {12, 10}, .strides = {10, 1}, .offset = 120}},
                   240},
        // An input that shares elements with the output one element later:
        // the stated order on the calling thread.
        Case<2, 2>{Plane{.sizes = {6, 20}, .strides = {20, 1}, .offset = 0},
                   {Plane{.sizes = {6, 20}, .strides = {20, 1}, .offset = 1},
                    Plane{.sizes = {6, 20}, .strides = {20, 1}, .offset = 121}},
                   241},
        // A transposed output: each view takes the General loop.
        Case<2, 2>{Plane{.sizes = {12, 10}, .strides = {1, 12}, .offset = 0},
                   {Plane{.sizes = {12, 10}, .strides = {10, 1}, .offset = 120},
                    Plane{.sizes = {12, 10}, .strides = {10, 1}, .offset = 240}},
                   360},
    };
    for (Case<2, 2> const& drawn : cases) {
        CRUCIBLE_TEST_ASSERT(test, each_split_agrees<E>(ctx, drawn, Add{}));
    }
    pass(test, "  two_inputs {}: PASSED\n", name);
}

// The cases of other ranks and other counts of inputs.
void other_shapes(::fixy::TestRunnerCtx const& test, BgCtx const& ctx) noexcept {
    using Line = Layout<1>;
    using Sizes1 = ::fixy::FixedArray<std::size_t, 1>;
    // Rank one: a step-three input and a dense one, 100 positions.
    Case<1, 2> const line{
        Line{Sizes1{100}, Sizes1{1}, 0}, {Line{Sizes1{100}, Sizes1{3}, 100}, Line{Sizes1{100}, Sizes1{1}, 398}}, 498};
    CRUCIBLE_TEST_ASSERT(test, each_split_agrees<float>(ctx, line, Add{}));
    // Rank one with an input in place: adjacent shards meet inside a vector
    // of lanes.
    Case<1, 2> const line_in_place{
        Line{Sizes1{100}, Sizes1{1}, 0}, {Line{Sizes1{100}, Sizes1{1}, 0}, Line{Sizes1{100}, Sizes1{3}, 100}}, 398};
    CRUCIBLE_TEST_ASSERT(test, each_split_agrees<float>(ctx, line_in_place, Add{}));
    // Rank three: an input with its axes in a different order.
    using Cube = Layout<3>;
    using Sizes3 = ::fixy::FixedArray<std::size_t, 3>;
    Case<3, 2> const cube{Cube{Sizes3{5, 4, 9}, Sizes3{36, 9, 1}, 0},
                          {Cube{Sizes3{5, 4, 9}, Sizes3{1, 45, 5}, 180}, Cube{Sizes3{5, 4, 9}, Sizes3{36, 9, 1}, 360}},
                          540};
    CRUCIBLE_TEST_ASSERT(test, each_split_agrees<float>(ctx, cube, Add{}));
    // Three inputs, one of them a broadcast row.
    Case<2, 3> const three{Plane{.sizes = {10, 24}, .strides = {24, 1}, .offset = 0},
                           {Plane{.sizes = {10, 24}, .strides = {24, 1}, .offset = 240},
                            Plane{.sizes = {10, 24}, .strides = {0, 1}, .offset = 480},
                            Plane{.sizes = {10, 24}, .strides = {24, 1}, .offset = 504}},
                           744};
    CRUCIBLE_TEST_ASSERT(test, each_split_agrees<float>(ctx, three, MultiplyAdd{}));
    CRUCIBLE_TEST_ASSERT(test, each_split_agrees<double>(ctx, three, MultiplyAdd{}));
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
    two_inputs<double>(test, ctx, "double");
    other_shapes(test, ctx);
    past_private_cache(test, ctx);
    shape_mismatch(test, ctx);
    placed_shards(test, ctx);
    ::fixy::report(::fixy::Sink::Out, "test_concurrent_parallel_pointwise: all tests passed\n");
    return 0;
}
