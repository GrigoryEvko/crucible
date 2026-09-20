#pragma once

// Structured spawning: the mechanism a child is joined by, the rationale
// a non-joining mechanism has to state, and the two mints that fan work
// out and collect it again.
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
//     two roster checks sit beside them.
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
//  5. mint_spawn forwards to the spawning arm.  foundation ships two,
//     mint_permission_fork (one thread per child, needs Effect::Bg) and
//     mint_permission_fork_inline (bodies in child order, needs nothing),
//     and no cost model chooses between them.  The old choice came from
//     ctx_workbudget and parallelism_decision_for, neither of which is in
//     the new tree.  mint_spawn is the spawning arm; a caller that wants
//     the inline arm names it directly until a fixy-side cost model
//     exists to choose.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/OwnedRegion.h>
#include <fixy/Throws.h>
#include <fixy/atoms/Ctrl.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::spawn::join {

enum class JoinMechanism : std::uint8_t {
    AutoJoin = 0,
    ManualJoin = 1,
    Detached = 2,
    Cloned = 3,
    Forked = 4,
    PosixSpawn = 5,
};

inline constexpr std::size_t join_mechanism_count = std::meta::enumerators_of(^^JoinMechanism).size();

[[nodiscard]] consteval std::string_view name_of(JoinMechanism m) noexcept {
    switch (m) {
        case JoinMechanism::AutoJoin:
            return "AutoJoin";
        case JoinMechanism::ManualJoin:
            return "ManualJoin";
        case JoinMechanism::Detached:
            return "Detached";
        case JoinMechanism::Cloned:
            return "Cloned";
        case JoinMechanism::Forked:
            return "Forked";
        case JoinMechanism::PosixSpawn:
            return "PosixSpawn";
        default:
            return std::string_view{"<unknown JoinMechanism>"};
    }
}

// Each tag is final so no imposter can inherit from one and reach the
// identity allowlist below through a derived type.
struct AutoJoin final {
    static constexpr JoinMechanism mechanism = JoinMechanism::AutoJoin;
};
struct ManualJoin final {
    static constexpr JoinMechanism mechanism = JoinMechanism::ManualJoin;
};
struct Detached final {
    static constexpr JoinMechanism mechanism = JoinMechanism::Detached;
};
struct Cloned final {
    static constexpr JoinMechanism mechanism = JoinMechanism::Cloned;
};
struct Forked final {
    static constexpr JoinMechanism mechanism = JoinMechanism::Forked;
};
struct PosixSpawn final {
    static constexpr JoinMechanism mechanism = JoinMechanism::PosixSpawn;
};

// The gate is an identity allowlist rather than a structural check for a
// mechanism member. A structural check would admit any struct that declares
// the field, letting a caller substitute its own type for a declared tag.
template <typename T>
concept IsJoinMechanismTag = std::is_same_v<T, AutoJoin> || std::is_same_v<T, ManualJoin> || std::is_same_v<T, Detached>
                          || std::is_same_v<T, Cloned> || std::is_same_v<T, Forked> || std::is_same_v<T, PosixSpawn>;

template <typename T>
    requires IsJoinMechanismTag<T>
inline constexpr JoinMechanism mechanism_of_v = T::mechanism;

using Default = AutoJoin;

}  // namespace fixy::spawn::join

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

// The coherence check asks whether a pack holds a given atom at any
// rationale, which is a class-template match rather than a type compare.
namespace detail {

template <typename A>
struct is_detach_with : std::false_type {};
template <::fixy::atom::ctrl::rationale R>
struct is_detach_with<detach_with<R>> : std::true_type {};

template <typename A>
struct is_syscall_only : std::false_type {};
template <::fixy::atom::ctrl::rationale R>
struct is_syscall_only<syscall_only<R>> : std::true_type {};

template <typename A>
struct is_subprocess : std::false_type {};
template <::fixy::atom::ctrl::rationale R>
struct is_subprocess<subprocess<R>> : std::true_type {};

template <template <typename> class Pred, typename... Atoms>
inline constexpr bool any_of_v = (Pred<Atoms>::value || ...);

}  // namespace detail

// One predicate per family rather than a template-template dispatcher.
// The dispatcher would be shorter and would report a mismatch far less
// clearly.
template <typename... Atoms>
inline constexpr bool has_detach_with_v = detail::any_of_v<detail::is_detach_with, Atoms...>;

template <typename... Atoms>
inline constexpr bool has_syscall_only_v = detail::any_of_v<detail::is_syscall_only, Atoms...>;

template <typename... Atoms>
inline constexpr bool has_subprocess_v = detail::any_of_v<detail::is_subprocess, Atoms...>;

}  // namespace fixy::atom::spawn

namespace fixy::atom::detail {

// Every member is a parametric atom, so the namespace walk that catches an
// unrostered plain atom has nothing to read here; fixy/Atom.h says why
// beside that walk.  The roster still earns its place: the two checks
// below read it, and they are what says a rationale atom is an atom on the
// axis it claims.
using spawn_atom_roster = std::tuple<spawn::detach_with<"sample detach">, spawn::syscall_only<"sample clone">,
                                     spawn::subprocess<"sample fork">>;

}  // namespace fixy::atom::detail

namespace fixy::spawn {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;
namespace atom_spawn = ::fixy::atom::spawn;

// The pack must carry an atom of the family the chosen mechanism demands.
// The two joining mechanisms demand nothing, because choosing the tag is
// itself the acknowledgement.
template <typename Mechanism, typename... Atoms>
concept JoinPolicyGrantsCoherent =
    join::IsJoinMechanismTag<Mechanism>
    && (std::is_same_v<Mechanism, join::AutoJoin>  // default — no atom required
        || std::is_same_v<Mechanism, join::ManualJoin>  // tag-acknowledged join site
        || (std::is_same_v<Mechanism, join::Detached> && atom_spawn::has_detach_with_v<Atoms...>)
        || (std::is_same_v<Mechanism, join::Cloned> && atom_spawn::has_syscall_only_v<Atoms...>)
        || (std::is_same_v<Mechanism, join::Forked> && atom_spawn::has_subprocess_v<Atoms...>)
        || (std::is_same_v<Mechanism, join::PosixSpawn> && atom_spawn::has_subprocess_v<Atoms...>));

namespace detail {

template <typename Ctx, typename Parent, typename ChildrenTuple, typename CallablesTuple>
struct ctx_fits_spawn_helper : std::false_type {};

template <typename Ctx, typename Parent, typename... Children, typename... Callables>
struct ctx_fits_spawn_helper<Ctx, Parent, std::tuple<Children...>, std::tuple<Callables...>>
    : std::bool_constant<perm::CtxFitsPermissionFork<Ctx, Parent, Children...>
                         && perm::detail::permission_fork_ctx_callables_v<Ctx, std::tuple<Children...>,
                                                                          std::tuple<Callables...>>> {};

// True when no callable's type carries the throws atom anywhere in its
// type tree.  Read by the mint's body, per deviation 1.
template <typename... Callables>
inline constexpr bool no_callable_throws_v =
    !(::fixy::type_tree_contains_throws_v<std::decay_t<Callables>> || ...);

}  // namespace detail

// The two substrate gates are folded into one concept so the declaration
// below carries a single requires clause.
template <typename Ctx, typename Parent, typename ChildrenTuple, typename CallablesTuple>
concept CtxFitsSpawn = detail::ctx_fits_spawn_helper<Ctx, Parent, ChildrenTuple, CallablesTuple>::value;

// The call returns once every child has joined.
template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsSpawn<Ctx, Parent, std::tuple<Children...>, std::tuple<std::decay_t<Callables>...>>
[[nodiscard]] perm::Permission<Parent, Brand> mint_spawn(Ctx const& ctx, perm::Permission<Parent, Brand>&& parent,
                                                        Callables&&... callables) noexcept {
    // Deviation 1.  The clause above has already checked each callable's
    // noexcept specification through foundation's gate; this is the
    // structural half, which foundation cannot express because the atom is
    // a fixy name.
    static_assert(detail::no_callable_throws_v<Callables...>,
                  "mint_spawn: a callable's type carries fixy::atom::ctrl::throws. A noexcept declaration is "
                  "a promise the callable can still break, and a throw out of a child tears through the join "
                  "instead of unwinding it. Remove the atom from the callable's type, or do not spawn it.");
    return perm::mint_permission_fork<Children...>(ctx, std::move(parent), std::forward<Callables>(callables)...);
}

// The background capability is demanded even when N is one and no thread
// is spawned, so the contract does not change shape with N.
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

namespace detail {

// One thread per shard, each mutating its own tuple element.  The array of
// jthreads joins in its destructor, so every body has returned before this
// function does and the tuple is whole again for recombine.
template <typename Shards, typename Body, std::size_t... Is>
void run_shards_(Shards& shards, Body body, std::index_sequence<Is...>) noexcept {
    std::array<std::jthread, sizeof...(Is)> workers{
        std::jthread{[&shards, body](std::stop_token) mutable noexcept { body(std::get<Is>(shards)); }}...};
    (void)workers;
}

}  // namespace detail

// The call returns once every shard has run its body, and the region it
// hands back is recombined from the shards.
template <std::size_t N, typename Ctx, typename T, typename Whole, typename Brand, typename Body>
    requires CtxFitsParallelFor<N, Ctx, T, Whole, Brand, Body>
[[nodiscard]] ::fixy::OwnedRegion<T, Whole, Brand> mint_parallel_for(Ctx const& /*ctx*/,
                                                                     ::fixy::OwnedRegion<T, Whole, Brand>&& region,
                                                                     Body body) noexcept {
    // The context is read by the constraint on the declaration and nowhere
    // else.  The fan-out below is driven by N alone.
    auto parts = ::fixy::mint_split<N>(std::move(region));

    if constexpr (N == 1) {
        body(std::get<0>(parts.shards));
    } else {
        detail::run_shards_(parts.shards, body, std::make_index_sequence<N>{});
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
namespace join_ = ::fixy::spawn::join;
using ::fixy::atom::IsAtom;

// ── The mechanism universe ──────────────────────────────────────────

static_assert(join_::join_mechanism_count == 6, "fixy::spawn::join::JoinMechanism universe drifted from six "
                                                "{AutoJoin, ManualJoin, Detached, Cloned, Forked, PosixSpawn}. "
                                                "Adding a mechanism is append-only at the next free ordinal, "
                                                "because a stored cache slot keys on the ordinal. Update the "
                                                "cardinality sentinel, the tag struct and the name_of switch "
                                                "together.");

[[nodiscard]] consteval bool every_mechanism_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^join_::JoinMechanism));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (join_::name_of([:en:]) == std::string_view{"<unknown JoinMechanism>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_mechanism_has_name(), "fixy::spawn::join::name_of switch is missing an arm for at least "
                                          "one JoinMechanism enumerator. Add the arm, or the new mechanism "
                                          "leaks the '<unknown JoinMechanism>' sentinel into debug output.");

static_assert(join_::AutoJoin::mechanism == join_::JoinMechanism::AutoJoin);
static_assert(join_::ManualJoin::mechanism == join_::JoinMechanism::ManualJoin);
static_assert(join_::Detached::mechanism == join_::JoinMechanism::Detached);
static_assert(join_::Cloned::mechanism == join_::JoinMechanism::Cloned);
static_assert(join_::Forked::mechanism == join_::JoinMechanism::Forked);
static_assert(join_::PosixSpawn::mechanism == join_::JoinMechanism::PosixSpawn);

static_assert(join_::mechanism_of_v<join_::AutoJoin> == join_::JoinMechanism::AutoJoin);
static_assert(join_::mechanism_of_v<join_::PosixSpawn> == join_::JoinMechanism::PosixSpawn);

static_assert(join_::IsJoinMechanismTag<join_::AutoJoin>);
static_assert(join_::IsJoinMechanismTag<join_::ManualJoin>);
static_assert(join_::IsJoinMechanismTag<join_::Detached>);
static_assert(join_::IsJoinMechanismTag<join_::Cloned>);
static_assert(join_::IsJoinMechanismTag<join_::Forked>);
static_assert(join_::IsJoinMechanismTag<join_::PosixSpawn>);

static_assert(!join_::IsJoinMechanismTag<int>);
static_assert(!join_::IsJoinMechanismTag<void>);
static_assert(!join_::IsJoinMechanismTag<join_::JoinMechanism>);

// The allowlist is by identity, so a struct that declares the same member
// is still refused.  That is the cheat the comment beside it names.
struct MechanismImposter {
    static constexpr join_::JoinMechanism mechanism = join_::JoinMechanism::Detached;
};
static_assert(!join_::IsJoinMechanismTag<MechanismImposter>,
              "IsJoinMechanismTag must be an identity allowlist. A structural check would admit any struct "
              "that declares a mechanism member.");

static_assert(std::is_empty_v<join_::AutoJoin> && sizeof(join_::AutoJoin) == 1);
static_assert(std::is_empty_v<join_::PosixSpawn> && sizeof(join_::PosixSpawn) == 1);
static_assert(std::is_final_v<join_::AutoJoin>);
static_assert(std::is_final_v<join_::ManualJoin>);
static_assert(std::is_final_v<join_::Detached>);
static_assert(std::is_final_v<join_::Cloned>);
static_assert(std::is_final_v<join_::Forked>);
static_assert(std::is_final_v<join_::PosixSpawn>);
static_assert(!std::is_same_v<join_::AutoJoin, join_::ManualJoin>);
static_assert(!std::is_same_v<join_::Cloned, join_::Forked>);
static_assert(!std::is_same_v<join_::Forked, join_::PosixSpawn>);
static_assert(std::is_same_v<join_::Default, join_::AutoJoin>);

static_assert(join_::name_of(join_::JoinMechanism::AutoJoin) == "AutoJoin");
static_assert(join_::name_of(join_::JoinMechanism::PosixSpawn) == "PosixSpawn");

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

static_assert(::fixy::atom::detail::every_roster_member_is_atom_<::fixy::atom::detail::spawn_atom_roster>(),
              "fixy/os/Spawn.h: a member of spawn_atom_roster is not an atom.");
static_assert(::fixy::atom::detail::every_roster_member_on_axis_<::fixy::atom::detail::spawn_atom_roster,
                                                                 Axis::Protocol>(),
              "fixy/os/Spawn.h: every spawn rationale atom engages Axis::Protocol.");

static_assert(std::is_empty_v<atom_spawn::detach_with<"x">>);
static_assert(sizeof(atom_spawn::detach_with<"x">) == 1);
static_assert(atom_spawn::detach_with<"audit">::reason.size() == 6);  // "audit" plus the terminator

// The rationale is part of the type, so two reasons are two types.
static_assert(!std::is_same_v<atom_spawn::detach_with<"reason_a">, atom_spawn::detach_with<"reason_b">>);
static_assert(!std::is_same_v<atom_spawn::detach_with<"x">, atom_spawn::syscall_only<"x">>);

static_assert(atom_spawn::has_detach_with_v<atom_spawn::detach_with<"x">>);
static_assert(!atom_spawn::has_detach_with_v<atom_spawn::syscall_only<"x">>);
static_assert(atom_spawn::has_detach_with_v<atom_spawn::syscall_only<"x">, atom_spawn::detach_with<"y">>);
static_assert(!atom_spawn::has_detach_with_v<>);  // empty pack

// ── The coherence bridge ────────────────────────────────────────────

static_assert(JoinPolicyGrantsCoherent<join_::AutoJoin>);
static_assert(JoinPolicyGrantsCoherent<join_::ManualJoin>);
static_assert(JoinPolicyGrantsCoherent<join_::AutoJoin, atom_spawn::detach_with<"unused but allowed">>);

static_assert(!JoinPolicyGrantsCoherent<join_::Detached>);
static_assert(JoinPolicyGrantsCoherent<join_::Detached, atom_spawn::detach_with<"logger drain outlives container">>);
static_assert(!JoinPolicyGrantsCoherent<join_::Detached, atom_spawn::subprocess<"wrong atom family">>);

static_assert(!JoinPolicyGrantsCoherent<join_::Cloned>);
static_assert(JoinPolicyGrantsCoherent<join_::Cloned, atom_spawn::syscall_only<"perf bpf loader needs CLONE_VM">>);

static_assert(!JoinPolicyGrantsCoherent<join_::Forked>);
static_assert(JoinPolicyGrantsCoherent<join_::Forked, atom_spawn::subprocess<"CLI launcher fork-then-exec">>);

static_assert(!JoinPolicyGrantsCoherent<join_::PosixSpawn>);
static_assert(JoinPolicyGrantsCoherent<join_::PosixSpawn, atom_spawn::subprocess<"test-harness fork-exec helper">>);

static_assert(!JoinPolicyGrantsCoherent<join_::Detached, atom_spawn::syscall_only<"wrong family">>);
static_assert(!JoinPolicyGrantsCoherent<join_::Cloned, atom_spawn::detach_with<"wrong family">>);
static_assert(!JoinPolicyGrantsCoherent<join_::Forked, atom_spawn::detach_with<"wrong family">>);

// The right atom anywhere in the pack satisfies it.
static_assert(JoinPolicyGrantsCoherent<join_::Detached, atom_spawn::syscall_only<"a">,
                                       atom_spawn::detach_with<"r">, atom_spawn::subprocess<"b">>);

static_assert(!JoinPolicyGrantsCoherent<int>);

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
