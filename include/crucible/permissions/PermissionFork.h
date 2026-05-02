#pragma once

// ── crucible::safety::mint_permission_fork — CSL parallel rule encoding ───
//
// Direct C++ encoding of Concurrent Separation Logic's parallel
// composition rule:
//
//   {P1} C1 {Q1}    {P2} C2 {Q2}                         ... {Pn} Cn {Qn}
//   ────────────────────────────────────────────────────────────────────────
//   {P1 * P2 * ... * Pn} (C1 || C2 || ... || Cn) {Q1 * Q2 * ... * Qn}
//
// mint_permission_fork takes a parent Permission, splits it into N disjoint
// children via splits_into_pack, spawns one std::jthread per child
// passing it the child Permission, joins all threads (RAII), and
// returns the parent Permission.  The parent is the SAME Tag the
// caller had before — but every child has independently mutated its
// own region, and the type system proved at compile time that no two
// children touched the same region.
//
//   Axiom coverage: ThreadSafe + BorrowSafe + the CSL parallel rule
//                   itself.
//   Runtime cost:   exactly one std::jthread spawn + join per child.
//                   The Permission tokens are zero-byte (EBO).
//
// ─── Why this is THE structured-concurrency primitive ──────────────
//
// Compare to raw thread spawning:
//   * Raw `std::jthread` requires manual coordination (atomic counters,
//     spin-loops on conditions) for "all done" — error-prone, the
//     SHARDED-test deadlock that motivated this file is exactly that
//     pattern's failure mode.
//   * mint_permission_fork joins on RAII (jthread destructor in scope-exit
//     order); the parent Permission is returned only AFTER all children
//     have finished.  No counter, no condition, no spin.
//
// Compare to std::async / std::future:
//   * std::async returns a std::future which the caller must remember
//     to .get() — easy to leak (future destructor blocks if not got).
//   * mint_permission_fork is a single expression; the join is automatic.
//
// Compare to OpenMP / TBB parallel_for:
//   * Those rely on runtime task-stealing pools (overhead) and don't
//     plumb permissions through to the bodies.
//   * mint_permission_fork is zero-overhead beyond jthread spawn/join, and
//     each child receives a TYPED permission proving its disjoint
//     region access.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * splits_into_pack_v<Parent, Children...> must be true (declarative
//     manifest in safety/Permission.h).
//   * sizeof...(Children) == sizeof...(Callables).
//   * Each Callable_i must be invocable as Callable_i(Permission<Child_i>&&)
//     — it CONSUMES its child's Permission token.
//   * Callables should be noexcept (Crucible's -fno-exceptions rule);
//     uncaught exceptions in jthread bodies call std::terminate.
//   * Children must NOT outlive the call (jthread joins before return).
//
// ─── Performance ────────────────────────────────────────────────────
//
//   N child jthreads = N pthread_create + N pthread_join.  Spawn cost
//   ~5-15 µs per thread on Linux.  This is the right primitive for
//   "few threads, long bodies" — NOT for "thousands of micro-tasks"
//   (use ChaseLevDeque + ThreadPool for that, see SEPLOG-C4).
//
// ─── Example ────────────────────────────────────────────────────────
//
//   // 1. Tag tree
//   namespace work {
//       struct Whole {};
//       struct Left {};
//       struct Right {};
//   }
//   namespace crucible::safety {
//       template <> struct splits_into_pack<work::Whole, work::Left, work::Right>
//                    : std::true_type {};
//   }
//
//   // 2. Mint and fork:
//   auto whole = mint_permission_root<work::Whole>();
//
//   auto returned_whole = mint_permission_fork<work::Left, work::Right>(
//       std::move(whole),
//       [](Permission<work::Left>) noexcept  { /* mutate left region */ },
//       [](Permission<work::Right>) noexcept { /* mutate right region */ }
//   );
//
//   // 3. After return, both threads have joined; both regions have been
//   //    touched in parallel; type system proved disjointness; the
//   //    `returned_whole` Permission is again exclusive in this scope.

#include <crucible/Platform.h>
#include <crucible/permissions/Permission.h>

#include <array>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

namespace detail {

// Spawn one jthread per child, capturing the child's Permission and
// the corresponding callable by move.  std::array<jthread, N>'s
// destructor (in fn epilogue) joins all threads in reverse order.
//
// Implemented as a fold over std::index_sequence so we can handle
// arbitrary N at compile time without recursion.

template <typename Children, typename Callables, std::size_t... Is>
void mint_permission_fork_spawn_(Children&& children,
                             Callables&& callables,
                             std::index_sequence<Is...>) noexcept
{
    // Build the array of jthreads in-place.  Each entry's lambda
    // captures-by-move its child Permission and its corresponding
    // callable.  jthread's stop_token argument is unused in our
    // pattern (the body runs to completion without cancellation).
    //
    // The array's destructor joins all jthreads when the function
    // returns, by reverse-of-construction order (which doesn't matter
    // for correctness — all bodies must complete before we return).
    [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {
        std::jthread{
            [child_perm = std::move(std::get<Is>(std::forward<Children>(children))),
             callable   = std::move(std::get<Is>(std::forward<Callables>(callables)))]
            (std::stop_token) mutable noexcept {
                callable(std::move(child_perm));
            }
        }...
    };
    // ~std::array runs here, joining each jthread.
}

}  // namespace detail

// ── mint_permission_fork<Children...>(parent, callables...) ──────────────
//
// Encodes CSL's parallel composition rule.  Returns the rebuilt
// parent Permission after all children have joined.
//
// Constraints:
//   * splits_into_pack_v<Parent, Children...>
//   * sizeof...(Children) == sizeof...(Callables)
//   * each Callable_i invocable as Callable_i(Permission<Child_i>&&)

template <typename... Children, typename Parent, typename... Callables>
[[nodiscard]] Permission<Parent> mint_permission_fork(
    Permission<Parent>&& parent,
    Callables&&... callables) noexcept
{
    static_assert(splits_into_pack_v<Parent, Children...>,
        "mint_permission_fork<Children...>(Permission<Parent>&&, ...) requires "
        "splits_into_pack<Parent, Children...>::value to be specialized true.");
    static_assert(sizeof...(Children) == sizeof...(Callables),
        "mint_permission_fork: number of Child tags must match number of callables.");
    static_assert((std::is_invocable_v<Callables, Permission<Children>> && ...),
        "mint_permission_fork: each callable must be invocable as "
        "Callable_i(Permission<Child_i>&&).");
    static_assert((std::is_nothrow_invocable_v<Callables, Permission<Children>> && ...),
        "mint_permission_fork: callables must be noexcept (Crucible -fno-exceptions).");

    // Step 1: split the parent into disjoint child Permissions.
    auto child_perms =
        mint_permission_split_n<Children...>(std::move(parent));

    // Step 2: pack the callables for index_sequence-driven spawn.
    auto callable_pack = std::tuple<std::decay_t<Callables>...>{
        std::forward<Callables>(callables)...
    };

    // Step 3: spawn jthreads, join via array destructor.
    detail::mint_permission_fork_spawn_(
        std::move(child_perms),
        std::move(callable_pack),
        std::index_sequence_for<Children...>{});

    // Step 4: rebuild the parent Permission.  All children consumed
    // their tokens; no holder remains; the parent region is again
    // exclusive in the caller's scope.  See discipline note in
    // safety/Permission.h about why this is sound under the
    // linearity/no-exceptions invariants.
    return mint_permission_fork_rebuild_<Parent>();
}

}  // namespace crucible::safety
