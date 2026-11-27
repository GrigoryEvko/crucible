#pragma once

// The shared parts of the fixtures of mint_parallel_for that must not compile:
// the context, the tag of the region of the shards, and the runs.

#include <fixy/Core.h>
#include <fixy/concurrent/Spawn.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace parallel_loop_rig {

namespace eff = ::foundation::effects;

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using Word = std::uint64_t;

// The tag of the region that the shards write.
struct Written {
    using permission_row = eff::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
};

inline constexpr std::size_t kCount = 64;
inline constexpr std::size_t kShards = 2;

[[nodiscard]] inline ::fixy::HeapArray<Word> make_items() noexcept {
    return ::fixy::mint_heap_array(eff::testing::test().alloc, kCount, Word{1});
}

// Moves items into a region of Written, and gives body the region and the
// borrow.  The closure of the door captures the borrow, so the shards can
// write the items while the closure reads the borrow.  Thus the plugin refuses
// a call of this function with a borrow of the items (the ends beside a borrow
// of the plugin).  It also refuses a call with a closure that holds a borrow
// of the items through its captures (the borrows through a capture).  A fixture
// with such a borrow gives that error beside its own error.
template <class Borrow, class Body>
void give_region_and_borrow(::fixy::HeapArray<Word>& items, Borrow const& borrow, Body body) noexcept {
    items = ::fixy::with_region<Written>(
        std::move(items), [&borrow, body](auto region) noexcept { return body(std::move(region), borrow); });
}

// The loop over the region with the inputs and the body.
template <class Region, class Body, class... Inputs>
[[nodiscard]] Region loop_over(Region region, Body body, Inputs const&... inputs) noexcept {
    BgCtx const ctx{eff::testing::bg()};
    return ::fixy::spawn::mint_parallel_for<::fixy::spawn::BodyCoupling::Independent, kShards>(
        ctx, ::fixy::concurrent::WorkBudget{.read_bytes = 64}, std::move(region), ::fixy::spawn::reads(inputs...),
        std::move(body));
}

}  // namespace parallel_loop_rig
