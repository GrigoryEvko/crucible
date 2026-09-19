#pragma once

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/PermissionTreeGenerator.h>
#include <crucible/safety/Workload.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::fixy::spawn {

using ::crucible::safety::OwnedRegion;
using ::crucible::safety::Permission;
using ::crucible::safety::Slice;

namespace detail {

template <typename Ctx, typename Parent, typename ChildrenTuple, typename CallablesTuple>
struct ctx_fits_spawn_helper : std::false_type {};

template <typename Ctx, typename Parent, typename... Children, typename... Callables>
struct ctx_fits_spawn_helper<Ctx, Parent, std::tuple<Children...>, std::tuple<Callables...>>
    : std::bool_constant<
          ::crucible::safety::CtxFitsPermissionFork<Ctx, Parent, Children...>&& ::crucible::safety::detail::
              permission_fork_ctx_callables_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>> {};

}  // namespace detail

// The two substrate gates are folded into one concept so the
// declaration below carries a single requires clause.
template <typename Ctx, typename Parent, typename ChildrenTuple, typename CallablesTuple>
concept CtxFitsSpawn = detail::ctx_fits_spawn_helper<Ctx, Parent, ChildrenTuple, CallablesTuple>::value;

// The call returns once every child has joined.
template <typename... Children, typename Ctx, typename Parent, typename... Callables>
    requires CtxFitsSpawn<Ctx, Parent, std::tuple<Children...>, std::tuple<std::decay_t<Callables>...>>
[[nodiscard]] Permission<Parent> mint_spawn(Ctx const& ctx, Permission<Parent>&& parent,
                                            Callables&&... callables) noexcept {
    return ::crucible::safety::mint_permission_fork<Children...>(ctx, std::move(parent),
                                                                 std::forward<Callables>(callables)...);
}

// The background capability is demanded even when N is one and no
// thread is spawned, so the contract does not change shape with N.
template <std::size_t N, typename Ctx, typename T, typename Whole, typename Body>
concept CtxFitsParallelFor = (N > 0) && ::crucible::effects::IsExecCtx<Ctx>
                          && ::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Bg>
                          && ::crucible::safety::CtxAdmitsPermission<Whole, Ctx>
                          && std::is_nothrow_invocable_v<Body&, OwnedRegion<T, Slice<Whole, 0>>&&>
                          && (N == 1 || std::is_copy_constructible_v<Body>);

// The call returns once every shard has run its body, and the region
// it hands back is rebuilt from the shards.
template <std::size_t N, typename Ctx, typename T, typename Whole, typename Body>
    requires CtxFitsParallelFor<N, Ctx, T, Whole, Body>
[[nodiscard]] OwnedRegion<T, Whole> mint_parallel_for(Ctx const& /*ctx*/, OwnedRegion<T, Whole>&& region,
                                                      Body body) noexcept {
    // The context is read by the constraint on the declaration and
    // nowhere else. The fan-out below is driven by N alone.
    return ::crucible::safety::parallel_for_views<N>(std::move(region), std::move(body));
}

}  // namespace crucible::fixy::spawn

namespace crucible::fixy::spawn::self_test {

static_assert(std::is_same_v<::crucible::fixy::spawn::Permission<int>, ::crucible::safety::Permission<int>>,
              "fixy::spawn::Permission must alias safety::Permission");

static_assert(std::is_same_v<::crucible::fixy::spawn::OwnedRegion<int, struct probe_tag_>,
                             ::crucible::safety::OwnedRegion<int, struct probe_tag_>>,
              "fixy::spawn::OwnedRegion must alias safety::OwnedRegion");

static_assert(std::is_same_v<::crucible::fixy::spawn::Slice<struct probe_tag_, 0>,
                             ::crucible::safety::Slice<struct probe_tag_, 0>>,
              "fixy::spawn::Slice must alias safety::Slice");

// The count covers the three type carriers, the two mints and the two
// concepts this header exports.
constexpr int spawn_surface_cardinality = 7;
static_assert(spawn_surface_cardinality == 7, "the exported surface of fixy::spawn has changed. Update this count "
                                              "and the negative-compile fixtures for the mints together.");

}  // namespace crucible::fixy::spawn::self_test
