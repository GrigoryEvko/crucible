#pragma once

// Structured spawning: the rationale a spawn that no join ties to the
// frame has to state, and the two mints that fan work out and collect it
// again.
//
// Design notes:
//
//  1. mint_spawn carries the throws gate.  foundation/permissions/
//     PermissionFork.h checks a callable's own noexcept specification and
//     nothing else, because the control-flow atom is a fixy name and that
//     header is below fixy.  Its comment assigns the structural check
//     here, and this is where it lands: a callable whose type carries the
//     throws atom anywhere in its type tree is refused.  A noexcept
//     declaration is a promise the callable can break, and a throw
//     through a structured join tears the join rather than unwinding it.
//
//  2. The three rationale atoms live here rather than in fixy/atoms/,
//     because nothing lifts them.  They carry Axis::Protocol and no
//     effect row, because stating a justification performs no operation.
//     The roster sits beside them, and its two checks are in the check
//     file of this header.  Rule L003 of
//     fixy/Collision.h reads detach_with and syscall_only: a borrow
//     together with a spawn that no join ties to the frame is refused.
//
//  3. The runner of mint_parallel_for is the frame door of each shard
//     (foundation/core/Frame.h).  A body gets the ViewMut of the elements of
//     its shard and the ShardPlace of the shard, and it names no region.  The
//     shards stay in the tuple of the runner, which no body names, so no
//     door of a body reaches the run of a shard beside its ViewMut.  The
//     ViewMut of shard I carries the tag of shard I as its brand, so the
//     ViewMuts of two shards name two brands, and their runs do not overlap.
//     The shards are recombined, not rebuilt.  A rebuild has to consume the
//     thing it reissues, so no helper mints a parent Permission from nothing.
//     recombine consumes each shard to reissue the parent.
//
//  4. mint_spawn takes the coupling of its bodies as a template argument
//     with no default, and the budget of its children as a parameter.  The
//     two arms are mint_permission_fork of fixy/concurrent/PermissionFork.h
//     (one thread per child, needs Effect::Bg) and
//     mint_permission_fork_inline of foundation/permissions/PermissionFork.h
//     (bodies in child order on the calling thread).  choose_spawn_arm
//     gives the arm.  Bodies that wait on each other, such as the two sides
//     of a channel, always get one thread each: on one thread the first
//     wait never ends, because the other body runs only after it returns.
//     Their liveness then does not change with the budget or with the host.
//     For independent bodies, fixy/concurrent/ParallelismRule.h chooses
//     from the budget: a working set in the private cache of one core runs
//     inline, and a larger one starts one thread per child.  The context
//     carries no workload axis, and foundation/effects/Ctx.h tells a fork to
//     take its budget as a parameter.  The context must still own
//     Effect::Bg, because the choice happens at run time and the spawning
//     arm must be admissible.  mint_spawn_on_host is the door of a test: it
//     reads the facts of a host that the test gives, so a test runs the
//     mint under each cache size with no global state.
//
//     mint_parallel_for takes the coupling of its shards and a budget too.
//     choose_parallel_for_threads gives its count of threads.  Shards that
//     wait on each other get one thread each, for each budget and each
//     host, because two of them on one thread never end.  For independent
//     shards the parallelism rule gives the whole decision.  A sequential
//     decision runs every shard on the calling thread.  A parallel one
//     starts no more threads than its factor, and a thread runs every shard
//     whose index is its own modulo the thread count.  One thread per
//     shard, whatever the size of the work, makes a region in the private
//     cache of one core pay for N threads, and it runs slower than a loop.
//
//  5. The parallel factor of a mint_parallel_for of independent shards is a
//     choice of speed and not of result.  When the door of
//     fixy/os/ThreadTasks.h starts no worker, the mint runs each shard on
//     the calling thread, as a sequential decision does.  The door gives an
//     error only when no thread of the call started, which in practice is
//     eagain, so no shard ran and the inline run gives the same result.  A
//     start that fails after other workers started ends the process in the
//     door.  The door does not give the count of the workers that started,
//     so the mint cannot give their shards to the calling thread.  Shards
//     that wait on each other cannot run on the calling thread, so a door
//     that starts no worker of theirs ends the process.
//     mint_parallel_for_with_thread_options is the door of a test: it
//     gives the workers options that the thread door refuses, and so a
//     test reaches the inline run with no global state.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/OwnedRegion.h>
#include <fixy/Throws.h>
#include <fixy/atoms/Ctrl.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <fixy/concurrent/PermissionFork.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Record.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>
#include <foundation/reflect/TypeList.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// The three atoms that put a non-default spawn engagement in the type,
// per design note 2.  Each routes to Axis::Protocol, because a spawn
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

// Samples, not a roster, and the name says which.
//
// Every member is a parametric atom, so the namespace walk that catches an
// unrostered plain atom has nothing to read here; fixy/Atom.h says why
// beside that walk.  The set still earns its place: two checks in the
// check file of this header read it, and they are what says a rationale
// atom is an atom on the axis it claims.
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
// grades.  sample_set_wrongly_joined() of fixy/collision/Report.h holds
// this distinction, so the name is load-bearing rather than descriptive.
using spawn_atom_samples = std::tuple<spawn::detach_with<"sample detach">, spawn::syscall_only<"sample clone">,
                                      spawn::subprocess<"sample fork">>;

}  // namespace fixy::atom::detail

namespace fixy::spawn {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

namespace detail {

// The std::tuple of the types of a foundation::reflect::Pack, in the shape
// that each_body_takes_its_child of foundation reads.
[[nodiscard]] consteval std::meta::info tuple_of_pack(std::meta::info pack) {
    return std::meta::substitute(^^std::tuple, std::meta::template_arguments_of(pack));
}

// True when the fork admits the context, the parent and the children, and
// each body takes the view of its own child.  The two packs are
// foundation::reflect::Pack, because CtxFitsSpawn reads PackType before
// this test.  The test is a function at namespace scope that is not a
// template, so no translation unit can specialize it to spawn a body that
// the fork refuses.  The tags are a std::vector copy: the arguments of the
// fork take their iterators, and std::meta::substitute takes no
// ReflectionList.
[[nodiscard]] consteval bool spawn_fits(std::meta::info ctx, std::meta::info parent, std::meta::info brand,
                                        std::meta::info children_pack, std::meta::info callables_pack) {
    const std::meta::info children = std::meta::dealias(children_pack);
    const std::meta::info callables = std::meta::dealias(callables_pack);
    const std::vector<std::meta::info> child_tags = std::meta::template_arguments_of(children);
    std::vector<std::meta::info> fork_arguments{ctx, parent};
    fork_arguments.insert(fork_arguments.end(), child_tags.begin(), child_tags.end());
    return std::meta::extract<bool>(std::meta::substitute(^^::fixy::concurrent::CtxFitsPermissionFork, fork_arguments))
        && perm::detail::each_body_takes_its_child(ctx, brand, tuple_of_pack(children), tuple_of_pack(callables));
}

// True when no callable's type carries the throws atom anywhere in its
// type tree.  Read by the mint's body, per design note 1.
template <typename... Callables>
inline constexpr bool no_callable_throws_v = !(::fixy::type_tree_contains_throws_v<std::decay_t<Callables>> || ...);

}  // namespace detail

// The two substrate gates are folded into one concept so the declaration
// below carries a single requires clause.
// ChildrenPack and CallablesPack are foundation::reflect::Pack of the
// children and of the decayed bodies.
template <typename Ctx, typename Parent, typename Brand, typename ChildrenPack, typename CallablesPack>
concept CtxFitsSpawn = ::foundation::reflect::PackType<ChildrenPack> && ::foundation::reflect::PackType<CallablesPack>
                    && detail::spawn_fits(^^Ctx, ^^Parent, ^^Brand, ^^ChildrenPack, ^^CallablesPack);

// The gate of the door of a test (design note 4): the gate of mint_spawn,
// and a second context that owns Test, which only the testing door of
// foundation/effects/Effect.h mints.
template <typename Ctx, typename TestCtx, typename Parent, typename Brand, typename ChildrenPack,
          typename CallablesPack>
concept CtxFitsSpawnOnHost =
    CtxFitsSpawn<Ctx, Parent, Brand, ChildrenPack, CallablesPack> && eff::CtxOwnsCapability<TestCtx, eff::Effect::Test>;

// The coupling of the bodies of one spawn or one parallel loop, which the
// caller states (design note 4).
enum class BodyCoupling : std::uint8_t {
    // No body waits on another body of the call.  The bodies can run one
    // after another on one thread, and the budget chooses the arm.
    Independent = 0,
    // A body can wait on another body of the call, for example on a message
    // through a channel.  Each body runs on a thread of its own.
    WaitOnEachOther = 1,
};

// The arm of one spawn.
enum class SpawnArm : std::uint8_t {
    // Each body in child order, on the calling thread.
    Inline = 0,
    // One thread for each body.
    ThreadPerChild = 1,
};

// The arm of a spawn of bodies of this coupling, with this budget, on this
// host (design note 4).  Bodies that wait on each other get one thread each
// for each budget and each host.
[[nodiscard]] constexpr SpawnArm choose_spawn_arm(BodyCoupling coupling, ::fixy::concurrent::HostFacts const& host,
                                                  ::fixy::concurrent::WorkBudget budget) noexcept {
    if (coupling == BodyCoupling::WaitOnEachOther) return SpawnArm::ThreadPerChild;
    const bool is_core_resident = ::fixy::concurrent::is_core_private_tier(
        ::fixy::concurrent::ParallelismRule::classify_for(host, budget.working_set_bytes()));
    return is_core_resident ? SpawnArm::Inline : SpawnArm::ThreadPerChild;
}

// The count of the threads that run the shards of one parallel loop of
// shards of this coupling, with this budget, on this host (design note 4).
// Shards that wait on each other get one thread each.  For independent
// shards, a sequential decision of the parallelism rule gives one, which is
// the calling thread, and a parallel decision gives its factor, never more
// threads than shards.
[[nodiscard]] constexpr std::size_t choose_parallel_for_threads(BodyCoupling coupling,
                                                                ::fixy::concurrent::HostFacts const& host,
                                                                ::fixy::concurrent::WorkBudget budget,
                                                                std::size_t shards) noexcept {
    if (coupling == BodyCoupling::WaitOnEachOther) return shards;
    const ::fixy::concurrent::ParallelismDecision decision =
        ::fixy::concurrent::ParallelismRule::recommend_for(host, budget);
    if (!decision.is_parallel()) return 1;
    return decision.factor < shards ? decision.factor : shards;
}

namespace detail {

// The body of the two spawn mints, on the facts of a host.  The arm of bodies
// that wait on each other is the fork of threads alone, so the inline arm is
// not part of that instantiation.
template <BodyCoupling Coupling, typename... Children, typename Ctx, typename Parent, typename Brand,
          typename... Callables>
    requires CtxFitsSpawn<Ctx, Parent, Brand, ::foundation::reflect::Pack<Children...>,
                          ::foundation::reflect::Pack<std::decay_t<Callables>...>>
[[nodiscard]] perm::fork_result_t<Ctx, Parent, Brand, perm::detail::fork_pack_<Children...>,
                                  perm::detail::fork_pack_<std::decay_t<Callables>...>>
spawn_on_host_(Ctx const& ctx, ::fixy::concurrent::HostFacts const& host, ::fixy::concurrent::WorkBudget budget,
               perm::Permission<Parent, Brand>&& parent, Callables&&... callables) noexcept {
    // Design note 1.  The clause above has already checked each callable's
    // noexcept specification through foundation's gate; this is the
    // structural half, which foundation cannot express because the atom is
    // a fixy name.
    static_assert(no_callable_throws_v<Callables...>,
                  "mint_spawn: a callable's type carries fixy::atom::ctrl::throws. A noexcept declaration is "
                  "a promise the callable can still break, and a throw out of a child tears through the join "
                  "instead of unwinding it. Remove the atom from the callable's type, or do not spawn it.");
    static_assert(
        (::fixy::concurrent::detail::ThreadForkValue<perm::detail::lent_result_t<Callables, Children, Brand, Ctx>>
         && ...),
        "mint_spawn: the value of a body crosses from the thread of the body to the caller.  So it holds no "
        "borrow, an Option takes it by assignment, and it is Sendable (foundation/Transfer.h).");
    if constexpr (Coupling == BodyCoupling::Independent) {
        if (choose_spawn_arm(Coupling, host, budget) == SpawnArm::Inline) {
            return perm::mint_permission_fork_inline<Children...>(ctx, std::move(parent),
                                                                  std::forward<Callables>(callables)...);
        }
    } else {
        static_cast<void>(host);
        static_cast<void>(budget);
    }
    return ::fixy::concurrent::mint_permission_fork<Children...>(ctx, std::move(parent),
                                                                 std::forward<Callables>(callables)...);
}

}  // namespace detail

// The call returns once every child has joined.  The coupling states whether
// a body can wait on another body, and the budget states the bytes the
// children touch together.  choose_spawn_arm gives the arm from the two and
// the facts of this host, per design note 4.  Each body borrows its child
// through a WriteView of the parent's brand, as the fork lends it.
template <BodyCoupling Coupling, typename... Children, typename Ctx, typename Parent, typename Brand,
          typename... Callables>
    requires CtxFitsSpawn<Ctx, Parent, Brand, ::foundation::reflect::Pack<Children...>,
                          ::foundation::reflect::Pack<std::decay_t<Callables>...>>
[[nodiscard]] perm::fork_result_t<Ctx, Parent, Brand, perm::detail::fork_pack_<Children...>,
                                  perm::detail::fork_pack_<std::decay_t<Callables>...>>
mint_spawn(Ctx const& ctx, ::fixy::concurrent::WorkBudget budget, perm::Permission<Parent, Brand>&& parent,
           Callables&&... callables) noexcept {
    return detail::spawn_on_host_<Coupling, Children...>(ctx, ::fixy::concurrent::ParallelismRule::host_facts(), budget,
                                                         std::move(parent), std::forward<Callables>(callables)...);
}

// mint_spawn on the facts of a host that a test gives, for a test (design
// note 4).  The test context is the authority of the facts.
template <BodyCoupling Coupling, typename... Children, typename Ctx, typename TestCtx, typename Parent, typename Brand,
          typename... Callables>
    requires CtxFitsSpawnOnHost<Ctx, TestCtx, Parent, Brand, ::foundation::reflect::Pack<Children...>,
                                ::foundation::reflect::Pack<std::decay_t<Callables>...>>
[[nodiscard]] perm::fork_result_t<Ctx, Parent, Brand, perm::detail::fork_pack_<Children...>,
                                  perm::detail::fork_pack_<std::decay_t<Callables>...>>
mint_spawn_on_host(Ctx const& ctx, TestCtx const& test, ::fixy::concurrent::HostFacts const& host,
                   ::fixy::concurrent::WorkBudget budget, perm::Permission<Parent, Brand>&& parent,
                   Callables&&... callables) noexcept {
    static_cast<void>(test);
    return detail::spawn_on_host_<Coupling, Children...>(ctx, host, budget, std::move(parent),
                                                         std::forward<Callables>(callables)...);
}

class ParallelForRunner;

}  // namespace fixy::spawn

namespace foundation::core {

// The door of the runner of a parallel loop to the key of the frame doors
// (the doors of the owners above foundation/core/Frame.h).  Its one member is
// private, and ParallelForRunner of fixy/concurrent/Spawn.h is its one friend.
// The invariant: the runner reads the run of each shard that the split of its
// region cut, and it lends the run of a shard to one body at a time, with the
// tag of the shard as its brand.  The split cut the shards out of one run, so
// no two shards overlap.  A home owner row of utils/rule-table.txt keeps this
// definition in this header.
class ParallelForFrameKeyDoor final : ::foundation::NoObject<ParallelForFrameKeyDoor> {
    friend class ::fixy::spawn::ParallelForRunner;

    // The key of the frame doors, for the runs of the shards.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

}  // namespace foundation::core

namespace fixy::spawn {

namespace detail {

// The run that the frame of a shard lends to its body: the ViewMut of the
// elements, with the tag of the shard as its brand.  A region of const
// elements lends the View of its elements, which no body writes.
template <typename T, typename ShardTag, bool = std::is_const_v<T>>
struct shard_run {
    using type = ::foundation::core::ViewMut<T, ShardTag>;
};

template <typename T, typename ShardTag>
struct shard_run<T, ShardTag, true> {
    using type = ::foundation::core::View<T>;
};

}  // namespace detail

template <typename T, typename ShardTag>
using shard_run_t = typename detail::shard_run<T, ShardTag>::type;

// The place of one shard in the region that the split of mint_parallel_for
// cut: the index of the shard among the N shards, and the position of the
// first element of the shard in that region.  The runner gives each body the
// place of its shard beside the ViewMut of its elements, so a body that
// reads a run of the whole at the position of its shard needs no address.
// Only the runner builds a place.
template <std::size_t N>
    requires ::foundation::core::IndexCount<N>
class [[nodiscard]] ShardPlace final {
    ::foundation::core::Index<N> index_{::foundation::core::index<0>()};
    std::size_t first_ = 0;

    constexpr ShardPlace(::foundation::core::Index<N> index, std::size_t first) noexcept
        : index_{index}, first_{first} {}

    friend class ParallelForRunner;

public:
    [[nodiscard]] constexpr ::foundation::core::Index<N> index() const noexcept { return index_; }
    [[nodiscard]] constexpr std::size_t first() const noexcept { return first_; }
};

// The background capability is demanded even when N is one or the budget
// runs every shard inline, so the contract does not change shape with N
// or with the size of the work.  The choice between the arms happens at
// run time, and the threaded arm must be admissible.
// The ViewMut that a body actually receives carries the tag of its shard,
// which names the split that cut it, and that name is minted inside the
// call below, so no clause out here can spell it.  The probe therefore
// carries the tag of shard 0 with the Unsplit name, which stands for the
// shape of a shard rather than for one split's shard.  A body generic over
// its ViewMut satisfies the clause and the call; a body that spells the
// probe exactly satisfies the clause and then fails inside, which is the one
// case where the diagnostic lands in this header rather than at the call.
// The body takes the ViewMut by value: a body that takes it by reference
// does not satisfy the clause.  A region of const elements gives the View of
// its elements in place of the ViewMut (shard_run_t).
template <std::size_t N, typename Ctx, typename T, typename Whole, typename Brand, typename Body>
concept CtxFitsParallelFor = (N > 0) && eff::IsExecCtx<Ctx> && eff::CtxOwnsCapability<Ctx, eff::Effect::Bg>
                          && perm::CtxAdmitsPermission<Whole, Ctx>
                          && std::is_nothrow_invocable_v<Body&, shard_run_t<T, ::fixy::Slice<Whole, 0>>, ShardPlace<N>>
                          && (N == 1 || std::is_copy_constructible_v<Body>);

// The gate of the door of a test (design note 5).  The context passes the
// gate of mint_parallel_for.  The second context owns Test, which only the
// testing door of foundation/effects/Effect.h mints, so code that ships
// cannot give the workers options of its own.
template <std::size_t N, typename Ctx, typename TestCtx, typename T, typename Whole, typename Brand, typename Body>
concept CtxFitsParallelForWithThreadOptions =
    CtxFitsParallelFor<N, Ctx, T, Whole, Brand, Body> && eff::CtxOwnsCapability<TestCtx, eff::Effect::Test>;

// The policy of the body of the parallel loop (foundation/Closure.h): the
// workers run at the same time, each copies the one body object, and a
// worker calls its copy for each of its shards.
inline constexpr ::foundation::closure::DoorPolicy parallel_for_body_policy{
    ::foundation::closure::Crossing::ConcurrentThreads, ::foundation::closure::QttGrade::Omega, true};

// Declared before the runner so that its friend declarations name these
// two mints and nothing else.
template <BodyCoupling Coupling, std::size_t N, typename Ctx, typename T, typename Whole, typename Brand, typename Body>
    requires CtxFitsParallelFor<N, Ctx, T, Whole, Brand, Body>
[[nodiscard]] ::fixy::OwnedRegion<T, Whole, Brand>
mint_parallel_for(Ctx const& ctx, ::fixy::concurrent::WorkBudget budget, ::fixy::OwnedRegion<T, Whole, Brand>&& region,
                  Body body) noexcept;

template <BodyCoupling Coupling, std::size_t N, typename Ctx, typename TestCtx, typename T, typename Whole,
          typename Brand, typename Body>
    requires CtxFitsParallelForWithThreadOptions<N, Ctx, TestCtx, T, Whole, Brand, Body>
[[nodiscard]] ::fixy::OwnedRegion<T, Whole, Brand>
mint_parallel_for_with_thread_options(Ctx const& ctx, TestCtx const& test, ::fixy::concurrent::WorkBudget budget,
                                      ::fixy::OwnedRegion<T, Whole, Brand>&& region, Body body,
                                      ::fixy::thread::ThreadOptions const& options) noexcept;

// The work of mint_parallel_for.  run_shards_ is the fan-out: one thread
// for each of `threads` workers, and each worker runs the frame of each
// shard whose index is its own modulo `threads`.  Each shard then has
// exactly one thread.  The door of fixy/os/ThreadTasks.h starts the threads
// and joins every one before it returns, so every body has returned before
// the member does and the tuple is whole again for recombine.
//
// Starting threads is the work of the mint, so only the mint may reach it.
// The members are private and static, the two mints are the only friends,
// and the class is final and cannot be built.  The fan-out also asks for
// the background effect itself, so the check stands where the threads
// start.  A free function in a detail namespace is not a place for this
// fan-out, because any translation unit can call it with no context at
// all.
class ParallelForRunner final : ::foundation::NoObject<ParallelForRunner> {
    template <BodyCoupling Coupling, std::size_t N, typename Ctx, typename T, typename Whole, typename Brand,
              typename Body>
        requires CtxFitsParallelFor<N, Ctx, T, Whole, Brand, Body>
    friend ::fixy::OwnedRegion<T, Whole, Brand> mint_parallel_for(Ctx const& ctx, ::fixy::concurrent::WorkBudget budget,
                                                                  ::fixy::OwnedRegion<T, Whole, Brand>&& region,
                                                                  Body body) noexcept;

    template <BodyCoupling Coupling, std::size_t N, typename Ctx, typename TestCtx, typename T, typename Whole,
              typename Brand, typename Body>
        requires CtxFitsParallelForWithThreadOptions<N, Ctx, TestCtx, T, Whole, Brand, Body>
    friend ::fixy::OwnedRegion<T, Whole, Brand>
    mint_parallel_for_with_thread_options(Ctx const& ctx, TestCtx const& test, ::fixy::concurrent::WorkBudget budget,
                                          ::fixy::OwnedRegion<T, Whole, Brand>&& region, Body body,
                                          ::fixy::thread::ThreadOptions const& options) noexcept;

    // What one worker reads.  The frames live in the frame of run_shards_,
    // which waits in the door until every worker has joined.
    template <typename Shards, typename Body>
    struct worker_frame_ {
        Shards* shards = nullptr;
        Body const* body = nullptr;
        std::size_t worker = 0;
        std::size_t threads = 1;
    };

    // The first element of the region that the split cut: the first element
    // of shard 0, which starts at the position zero of the region.  No body
    // reaches a shard, so no shard moves while the bodies run.
    template <typename Shards>
    [[nodiscard]] static auto first_of_whole_(Shards& shards) noexcept {
        return first_of_(::foundation::core::detail::BaseLayerRead{},
                         frame_run_(::foundation::core::ParallelForFrameKeyDoor::key_(), std::get<0>(shards)));
    }

    // The frame of shard I (design note 3): body gets the ViewMut of the
    // elements of the shard, with the tag of the shard as its brand, and the
    // place of the shard among the N shards.  The split cut each shard out of
    // one run, so the position of the shard is the distance of its first
    // element from the first element of the whole.  A shard of const elements
    // gives the View of its elements (shard_run_t).  The shard stays in the
    // tuple, and only this frame lends its run.  Complexity: O(1), and the
    // cost of body.
    template <std::size_t I, std::size_t N, typename Shards, typename Body, typename Element>
    static void run_shard_(Shards& shards, Body& body, Element* whole_first) noexcept {
        auto& shard = std::get<I>(shards);
        using Shard = std::remove_reference_t<decltype(shard)>;
        static_assert(std::is_same_v<Element, typename Shard::value_type>);
        ::foundation::core::View<Element> const run =
            frame_run_(::foundation::core::ParallelForFrameKeyDoor::key_(), shard);
        ShardPlace<N> const place{
            ::foundation::core::index<I>(),
            static_cast<std::size_t>(first_of_(::foundation::core::detail::BaseLayerRead{}, run) - whole_first)};
        if constexpr (std::is_const_v<Element>) {
            static_cast<void>(body(run, place));
        } else {
            static_cast<void>(body(
                ::foundation::core::ViewMut<Element, typename Shard::tag_type>{
                    ::foundation::core::ParallelForFrameKeyDoor::key_(), run},
                place));
        }
    }

    // Runs on the thread of one worker.  The worker copies the body, so each
    // thread calls a body of its own, and runs each shard whose index is its
    // own modulo the thread count.
    template <typename Shards, typename Body, std::size_t... Is>
    static void run_worker_(::foundation::core::ErasedAddress frame) noexcept {
        worker_frame_<Shards, Body> const& source = frame.restore<worker_frame_<Shards, Body>>();
        Body body = *source.body;
        auto* const whole_first = first_of_whole_(*source.shards);
        ((Is % source.threads == source.worker ? run_shard_<Is, sizeof...(Is)>(*source.shards, body, whole_first)
                                               : static_cast<void>(0)),
         ...);
    }

    // Starts the workers with the options, and joins each one before it
    // returns.  The error of the door when no worker started, and then no
    // shard ran (the errors of fixy/os/ThreadTasks.h).
    template <typename Ctx, typename Shards, typename Body, std::size_t... Is>
        requires eff::CtxOwnsCapability<Ctx, eff::Effect::Bg>
    [[nodiscard]] static ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
    run_shards_(Ctx const&, Shards& shards, Body const& body, std::size_t threads,
                ::fixy::thread::ThreadOptions const& options, std::index_sequence<Is...>) noexcept {
        std::array<worker_frame_<Shards, Body>, sizeof...(Is)> frames{};
        ::fixy::thread::detail::thread_task tasks[sizeof...(Is)]{};
        // The caller gives at most one thread for each slot, so the window of
        // the started tasks fits the array, and the loop writes inside it.
        ::foundation::core::View<::fixy::thread::detail::thread_task> const started =
            ::foundation::core::detail::view_of_array_(tasks).window(0, threads).expect("the threads fit the tasks");
        for (std::size_t worker = 0; worker < threads; ++worker) {
            frames[worker] = worker_frame_<Shards, Body>{&shards, &body, worker, threads};
            tasks[worker] = ::fixy::thread::detail::thread_task{::foundation::core::mint_erased_address(frames[worker]),
                                                                &run_worker_<Shards, Body, Is...>};
        }
        return ::fixy::thread::detail::run_thread_tasks(started, options);
    }

    // Runs each shard on the calling thread, in shard order.
    template <typename Shards, typename Body, std::size_t... Is>
    static void run_inline_(Shards& shards, Body& body, std::index_sequence<Is...>) noexcept {
        auto* const whole_first = first_of_whole_(shards);
        (run_shard_<Is, sizeof...(Is)>(shards, body, whole_first), ...);
    }

    // The body of the two mints.  The workers get the options.
    template <BodyCoupling Coupling, std::size_t N, typename Ctx, typename T, typename Whole, typename Brand,
              typename Body>
        requires CtxFitsParallelFor<N, Ctx, T, Whole, Brand, Body>
    [[nodiscard]] static ::fixy::OwnedRegion<T, Whole, Brand>
    run_(Ctx const& ctx, ::fixy::concurrent::WorkBudget budget, ::fixy::OwnedRegion<T, Whole, Brand>&& region,
         Body& body, ::fixy::thread::ThreadOptions const& options) noexcept {
        constexpr ::foundation::closure::ClosureVerdict verdict =
            ::foundation::closure::check_closure(^^Body, parallel_for_body_policy, "mint_parallel_for");
        static_assert(verdict.is_admitted(), verdict);
        auto parts = ::fixy::mint_split<N>(std::move(region));

        if constexpr (N == 1) {
            // One shard runs on the calling thread, whatever the budget.  It
            // has no other shard to wait on.
            static_cast<void>(ctx);
            static_cast<void>(budget);
            static_cast<void>(options);
            run_inline_(parts.shards, body, std::make_index_sequence<1>{});
        } else if constexpr (Coupling == BodyCoupling::WaitOnEachOther) {
            // One thread for each shard, for each budget and each host, and
            // no run on the calling thread (design note 5).
            const std::size_t threads =
                choose_parallel_for_threads(Coupling, ::fixy::concurrent::ParallelismRule::host_facts(), budget, N);
            run_shards_(ctx, parts.shards, body, threads, options, std::make_index_sequence<N>{})
                .expect("mint_parallel_for: no worker of the shards started.  The shards wait on each other, so they "
                        "cannot run one after another on the calling thread");
        } else {
            // A sequential decision, or a parallel one whose workers did
            // not start, runs every shard on the calling thread (design
            // note 5).
            const std::size_t threads =
                choose_parallel_for_threads(Coupling, ::fixy::concurrent::ParallelismRule::host_facts(), budget, N);
            const bool have_workers_run =
                threads > 1
                && run_shards_(ctx, parts.shards, body, threads, options, std::make_index_sequence<N>{}).is_ok();
            if (!have_workers_run) {
                run_inline_(parts.shards, body, std::make_index_sequence<N>{});
            }
        }

        // Design note 3: every shard is surrendered here, and their Slice
        // permissions are what reissue the parent's.  The receipt the split
        // wrote is surrendered with them, so this rebuild is the one that
        // split authorized and there is no second one.
        return ::fixy::OwnedRegion<T, Whole, Brand>::recombine(std::move(parts.witness), std::move(parts.shards));
    }
};

// The call returns once every shard has run its body, and the region it
// hands back is recombined from the shards.  The coupling states whether a
// shard can wait on another shard, and the budget states the bytes the
// bodies touch together.  choose_parallel_for_threads gives the thread count
// from the two, per design note 4.  The workers get the default options of
// the thread door.
template <BodyCoupling Coupling, std::size_t N, typename Ctx, typename T, typename Whole, typename Brand, typename Body>
    requires CtxFitsParallelFor<N, Ctx, T, Whole, Brand, Body>
[[nodiscard]] ::fixy::OwnedRegion<T, Whole, Brand>
mint_parallel_for(Ctx const& ctx, ::fixy::concurrent::WorkBudget budget, ::fixy::OwnedRegion<T, Whole, Brand>&& region,
                  Body body) noexcept {
    // The constraint on the declaration reads the context.  The runner
    // reads it too, and asks for the background effect again before it
    // starts a thread.
    return ParallelForRunner::run_<Coupling, N>(ctx, budget, std::move(region), body, ::fixy::thread::ThreadOptions{});
}

// mint_parallel_for with the options of its worker threads, for a test
// (design note 5).  The test context is the authority of the options.
template <BodyCoupling Coupling, std::size_t N, typename Ctx, typename TestCtx, typename T, typename Whole,
          typename Brand, typename Body>
    requires CtxFitsParallelForWithThreadOptions<N, Ctx, TestCtx, T, Whole, Brand, Body>
[[nodiscard]] ::fixy::OwnedRegion<T, Whole, Brand>
mint_parallel_for_with_thread_options(Ctx const& ctx, TestCtx const& test, ::fixy::concurrent::WorkBudget budget,
                                      ::fixy::OwnedRegion<T, Whole, Brand>&& region, Body body,
                                      ::fixy::thread::ThreadOptions const& options) noexcept {
    static_cast<void>(test);
    return ParallelForRunner::run_<Coupling, N>(ctx, budget, std::move(region), body, options);
}

}  // namespace fixy::spawn
