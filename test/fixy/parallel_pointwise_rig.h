#pragma once

// The rig of test_concurrent_parallel_pointwise: the cases of one walk in one
// run, the two forms of the walk under test, and the check that each count
// of shards gives the bits of write_pointwise on the calling thread.
//
// The compile of an op through write_pointwise gives one lane plan for each
// class of each input, so each element type and op costs much machine code.
// Thus the test compiles in more than one unit.  Each unit
// parallel_pointwise_unit_*.cpp gives functions at the end of this header,
// which run each_split_agrees or two_inputs for one element type, rank and
// op, and no two units compile the same walk.  The unit
// test_concurrent_parallel_pointwise.cpp holds main and each other case.

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

namespace test_parallel_pointwise {

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

// ── The cases of the units ──────────────────────────────────────────────

// parallel_pointwise_unit_double.cpp: the cases of two inputs over planes of
// doubles.
void test_the_double_two_inputs(::fixy::TestRunnerCtx const& test, BgCtx const& ctx) noexcept;
// parallel_pointwise_unit_other_ranks.cpp: each split of the add of floats
// over views of rank one and of rank three.
[[nodiscard]] bool each_float_split_agrees(BgCtx const& ctx, Case<1, 2> const& drawn, Add op) noexcept;
[[nodiscard]] bool each_float_split_agrees(BgCtx const& ctx, Case<3, 2> const& drawn, Add op) noexcept;
// parallel_pointwise_unit_multiply_add_float.cpp: each split of the
// multiply-add of floats.
[[nodiscard]] bool each_float_split_agrees(BgCtx const& ctx, Case<2, 3> const& drawn, MultiplyAdd op) noexcept;
// parallel_pointwise_unit_multiply_add_double.cpp: each split of the
// multiply-add of doubles.
[[nodiscard]] bool each_double_split_agrees(BgCtx const& ctx, Case<2, 3> const& drawn, MultiplyAdd op) noexcept;

}  // namespace test_parallel_pointwise
