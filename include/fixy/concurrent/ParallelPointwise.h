#pragma once

// The parallel form of the pointwise walk of foundation/core/Pointwise.h.  The
// result has the bits of write_pointwise for each count of shards, each count
// of threads and each schedule.
//
//   write_pointwise_in_parallel<Coupling, Shards>(ctx, out, op, inputs...)
//                       writes op(input...) into the element of out at each
//                       index, as write_pointwise does, or gives ShapeMismatch
//                       and writes nothing.  out is a StridedViewMut of a
//                       static rank, which the form borrows for the call
//   write_pointwise_in_parallel_with_thread_options<Coupling, Shards>(
//       ctx, test, options, out, op, inputs...)
//                       the same walk with the options of the threads of the
//                       shards, for a test or a bench, such as the placement
//                       OneCpuEach that gives each shard its own core
//
// THE SPLIT
//   Shard s writes the positions [start(s), start(s + 1)) of the first axis
//   of out, by the cut of mint_split (fixy::detail::shard_start), and it
//   reads the same positions of each input.  out is a unique view, so no two
//   indexes of out name one element, and no two shards write one element.
//   Each shard runs the body of write_pointwise over its positions.  The
//   walk computes each element alone from the input elements at its index
//   (THE LANES of Pointwise.h), so the cut changes no bit.  A first axis of
//   fewer positions than Shards leaves some shards with no position.
//
// THE ALIAS PROOF
//   The form takes the plan of write_pointwise over the whole views one time,
//   on the calling thread.  An input that can name an element of out at a
//   different index makes the walk keep the stated order of walk(), in which
//   each visit reads the writes of the visits before it.  Such a walk does
//   not split, and the form calls write_pointwise on the calling thread.  An
//   input in place reads in each shard only the elements that the shard
//   writes, and an input apart reads no element that a shard writes.  A rank
//   of zero, a view with no element and a first axis of one position do not
//   split either.
//
// THE THREADS
//   The coupling and the count of shards pass to mint_parallel_for of
//   fixy/concurrent/Spawn.h.  Independent shards run on the threads that the
//   parallelism rule gives for the bytes of the walk.  When the rule gives
//   one thread, such as for a walk in the private cache of one core, the
//   form calls write_pointwise: one plan, and no cut of the tiles.
//   WaitOnEachOther gives each shard its own thread, and a test uses it to
//   run a stated count of threads.  The form starts the threads of each
//   call, so a walk of few elements costs more than the walk of one thread.
//
// THE TARGET
//   The form calls the lane path of the walk, so it is in the inline namespace
//   CRUCIBLE_SIMD_TARGET (THE TARGET NAMESPACE of foundation/Platform.h).

#include <fixy/OwnedRegion.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <fixy/concurrent/Spawn.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Array.h>
#include <foundation/core/Index.h>
#include <foundation/core/Pointwise.h>
#include <foundation/core/Strided.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

namespace detail {

// A context of the parallel walk: an execution context that owns Bg, which
// the threads of mint_parallel_for need.
template <typename Ctx>
concept CtxFitsParallelPointwise = ::foundation::effects::IsExecCtx<Ctx>
                                && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

// The view of const elements of the plain view of an input, which each shard
// copies and only reads.
template <class T, std::size_t Rank>
[[nodiscard]] constexpr ::foundation::core::StridedView<T const, Rank>
read_only_(::foundation::core::StridedView<T, Rank> const& view) noexcept {
    return ::foundation::core::StridedView<T const, Rank>{view};
}

// The tag of the region of the shards of one walk: one slot for each shard,
// which holds the rows of out of the shard on the first axis, or no rows.
struct ParallelPointwiseShards {
    using permission_row = ::foundation::effects::Row<>;
};

// The bytes that a walk over the elements of out reads and writes, for the
// parallelism rule.  A broadcast input reads fewer bytes, so the sum is an
// upper bound.
template <class... Inputs, class T, std::size_t Rank>
[[nodiscard]] constexpr WorkBudget pointwise_budget_(::foundation::core::StridedView<T, Rank> const& out) noexcept {
    std::size_t const count = out.count();
    std::size_t read = 0;
    ((read = ::foundation::core::saturating_add(
          read, ::foundation::core::saturating_mul(count, sizeof(::foundation::core::pointwise_element_t<Inputs>)))),
     ...);
    return WorkBudget{
        .read_bytes = read, .write_bytes = ::foundation::core::saturating_mul(count, sizeof(T)), .item_count = count};
}

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// The body of the two forms below.  launch runs the shards: it takes the
// budget, the region of the shards and the body of a shard, and it calls
// mint_parallel_for, or the door of a test with the options of the threads.
template <spawn::BodyCoupling Coupling, std::size_t Shards, class Launch, class T, std::size_t Rank, class Brand,
          class Op, class... Inputs>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::core::ShapeMismatch>
write_pointwise_in_parallel_body_(Launch const& launch, ::foundation::core::StridedViewMut<T, Rank, Brand>& out, Op op,
                                  Inputs const&... inputs) noexcept {
    namespace core = ::foundation::core;
    if constexpr (Rank == 0) {
        static_cast<void>(launch);
        return core::write_pointwise(out, op, inputs...);
    } else {
        using Sequence = std::index_sequence_for<Inputs...>;
        core::Index<Rank> const first_axis{core::index<0>()};
        core::StridedView<T, Rank> const whole = core::detail::plain_view_of_(out);
        core::detail::StridedLanes<Rank> const& shape = core::detail::strided_lanes_of_(whole);
        bool const is_same_shape =
            (core::detail::same_sizes_(shape, core::detail::strided_lanes_of_(core::detail::plain_view_of_(inputs)))
             && ...);
        std::size_t const positions = whole.size(first_axis);
        WorkBudget const budget = detail::pointwise_budget_<Inputs...>(whole);
        // A walk that a sequential decision keeps on the calling thread
        // takes write_pointwise: one plan, and the tiles of one cut.
        bool const is_one_thread =
            spawn::choose_parallel_for_threads(Coupling, ParallelismRule::host_facts(), budget, Shards) < 2;
        if (!is_same_shape || whole.count() == 0 || positions < 2 || is_one_thread) {
            return core::write_pointwise(out, op, inputs...);
        }
        bool const apart[1 + sizeof...(Inputs)] = {false, core::detail::is_branded_apart_<Inputs, Brand>()...};
        if (core::detail::plan_pointwise_(Sequence{}, apart, whole, core::detail::plain_view_of_(inputs)...)
                .is_shared) {
            return core::write_pointwise(out, op, inputs...);
        }

        // The rows of out are a region of one part for each shard, so the
        // split of the door gives shard s the rows s and no other.  A shard
        // with no positions gets no rows.  No body holds the whole view of
        // out, which writes.  Each body holds a copy of a view of const
        // elements of each input, because an input can be a view that writes.
        using Rows = core::StridedView<T, Rank>;
        core::FixedArray<core::Option<Rows>, Shards> parts{};
        for (std::size_t shard = 0; shard < Shards; ++shard) {
            std::size_t const first = ::fixy::detail::shard_start(positions, Shards, shard);
            std::size_t const count = ::fixy::detail::shard_start(positions, Shards, shard + 1) - first;
            if (count == 0) continue;
            core::Option<Rows> rows = core::Option<Rows>::some(
                whole.slice(first_axis, first, count, 1).expect("the positions of a shard lie inside out"));
            parts.with_element(core::Index<Shards>::check(shard).expect("each shard has a part"),
                               [&rows](core::Option<Rows>& part) noexcept { part = std::move(rows); });
        }
        auto const ruled = core::detail::ruled_op_<T>(op);
        ::foundation::linear::drop(launch(
            budget,
            ::fixy::mint_owned_region(
                first_of_(core::detail::BaseLayerRead{}, frame_run_(core::detail::FrameDoor::key(), parts)), Shards,
                ::foundation::permissions::mint_permission_root<detail::ParallelPointwiseShards>()),
            [&ruled, &apart, ... reads = detail::read_only_(core::detail::plain_view_of_(inputs)), first_axis,
             positions](auto shard, auto place) noexcept {
                std::size_t const index = place.index().raw();
                CRUCIBLE_FATAL_INVARIANT(shard.size() == 1);
                core::Option<Rows> own = std::move(*first_of_(core::detail::BaseLayerRead{}, shard));
                if (own.is_none()) return;
                std::size_t const first = ::fixy::detail::shard_start(positions, Shards, index);
                std::size_t const count = ::fixy::detail::shard_start(positions, Shards, index + 1) - first;
                Rows const rows = std::move(own).expect("a shard with positions has its rows");
                bool const is_written =
                    core::detail::write_pointwise_(
                        ruled, Sequence{}, apart, rows,
                        reads.slice(first_axis, first, count, 1)
                            .expect("an input has the sizes of out, so the positions of a shard lie inside it")...)
                        .is_ok();
                if (!is_written) [[unlikely]] {
                    core::fatal("shard {} of a parallel pointwise walk got rows of different sizes", index);
                }
            }));
        return core::Unit{};
    }
}

// Writes op(input...) into the element of out at each index, by THE SPLIT,
// with the bits of write_pointwise.  It gives ShapeMismatch, and writes
// nothing, when the sizes of an input differ from the sizes of out.
// Complexity: O(count) calls of the op in Shards shards, O(rank^2 * views)
// for the plan on the calling thread and in each shard, and O(Shards) for the
// region of the shards.
template <spawn::BodyCoupling Coupling, std::size_t Shards, typename Ctx, class T, std::size_t Rank, class Brand,
          class Op, class... Inputs>
    requires(Shards > 0) && (Rank != ::foundation::core::dynamic_rank)
         && detail::CtxFitsParallelPointwise<Ctx> && ::foundation::core::CanWritePointwise<Op, T, Rank, Inputs...>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::core::ShapeMismatch>
write_pointwise_in_parallel(Ctx const& ctx, ::foundation::core::StridedViewMut<T, Rank, Brand>& out, Op op,
                            Inputs const&... inputs) noexcept {
    auto const launch = [&ctx](WorkBudget budget, auto&& region, auto body) noexcept {
        return spawn::mint_parallel_for<Coupling, Shards>(ctx, budget, std::move(region), std::move(body));
    };
    return write_pointwise_in_parallel_body_<Coupling, Shards>(launch, out, op, inputs...);
}

// write_pointwise_in_parallel with the options of the threads of the shards,
// for a test or a bench.  The placement OneCpuEach of fixy/os/ThreadTasks.h
// gives each shard its own CPU of the mask of the options, so the shards of a
// bench on isolated cores run on one core each.  The test context is the
// authority of the options, as for mint_parallel_for_with_thread_options of
// fixy/concurrent/Spawn.h, so code that ships cannot give them.  The bits of
// out are the bits of write_pointwise for each option.
template <spawn::BodyCoupling Coupling, std::size_t Shards, typename Ctx, typename TestCtx, class T, std::size_t Rank,
          class Brand, class Op, class... Inputs>
    requires(Shards > 0) && (Rank != ::foundation::core::dynamic_rank) && detail::CtxFitsParallelPointwise<Ctx>
         && ::foundation::effects::CtxOwnsCapability<TestCtx, ::foundation::effects::Effect::Test>
         && ::foundation::core::CanWritePointwise<Op, T, Rank, Inputs...>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::core::ShapeMismatch>
write_pointwise_in_parallel_with_thread_options(Ctx const& ctx, TestCtx const& test,
                                                ::fixy::thread::ThreadOptions const& options,
                                                ::foundation::core::StridedViewMut<T, Rank, Brand>& out, Op op,
                                                Inputs const&... inputs) noexcept {
    auto const launch = [&ctx, &test, &options](WorkBudget budget, auto&& region, auto body) noexcept {
        return spawn::mint_parallel_for_with_thread_options<Coupling, Shards>(ctx, test, budget, std::move(region),
                                                                              std::move(body), options);
    };
    return write_pointwise_in_parallel_body_<Coupling, Shards>(launch, out, op, inputs...);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::concurrent
