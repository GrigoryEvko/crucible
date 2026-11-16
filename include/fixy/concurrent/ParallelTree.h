#pragma once

// The parallel forms of the tree reduce and the scans of foundation/core.
// Each one gives the bits of the form of one thread, for each count of
// shards, each count of threads and each schedule.
//
//   sum_by_tree_in_parallel<Coupling, Shards>(ctx, elements, partials)
//   sum_by_tree_in_parallel<Coupling, Shards>(ctx, alloc, elements)
//   reduce_by_tree_in_parallel<Coupling, Shards>(ctx, elements, partials, combine)
//   reduce_by_tree_in_parallel<Coupling, Shards>(ctx, alloc, elements, combine)
//   scan_sums_in_parallel<Coupling, Shards>(ctx, kind, in, out, carries)
//   scan_sums_in_parallel<Coupling, Shards>(ctx, alloc, kind, in, out)
//   sum_segments_by_tree_in_parallel<Coupling, Shards>(ctx, elements, offsets, sums)
//
// A form with a region takes its slots from the caller, for a caller that
// keeps one region for many calls.  A form with the allocation capability
// takes its slots from a HeapArray of its own, so the allocation shows at
// the call.
// THE SPLIT
//   The run divides into grains of parallel_tree_grain elements, and the
//   last grain holds the rest.  The caller gives a region of one slot for
//   each grain (count_parallel_tree_partials).  mint_parallel_for of
//   fixy/concurrent/Spawn.h cuts the region into Shards shards in proportion
//   to the count of the slots, and each shard computes the grains of its
//   slots.  A grain is a node of the tree (THE ORDER of
//   foundation/core/Reduce.h), so the cut of the shards and the thread of
//   each shard change no operand.  The calling thread then combines the
//   slots by the tree of the same rule, and the result has the bits of
//   sum_by_tree or reduce_by_tree of the run.
//
//   The coupling and the count of shards pass to mint_parallel_for.
//   Independent shards run on the threads that the parallelism rule gives
//   for the size of the run.  WaitOnEachOther gives each shard its own
//   thread, and a test uses it to run a stated count of threads.
//
// THE WRITES OF A SCAN
//   A scan takes three phases: the tree sum of each grain into its slot, the
//   exclusive scan of the slots on the calling thread, which makes each slot
//   the carry of its grain (THE GRAINS of foundation/core/Scan.h), and the
//   scan of each grain with its carry.  In the third phase each shard writes
//   the outputs of its grains.  Before the third phase, split_at of
//   foundation/core/Region.h divides the ViewMut out at the cut of the shards
//   of mint_parallel_for, so each shard writes through the ViewMut of its own
//   grains, and each output has one writer by its type.  A shard takes its
//   part by the index of its Slice, and it checks that its first slot is the
//   first slot of that cut, so a different cut ends the process.
//
// THE SCRATCH
//   A form with a region gives the region back, and the values of its slots
//   after the call have no meaning.  A region of a different count of slots
//   ends the process.  The sums of segments wrap the run of the ViewMut of the
//   sums in a region of their own, one slot for each segment, and need no
//   scratch.  Only a form with the allocation capability allocates.  A shard
//   of a region writes its slots through the raw run of the shard, with the
//   detail kernels of Reduce.h and Scan.h.  The raw run stays until a region
//   has a frame door that gives a ViewMut.
//
// THE TARGET
//   The sum forms call the SIMD kernels of foundation/core, so they are in
//   the inline namespace CRUCIBLE_SIMD_TARGET (THE TARGET NAMESPACE of
//   foundation/Platform.h).  The reduce forms with the combine of a caller
//   read no target, and they stay outside it.

#include <fixy/OwnedRegion.h>
#include <fixy/Reductions.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <fixy/concurrent/Spawn.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/core/Scan.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

// The grain of the parallel forms: 16384 elements, 64 KiB of floats.  A
// grain is a node of the tree, so the grain changes no bit, only the count of
// the slots and the work of each slot.
inline constexpr std::size_t parallel_tree_grain = std::size_t{1} << 14;

// The count of the slots of the region of a parallel form over count
// elements.
[[nodiscard]] constexpr std::size_t count_parallel_tree_partials(std::size_t count) noexcept {
    return ::foundation::core::count_tree_grains<parallel_tree_grain>(count);
}

// The result of a parallel tree sum: the sum, and the region of the slots.
template <typename T, typename Whole, typename Brand>
struct [[nodiscard]] TreeSumParts {
    T sum{};
    ::fixy::OwnedRegion<T, Whole, Brand> partials;
};

// The result of a parallel tree reduce: the tree of the run, or no value for
// an empty run, and the region of the slots.
template <typename T, typename Whole, typename Brand>
struct [[nodiscard]] TreeReduceParts {
    ::foundation::core::Option<T> value;
    ::fixy::OwnedRegion<T, Whole, Brand> partials;
};

namespace detail {

namespace eff = ::foundation::effects;

// A context of a parallel form: an execution context that owns Bg, which the
// threads of mint_parallel_for need.
template <typename Ctx>
concept CtxFitsParallelTree = eff::IsExecCtx<Ctx> && eff::CtxOwnsCapability<Ctx, eff::Effect::Bg>;

// The View of the elements of a region or of a shard.
template <typename Region>
[[nodiscard]] auto region_view_(Region& region) noexcept {
    return ::foundation::core::detail::view_over_(region.data(), region.size());
}

// The position of the first slot of a shard in the region that the split
// cut: the distance from the first slot of the region.
template <typename Shard, typename T>
[[nodiscard]] std::size_t first_slot_(Shard const& shard, T const* base) noexcept {
    return static_cast<std::size_t>(shard.data() - base);
}

// The region of the slots holds one slot for each grain of the run.
inline void check_slots_(std::size_t slots, std::size_t count) noexcept {
    if (slots != count_parallel_tree_partials(count)) [[unlikely]] {
        ::foundation::core::fatal("a parallel tree form of {} elements needs {} slots, and got {}", count,
                                  count_parallel_tree_partials(count), slots);
    }
}

// The bytes that a form reads and writes, for the parallelism rule.
template <typename T>
[[nodiscard]] constexpr WorkBudget budget_of_(std::size_t read, std::size_t written) noexcept {
    return WorkBudget{.read_bytes = ::foundation::core::saturating_mul(read, sizeof(T)),
                      .write_bytes = ::foundation::core::saturating_mul(written, sizeof(T)),
                      .item_count = read};
}

// The parts of out at the cut of the Shards shards of a region of grains
// slots: part s holds the outputs of the grains of shard s, by the cut of
// mint_split (fixy::detail::shard_start).  Complexity: O(Shards).
template <std::size_t Shards, typename T, typename Brand, std::size_t Extent>
[[nodiscard]] ::foundation::core::FixedArray<::foundation::core::Option<::foundation::core::ViewMut<T, Brand>>, Shards>
split_by_shards_(::foundation::core::ViewMut<T, Brand, Extent> out, std::size_t grains) noexcept {
    using Part = ::foundation::core::ViewMut<T, Brand>;
    ::foundation::core::FixedArray<::foundation::core::Option<Part>, Shards> parts{};
    std::size_t const count = out.size();
    Part rest{std::move(out)};
    std::size_t begin = 0;
    for (std::size_t shard = 0; shard < Shards; ++shard) {
        std::size_t const limit = ::fixy::detail::shard_start(grains, Shards, shard + 1) * parallel_tree_grain;
        std::size_t const end = limit < count ? limit : count;
        auto [own, next] = ::foundation::core::split_at(std::move(rest), end - begin)
                               .expect("the cut of a shard lies inside the outputs that are left");
        *parts.get(shard).expect("each shard has a part") = ::foundation::core::Option<Part>::some(std::move(own));
        rest = std::move(next);
        begin = end;
    }
    return parts;
}

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of the elements, by the grains of THE SPLIT, with the bits of
// foundation::core::sum_by_tree.  An empty run gives +0.  Complexity: O(n), in
// Shards shards, then O(slots) on the calling thread.
template <spawn::BodyCoupling Coupling, std::size_t Shards, typename Ctx, typename T, typename Whole, typename Brand>
    requires(Shards > 0) && detail::CtxFitsParallelTree<Ctx> && ::foundation::core::TreeSumElement<T>
[[nodiscard]] TreeSumParts<T, Whole, Brand>
sum_by_tree_in_parallel(Ctx const& ctx, ::foundation::core::View<T const> elements,
                        ::fixy::OwnedRegion<T, Whole, Brand>&& partials) noexcept {
    detail::check_slots_(partials.size(), elements.size());
    T const* const base = partials.data();
    auto whole = spawn::mint_parallel_for<Coupling, Shards>(
        ctx, detail::budget_of_<T>(elements.size(), partials.size()), std::move(partials),
        [elements, base](auto& shard) noexcept {
            if (shard.size() == 0) return;
            ::foundation::core::detail::sum_grains_by_tree_<parallel_tree_grain>(
                elements, detail::first_slot_(shard, base), shard.data(), shard.size());
        });
    T const sum = ::foundation::core::sum_by_tree(::foundation::core::View<T const>{detail::region_view_(whole)});
    return TreeSumParts<T, Whole, Brand>{sum, std::move(whole)};
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The tree of the elements with the combine of the caller, by the grains of
// THE SPLIT, with the value of foundation::core::reduce_by_tree.  Each shard
// takes a copy of the combine.  Complexity: O(n) combines, in Shards shards,
// then O(slots) on the calling thread.
template <spawn::BodyCoupling Coupling, std::size_t Shards, typename Ctx, typename T, typename Whole, typename Brand,
          typename Combine>
    requires(Shards > 0) && detail::CtxFitsParallelTree<Ctx> && ::foundation::core::TreeValue<T>
         && ::foundation::core::TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
         && std::is_nothrow_copy_constructible_v<Combine>
[[nodiscard]] TreeReduceParts<T, Whole, Brand>
reduce_by_tree_in_parallel(Ctx const& ctx, ::foundation::core::View<T const> elements,
                           ::fixy::OwnedRegion<T, Whole, Brand>&& partials, Combine combine) noexcept {
    detail::check_slots_(partials.size(), elements.size());
    T const* const base = partials.data();
    auto whole = spawn::mint_parallel_for<Coupling, Shards>(
        ctx, detail::budget_of_<T>(elements.size(), partials.size()), std::move(partials),
        [elements, base, combine](auto& shard) noexcept {
            if (shard.size() == 0) return;
            ::foundation::core::detail::reduce_grains_by_tree_<parallel_tree_grain>(
                elements, detail::first_slot_(shard, base), shard.data(), shard.size(), combine);
        });
    auto value =
        ::foundation::core::reduce_by_tree(::foundation::core::View<T const>{detail::region_view_(whole)}, combine);
    return TreeReduceParts<T, Whole, Brand>{std::move(value), std::move(whole)};
}

inline namespace CRUCIBLE_SIMD_TARGET {

// The sum scan of in into out, by the three phases of THE WRITES OF A SCAN,
// with the bits of scan_inclusive_sums or scan_exclusive_sums of
// foundation/core/Scan.h.  in and out hold one count, and in does not share
// an element with out.  Two counts that differ end the process.  Complexity:
// O(n) in Shards shards, two times, and O(slots) on the calling thread.
template <spawn::BodyCoupling Coupling, std::size_t Shards, typename Ctx, typename T, typename OutBrand,
          std::size_t OutExtent, typename Whole, typename Brand>
    requires(Shards > 0) && detail::CtxFitsParallelTree<Ctx> && ::foundation::core::TreeSumElement<T>
[[nodiscard]] ::fixy::OwnedRegion<T, Whole, Brand>
scan_sums_in_parallel(Ctx const& ctx, ::foundation::core::ScanKind kind, ::foundation::core::View<T const> in,
                      ::foundation::core::ViewMut<T, OutBrand, OutExtent> out,
                      ::fixy::OwnedRegion<T, Whole, Brand>&& carries) noexcept {
    if (in.size() != out.size()) [[unlikely]] {
        ::foundation::core::fatal("a parallel scan got an input of {} elements and an output of {}", in.size(),
                                  out.size());
    }
    detail::check_slots_(carries.size(), in.size());
    std::size_t const grains = carries.size();
    T const* const base = carries.data();
    WorkBudget const budget = detail::budget_of_<T>(in.size(), in.size());
    auto summed =
        spawn::mint_parallel_for<Coupling, Shards>(ctx, budget, std::move(carries), [in, base](auto& shard) noexcept {
            if (shard.size() == 0) return;
            ::foundation::core::detail::sum_grains_by_tree_<parallel_tree_grain>(in, detail::first_slot_(shard, base),
                                                                                 shard.data(), shard.size());
        });
    T* const slots = summed.data();
    ::foundation::core::detail::sum_scan_lanes_<::foundation::core::ScanKind::Exclusive, T,
                                                ::foundation::simd::native_lanes<T>>(slots, slots, grains,
                                                                                     static_cast<T const*>(nullptr));
    auto parts = detail::split_by_shards_<Shards>(std::move(out), grains);
    return spawn::mint_parallel_for<Coupling, Shards>(
        ctx, budget, std::move(summed), [in, base, kind, grains, &parts](auto& shard) noexcept {
            constexpr std::size_t index = std::remove_cvref_t<decltype(shard)>::tag_type::index;
            std::size_t const first = detail::first_slot_(shard, base);
            if (first != ::fixy::detail::shard_start(grains, Shards, index)) [[unlikely]] {
                ::foundation::core::fatal(
                    "shard {} of a parallel scan starts at slot {}, not at the slot {} of its part", index, first,
                    ::fixy::detail::shard_start(grains, Shards, index));
            }
            auto own = std::move(*parts.get(index).expect("each shard has a part"))
                           .expect("each shard takes its part one time");
            if (shard.size() == 0) return;
            ::foundation::core::View<T const> const shard_carries{detail::region_view_(shard)};
            if (kind == ::foundation::core::ScanKind::Inclusive) {
                ::foundation::core::scan_inclusive_sum_grains<parallel_tree_grain>(in, first, shard_carries,
                                                                                   std::move(own));
            } else {
                ::foundation::core::scan_exclusive_sum_grains<parallel_tree_grain>(in, first, shard_carries,
                                                                                   std::move(own));
            }
        });
}

}  // namespace CRUCIBLE_SIMD_TARGET

// ── The forms that allocate their slots ─────────────────────────────────

namespace detail {

// The tag of a region that a parallel form makes over storage of its own,
// for the length of one call.
struct ParallelTreeSlots {
    using permission_row = eff::Row<>;
};

// The first element of the slots, for the region of a form.  Each form makes
// its region in its own body with a fresh root permission, so the brand of
// the region stays in the call that made it.  The View borrows its storage
// for the call, and the region gives the split of mint_parallel_for and no
// other access.
template <typename T>
[[nodiscard]] T* first_slot_of_(::foundation::core::View<T> slots) noexcept {
    return first_of_(::foundation::core::detail::ViewDoor::key(), slots);
}

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of the elements, with the slots in a HeapArray that the
// allocation capability of the caller gives.  The bits are the bits of
// foundation::core::sum_by_tree.  Complexity: as the form with a region, and one
// allocation of count_parallel_tree_partials(n) slots.
template <spawn::BodyCoupling Coupling, std::size_t Shards, typename Ctx, typename T>
    requires(Shards > 0) && detail::CtxFitsParallelTree<Ctx> && ::foundation::core::TreeSumElement<T>
[[nodiscard]] T sum_by_tree_in_parallel(Ctx const& ctx, ::foundation::core::Alloc alloc,
                                        ::foundation::core::View<T const> elements) noexcept {
    ::foundation::core::HeapArray<T> storage =
        ::foundation::core::mint_heap_array(alloc, count_parallel_tree_partials(elements.size()), T{});
    auto parts = sum_by_tree_in_parallel<Coupling, Shards>(
        ctx, elements,
        ::fixy::mint_owned_region(detail::first_slot_of_(storage.view()), storage.size(),
                                  ::foundation::permissions::mint_permission_root<detail::ParallelTreeSlots>()));
    T const sum = parts.sum;
    ::foundation::linear::drop(std::move(parts.partials));
    return sum;
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The tree of the elements with the combine of the caller, with the slots in
// a HeapArray that the allocation capability gives.  An empty run gives no
// value.  The slot type is T, so it needs a value to fill the slots: the
// first element of the run.
template <spawn::BodyCoupling Coupling, std::size_t Shards, typename Ctx, typename T, typename Combine>
    requires(Shards > 0) && detail::CtxFitsParallelTree<Ctx> && ::foundation::core::TreeValue<T>
         && ::foundation::core::TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
         && std::is_nothrow_copy_constructible_v<Combine>
[[nodiscard]] ::foundation::core::Option<T> reduce_by_tree_in_parallel(Ctx const& ctx, ::foundation::core::Alloc alloc,
                                                                       ::foundation::core::View<T const> elements,
                                                                       Combine combine) noexcept {
    if (elements.size() == 0) return ::foundation::core::none;
    T const& fill = *elements.get(0).expect("the run has a first element");
    ::foundation::core::HeapArray<T> storage =
        ::foundation::core::mint_heap_array(alloc, count_parallel_tree_partials(elements.size()), fill);
    auto parts = reduce_by_tree_in_parallel<Coupling, Shards>(
        ctx, elements,
        ::fixy::mint_owned_region(detail::first_slot_of_(storage.view()), storage.size(),
                                  ::foundation::permissions::mint_permission_root<detail::ParallelTreeSlots>()),
        combine);
    ::foundation::core::Option<T> value = std::move(parts.value);
    ::foundation::linear::drop(std::move(parts.partials));
    return value;
}

inline namespace CRUCIBLE_SIMD_TARGET {

// The sum scan of in into out, with the carries in a HeapArray that the
// allocation capability gives.  The bits are the bits of scan_inclusive_sums
// or scan_exclusive_sums.
template <spawn::BodyCoupling Coupling, std::size_t Shards, typename Ctx, typename T, typename OutBrand,
          std::size_t OutExtent>
    requires(Shards > 0) && detail::CtxFitsParallelTree<Ctx> && ::foundation::core::TreeSumElement<T>
void scan_sums_in_parallel(Ctx const& ctx, ::foundation::core::Alloc alloc, ::foundation::core::ScanKind kind,
                           ::foundation::core::View<T const> in,
                           ::foundation::core::ViewMut<T, OutBrand, OutExtent> out) noexcept {
    ::foundation::core::HeapArray<T> storage =
        ::foundation::core::mint_heap_array(alloc, count_parallel_tree_partials(in.size()), T{});
    ::foundation::linear::drop(scan_sums_in_parallel<Coupling, Shards>(
        ctx, kind, in, std::move(out),
        ::fixy::mint_owned_region(detail::first_slot_of_(storage.view()), storage.size(),
                                  ::foundation::permissions::mint_permission_root<detail::ParallelTreeSlots>())));
}

// The tree sum of each segment, by shards of the segments: sums[s] is the
// tree sum of the elements [offsets[s], offsets[s + 1]), with the bits of
// sum_segments_by_tree.  The form wraps the run of the ViewMut of the sums in a
// region of its own, so the split of mint_parallel_for gives each shard its
// own sums.  The offsets must pass check_segment_offsets for the count of the
// sums, and offsets that fail it end the process before any write.
// Complexity: O(n + S) in Shards shards.
template <spawn::BodyCoupling Coupling, std::size_t Shards, typename Ctx, typename T, typename SumBrand,
          std::size_t SumExtent>
    requires(Shards > 0) && detail::CtxFitsParallelTree<Ctx> && ::foundation::core::TreeSumElement<T>
void sum_segments_by_tree_in_parallel(Ctx const& ctx, ::foundation::core::View<T const> elements,
                                      ::foundation::core::View<std::size_t const> offsets,
                                      ::foundation::core::ViewMut<T, SumBrand, SumExtent> sums) noexcept {
    if (::foundation::core::check_segment_offsets(elements.size(), offsets, sums.size()).is_err()) [[unlikely]] {
        ::foundation::core::fatal("a parallel segmented sum got offsets that fail check_segment_offsets for {} sums",
                                  sums.size());
    }
    T* const base = first_of_(::foundation::core::detail::ViewDoor::key(), sums);
    ::foundation::linear::drop(spawn::mint_parallel_for<Coupling, Shards>(
        ctx, detail::budget_of_<T>(elements.size(), sums.size()),
        ::fixy::mint_owned_region(base, sums.size(),
                                  ::foundation::permissions::mint_permission_root<detail::ParallelTreeSlots>()),
        [elements, offsets, base](auto& shard) noexcept {
            if (shard.size() == 0) return;
            std::size_t const first = detail::first_slot_(shard, static_cast<T const*>(base));
            ::foundation::core::View<std::size_t const> const own_offsets =
                offsets.window(first, shard.size() + 1).expect("the offsets of a shard fit the checked offsets");
            static_cast<void>(
                ::foundation::core::detail::sum_segments_by_tree_(elements, own_offsets, shard.data(), shard.size())
                    .expect("a window of checked offsets passes the check"));
        }));
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::concurrent
