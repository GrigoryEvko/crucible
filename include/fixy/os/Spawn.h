#pragma once

// Structured spawning: the rationale a spawn that no join ties to the
// frame has to state, and the two mints that fan work out and collect it
// again.
//
// Old spelling: include/crucible/fixy/spawn/Spawn.h,
// include/crucible/fixy/spawn/JoinPolicy.h and
// include/crucible/fixy/spawn/SpawnGrant.h, three headers because the
// grant system needed its which_dim specializations in a namespace of
// its own.  Atoms carry their axis, so the three are one here.
//
// Deviations, each deliberate:
//
//  1. mint_spawn re-adds the throws gate.  foundation/permissions/
//     PermissionFork.h checks a callable's own noexcept specification and
//     nothing else, because the control-flow atom is a fixy name and that
//     header is below fixy.  Its comment assigns the structural check
//     here, and this is where it lands: a callable whose type carries the
//     throws atom anywhere in its type tree is refused.  A noexcept
//     declaration is a promise the callable can break, and a throw
//     through a structured join tears the join rather than unwinding it.
//
//  2. fork_parent<ParentTag> and exec_ctx<Ctx> are gone.  Both were
//     grants whose comment said they "thread the parent identity and the
//     execution context through the grant pack, so the coherence check
//     below reads them without the call site retyping them", and the
//     coherence check read neither.  Nothing else read them either.  A
//     parameter no gate consumes is decoration.
//
//  3. The three rationale atoms live here rather than in fixy/atoms/,
//     for the reason fixy/os/Mmap.h gives for the advice tags: nothing
//     lifts them.  They carry Axis::Protocol, which is the routing the
//     old which_dim specializations gave them, and no effect row, because
//     stating a justification performs no operation.  The roster and the
//     two roster checks sit beside them.  Rule L003 of fixy/Collision.h
//     reads detach_with and syscall_only: a borrow together with a spawn
//     that no join ties to the frame is refused.
//
//  4. mint_parallel_for's body takes its shard by mutable reference and
//     the shards are recombined, not rebuilt.  The old one handed each
//     shard to the body by rvalue, let the body consume it, and then
//     called a private rebuild_parent_ helper that minted a fresh parent
//     Permission from nothing.  That helper is the forgeable path
//     fixy/OwnedRegion.h removed: a rebuild has to consume the thing it
//     reissues.  So the shards stay in the tuple, each worker mutates its
//     own element, and recombine consumes all of them to reissue the
//     parent.  The body signature changes with it; both mints had no
//     callers outside the old header, so nothing had to be adapted.
//
//  5. mint_spawn takes the budget of its children as a parameter, and
//     fixy/concurrent/ParallelismRule.h chooses the arm from it.
//     foundation ships two arms: mint_permission_fork (one thread per
//     child, needs Effect::Bg) and mint_permission_fork_inline (bodies in
//     child order on the calling thread).  A working set in the private
//     cache of one core runs inline, and a larger one starts one thread
//     per child.  The old choice read ctx_workbudget and
//     parallelism_decision_for, which read a workload axis of the old
//     context.  The new context carries no such axis, and
//     foundation/effects/Ctx.h tells a fork to take its budget as a
//     parameter.  The context must still own Effect::Bg, because the
//     choice happens at run time and the spawning arm must be admissible.
//     Bodies that wait on each other must not share a fork, because the
//     inline arm runs them one after another.
//
//     mint_parallel_for takes a budget too, and asks the rule for the
//     whole decision.  A sequential decision runs every shard on the
//     calling thread.  A parallel one starts no more threads than its
//     factor, and a thread runs every shard whose index is its own
//     modulo the thread count.  The old one started one thread per shard
//     whatever the size of the work, so a region in the private cache of
//     one core paid for N threads and ran slower than a loop.  Two shards
//     can run one after the other on one thread, so bodies that wait on
//     each other must not share a parallel-for either.
//
//  6. The join-mechanism tags and the concept that tied a mechanism to
//     its rationale atom are gone.  No mint read them, so the concept
//     refused nothing that a caller could reach.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/OwnedRegion.h>
#include <fixy/Throws.h>
#include <fixy/atoms/Ctrl.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

// The three atoms that put a non-default spawn engagement in the type,
// per deviation 3.  Each routes to Axis::Protocol, because a spawn
// engagement is itself a parent-child protocol, the same shape a binary
// session declares.
//
// A rationale rides as a fixed-string non-type template parameter rather
// than a free type parameter.  Two sites with different strings are then
// distinct types, so one grep over an atom name enumerates every
// justification in the codebase.  The emptiness check is a size
// comparison the atom runs on itself, so an empty literal is rejected at
// instantiation, before any consumer of the atom sees it.
namespace fixy::atom::spawn {

inline constexpr atom_seal atom_namespace_seal{};

// The same parameter type the atoms take, re-exported so a caller writes
// one here without reaching for the header that declares it.
template <std::size_t N>
using rationale = ::fixy::atom::ctrl::rationale<N>;

// The size of a fixed-string NTTP counts the trailing NUL, so an empty
// rationale has size 1.
template <::fixy::atom::ctrl::rationale Reason>
inline constexpr bool rationale_nonempty_v = (Reason.size() > 1);

template <::fixy::atom::ctrl::rationale Rationale>
struct detach_with final : atom_of<Axis::Protocol> {
    static_assert(rationale_nonempty_v<Rationale>, "detach_with<\"\"> rejected — Rationale must be non-empty. "
                                                   "Every detach() audit-trail must declare its non-engagement "
                                                   "reason in the type so `grep \"detach_with<\"` enumerates "
                                                   "every legitimate detach across the codebase. Replace the "
                                                   "empty literal with a descriptive justification (for "
                                                   "example, \"logger drain outlives container\").");
    static constexpr ::fixy::atom::ctrl::rationale reason = Rationale;
};

template <::fixy::atom::ctrl::rationale Rationale>
struct syscall_only final : atom_of<Axis::Protocol> {
    static_assert(rationale_nonempty_v<Rationale>, "syscall_only<\"\"> rejected — Rationale must be non-empty. "
                                                   "Raw clone(2) bypasses libc thread machinery and is reserved "
                                                   "for perf/bpf/cog loaders that genuinely need CLONE_VM / "
                                                   "CLONE_THREAD / CLONE_FILES semantics; the audit trail of "
                                                   "which loader and why is load-bearing.");
    static constexpr ::fixy::atom::ctrl::rationale reason = Rationale;
};

template <::fixy::atom::ctrl::rationale Rationale>
struct subprocess final : atom_of<Axis::Protocol> {
    static_assert(rationale_nonempty_v<Rationale>, "subprocess<\"\"> rejected — Rationale must be non-empty. "
                                                   "fork(2) / posix_spawn(3) creates a separate process image "
                                                   "that escapes the entire fixy:: type system; the script-side "
                                                   "opt-in guards the call site, this in-type atom complements "
                                                   "it with the audit-trail rationale.");
    static constexpr ::fixy::atom::ctrl::rationale reason = Rationale;
};

}  // namespace fixy::atom::spawn

namespace fixy::atom::detail {

// SAMPLES, not a roster, and the name says which.
//
// Every member is a parametric atom, so the namespace walk that catches an
// unrostered plain atom has nothing to read here; fixy/Atom.h says why
// beside that walk.  The set still earns its place: the two checks below
// read it, and they are what says a rationale atom is an atom on the axis
// it claims.
//
// It is a sample set rather than a family population because this family
// has no finite membership.  It is parametric over free-form justification
// text, so detach_with<"anything"> is an atom and no list can enumerate
// them.  The three strings below are placeholders chosen to instantiate
// the checks, not grades anybody writes.  Contrast observe_atom_roster,
// whose members are parameterised by a meaningful enumerator: that family
// is finite, its members are distinct grades, and it belongs in
// collision::all_atom_roster.  Joining these three would put three
// placeholder strings into the generated-rejection corpus as if they were
// grades.  fixy/Collision.h's sample_set_wrongly_joined() is what holds
// this distinction, so the name is load-bearing rather than descriptive.
using spawn_atom_samples = std::tuple<spawn::detach_with<"sample detach">, spawn::syscall_only<"sample clone">,
                                      spawn::subprocess<"sample fork">>;

}  // namespace fixy::atom::detail

namespace fixy::spawn {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

namespace detail {

template <typename Ctx, typename Parent, typename Brand, typename ChildrenTuple, typename CallablesTuple>
struct can_ctx_fit_spawn : std::false_type {};

template <typename Ctx, typename Parent, typename Brand, typename... Children, typename... Callables>
struct can_ctx_fit_spawn<Ctx, Parent, Brand, std::tuple<Children...>, std::tuple<Callables...>>
    : std::bool_constant<perm::CtxFitsPermissionFork<Ctx, Parent, Children...>
                         && perm::detail::can_each_body_take_its_child_v<Ctx, Brand, std::tuple<Children...>,
                                                                         std::tuple<Callables...>>> {};

// True when no callable's type carries the throws atom anywhere in its
// type tree.  Read by the mint's body, per deviation 1.
template <typename... Callables>
inline constexpr bool no_callable_throws_v =
    !(::fixy::type_tree_contains_throws_v<std::decay_t<Callables>> || ...);

}  // namespace detail

// The two substrate gates are folded into one concept so the declaration
// below carries a single requires clause.
template <typename Ctx, typename Parent, typename Brand, typename ChildrenTuple, typename CallablesTuple>
concept CtxFitsSpawn = detail::can_ctx_fit_spawn<Ctx, Parent, Brand, ChildrenTuple, CallablesTuple>::value;

// The call returns once every child has joined.  The budget states the
// bytes the children touch together, and the parallelism rule chooses
// the arm from it, per deviation 5.  Each body borrows its child through
// a WriteView of the parent's brand, as the fork lends it.
template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsSpawn<Ctx, Parent, Brand, std::tuple<Children...>, std::tuple<std::decay_t<Callables>...>>
[[nodiscard]] perm::Permission<Parent, Brand> mint_spawn(Ctx const& ctx, ::fixy::concurrent::WorkBudget budget,
                                                        perm::Permission<Parent, Brand>&& parent,
                                                        Callables&&... callables) noexcept {
    // Deviation 1.  The clause above has already checked each callable's
    // noexcept specification through foundation's gate; this is the
    // structural half, which foundation cannot express because the atom is
    // a fixy name.
    static_assert(detail::no_callable_throws_v<Callables...>,
                  "mint_spawn: a callable's type carries fixy::atom::ctrl::throws. A noexcept declaration is "
                  "a promise the callable can still break, and a throw out of a child tears through the join "
                  "instead of unwinding it. Remove the atom from the callable's type, or do not spawn it.");
    if (::fixy::concurrent::ParallelismRule::is_core_resident(budget.working_set_bytes())) {
        return perm::mint_permission_fork_inline<Children...>(ctx, std::move(parent),
                                                              std::forward<Callables>(callables)...);
    }
    return perm::mint_permission_fork<Children...>(ctx, std::move(parent), std::forward<Callables>(callables)...);
}

// The background capability is demanded even when N is one or the budget
// runs every shard inline, so the contract does not change shape with N
// or with the size of the work.  The choice between the arms happens at
// run time, and the threaded arm must be admissible.
// The shard a body actually receives carries the name of the split that
// cut it, and that name is minted inside the call below, so no clause
// out here can spell it.  The probe shard therefore carries the Unsplit
// name, which stands for the shape of a shard rather than for one
// split's shard.  A body generic over its shard satisfies the clause and
// the call; a body that spells the probe shard exactly satisfies the
// clause and then fails inside, which is the one case where the
// diagnostic lands in this header rather than at the call.
template <std::size_t N, typename Ctx, typename T, typename Whole, typename Brand, typename Body>
concept CtxFitsParallelFor =
    (N > 0) && eff::IsExecCtx<Ctx> && eff::CtxOwnsCapability<Ctx, eff::Effect::Bg>
    && perm::CtxAdmitsPermission<Whole, Ctx>
    && std::is_nothrow_invocable_v<Body&, ::fixy::OwnedRegion<T, ::fixy::Slice<Whole, 0>, Brand>&>
    && (N == 1 || std::is_copy_constructible_v<Body>);

// Declared before the runner so that its friend declaration names this
// mint and nothing else.
template <std::size_t N, typename Ctx, typename T, typename Whole, typename Brand, typename Body>
    requires CtxFitsParallelFor<N, Ctx, T, Whole, Brand, Body>
[[nodiscard]] ::fixy::OwnedRegion<T, Whole, Brand> mint_parallel_for(Ctx const& ctx,
                                                                     ::fixy::concurrent::WorkBudget budget,
                                                                     ::fixy::OwnedRegion<T, Whole, Brand>&& region,
                                                                     Body body) noexcept;

namespace detail {

// The number of threads that run N shards.  A sequential decision of the
// parallelism rule gives one, which is the calling thread.  A parallel
// decision gives its factor, and never more threads than shards.
template <std::size_t N>
[[nodiscard]] std::size_t parallel_for_thread_count(::fixy::concurrent::WorkBudget budget) noexcept {
    const ::fixy::concurrent::ParallelismDecision decision = ::fixy::concurrent::ParallelismRule::recommend(budget);
    if (!decision.is_parallel()) return 1;
    return std::min(N, decision.factor);
}

}  // namespace detail

// The fan-out of mint_parallel_for: one thread for each of `threads`
// workers, and each worker mutates the tuple elements whose index is its
// own modulo `threads`.  Each shard then has exactly one thread.  The
// array of jthreads joins in its destructor, so every body has returned
// before the member does and the tuple is whole again for recombine.
//
// Starting threads is the work of the mint, so only the mint may reach it.
// The member is private and static, the mint is the only friend, and the
// class is final and cannot be built.  The member also asks for the
// background effect itself, so the check stands where the threads start.
// A free function in a detail namespace once held this fan-out, and any
// translation unit could call it with no context at all.
class ParallelForRunner final {
    // No object of the runner exists.  Every constructor is deleted and
    // the destructor is user-provided, so the class is neither trivially
    // copyable nor an implicit-lifetime type, and no byte route
    // (std::bit_cast, std::start_lifetime_as) can make one either.
    ParallelForRunner() = delete("the parallel-for runner holds static members only; no object of it exists");
    ParallelForRunner(const ParallelForRunner&) = delete("the parallel-for runner holds static members only");
    ParallelForRunner& operator=(const ParallelForRunner&) = delete("the parallel-for runner holds static members only");
    ParallelForRunner(ParallelForRunner&&) = delete("the parallel-for runner holds static members only");
    ParallelForRunner& operator=(ParallelForRunner&&) = delete("the parallel-for runner holds static members only");
    constexpr ~ParallelForRunner() noexcept {}

    template <std::size_t N, typename Ctx, typename T, typename Whole, typename Brand, typename Body>
        requires CtxFitsParallelFor<N, Ctx, T, Whole, Brand, Body>
    friend ::fixy::OwnedRegion<T, Whole, Brand> mint_parallel_for(Ctx const& ctx,
                                                                  ::fixy::concurrent::WorkBudget budget,
                                                                  ::fixy::OwnedRegion<T, Whole, Brand>&& region,
                                                                  Body body) noexcept;

    template <typename Ctx, typename Shards, typename Body, std::size_t... Is>
        requires eff::CtxOwnsCapability<Ctx, eff::Effect::Bg>
    static void run_shards_(Ctx const&, Shards& shards, Body body, std::size_t threads,
                            std::index_sequence<Is...>) noexcept {
        std::array<std::jthread, sizeof...(Is)> workers{};
        for (std::size_t worker = 0; worker < threads; ++worker) {
            workers[worker] = std::jthread{[&shards, body, worker, threads](std::stop_token) mutable noexcept {
                ((Is % threads == worker ? static_cast<void>(body(std::get<Is>(shards))) : static_cast<void>(0)),
                 ...);
            }};
        }
    }
};

// The call returns once every shard has run its body, and the region it
// hands back is recombined from the shards.  The budget states the bytes
// the bodies touch together, and the parallelism rule chooses the thread
// count from it, per deviation 5.
template <std::size_t N, typename Ctx, typename T, typename Whole, typename Brand, typename Body>
    requires CtxFitsParallelFor<N, Ctx, T, Whole, Brand, Body>
[[nodiscard]] ::fixy::OwnedRegion<T, Whole, Brand> mint_parallel_for(Ctx const& ctx,
                                                                     ::fixy::concurrent::WorkBudget budget,
                                                                     ::fixy::OwnedRegion<T, Whole, Brand>&& region,
                                                                     Body body) noexcept {
    // The constraint on the declaration reads the context.  The runner
    // reads it too, and asks for the background effect again before it
    // starts a thread.
    auto parts = ::fixy::mint_split<N>(std::move(region));

    if constexpr (N == 1) {
        // One shard runs on the calling thread, whatever the budget.
        static_cast<void>(ctx);
        static_cast<void>(budget);
        static_cast<void>(body(std::get<0>(parts.shards)));
    } else {
        const std::size_t threads = detail::parallel_for_thread_count<N>(budget);
        if (threads == 1) {
            // The inline arm: every shard on the calling thread, in shard
            // order.
            [&parts, &body]<std::size_t... Is>(std::index_sequence<Is...>) noexcept {
                (static_cast<void>(body(std::get<Is>(parts.shards))), ...);
            }(std::make_index_sequence<N>{});
        } else {
            ParallelForRunner::run_shards_(ctx, parts.shards, body, threads, std::make_index_sequence<N>{});
        }
    }

    // Deviation 4: every shard is surrendered here, and their Slice
    // permissions are what reissue the parent's.  The receipt the split
    // wrote is surrendered with them, so this rebuild is the one that
    // split authorized and there is no second one.
    return ::fixy::OwnedRegion<T, Whole, Brand>::recombine(std::move(parts.witness), std::move(parts.shards));
}

}  // namespace fixy::spawn

namespace fixy::spawn::detail::spawn_self_test {

namespace atom_spawn = ::fixy::atom::spawn;
using ::fixy::atom::IsAtom;

// ── The rationale atoms ─────────────────────────────────────────────

static_assert(atom_spawn::rationale_nonempty_v<::fixy::atom::ctrl::rationale{"x"}>);
static_assert(atom_spawn::rationale_nonempty_v<::fixy::atom::ctrl::rationale{"reason"}>);
static_assert(!atom_spawn::rationale_nonempty_v<::fixy::atom::ctrl::rationale{""}>);

// Each one is an atom, on the axis deviation 3 names.
static_assert(IsAtom<atom_spawn::detach_with<"r">>);
static_assert(IsAtom<atom_spawn::syscall_only<"r">>);
static_assert(IsAtom<atom_spawn::subprocess<"r">>);
static_assert(atom_spawn::detach_with<"r">::axis == Axis::Protocol);
static_assert(atom_spawn::syscall_only<"r">::axis == Axis::Protocol);
static_assert(atom_spawn::subprocess<"r">::axis == Axis::Protocol);

// Stating a justification performs no operation, so none of them lifts to
// an effect row.  That is what keeps a rationale out of a context gate.
static_assert(!::foundation::effects::LiftsToRow<atom_spawn::detach_with<"r">>,
              "a rationale atom must not lift to a row: saying why is not doing anything.");
static_assert(!::foundation::effects::LiftsToRow<atom_spawn::syscall_only<"r">>);
static_assert(!::foundation::effects::LiftsToRow<atom_spawn::subprocess<"r">>);

static_assert(::fixy::atom::detail::every_roster_member_is_atom_<::fixy::atom::detail::spawn_atom_samples>(),
              "fixy/os/Spawn.h: a member of spawn_atom_samples is not an atom.");
static_assert(::fixy::atom::detail::every_roster_member_on_axis_<::fixy::atom::detail::spawn_atom_samples,
                                                                 Axis::Protocol>(),
              "fixy/os/Spawn.h: every spawn rationale atom engages Axis::Protocol.");

static_assert(std::is_empty_v<atom_spawn::detach_with<"x">>);
static_assert(sizeof(atom_spawn::detach_with<"x">) == 1);
static_assert(atom_spawn::detach_with<"audit">::reason.size() == 6);  // "audit" plus the terminator

// The rationale is part of the type, so two reasons are two types.
static_assert(!std::is_same_v<atom_spawn::detach_with<"reason_a">, atom_spawn::detach_with<"reason_b">>);
static_assert(!std::is_same_v<atom_spawn::detach_with<"x">, atom_spawn::syscall_only<"x">>);

// ── The throws gate ─────────────────────────────────────────────────

struct PlainCallable {
    void operator()() const noexcept {}
};

// A callable whose type names the atom, at the default family and at a
// named one.  The second is the case the old needle missed.
template <typename Marker>
struct MarkedCallable {
    void operator()() const noexcept {}
};

struct SampleException {};

static_assert(detail::no_callable_throws_v<PlainCallable>);
static_assert(detail::no_callable_throws_v<PlainCallable, PlainCallable>);
static_assert(!detail::no_callable_throws_v<MarkedCallable<::fixy::atom::ctrl::throws<>>>);
static_assert(!detail::no_callable_throws_v<MarkedCallable<::fixy::atom::ctrl::throws<SampleException>>>,
              "a callable marked with a named exception family must be refused too. The old needle matched "
              "only the default family, so this case passed.");
static_assert(!detail::no_callable_throws_v<PlainCallable, MarkedCallable<::fixy::atom::ctrl::throws<>>>,
              "one marked callable anywhere in the pack refuses the whole spawn.");
static_assert(detail::no_callable_throws_v<>, "an empty pack spawns nothing and carries no throw.");

}  // namespace fixy::spawn::detail::spawn_self_test
