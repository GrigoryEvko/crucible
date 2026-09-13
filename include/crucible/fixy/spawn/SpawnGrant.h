#pragma once

// Five grants that put a non-default spawn engagement in the type.  Each one
// routes to the Protocol axis, because a spawn engagement is itself a
// parent-child protocol, the same shape a binary-session grant declares.
//
// A rationale rides as a fixed-string non-type template parameter rather than
// a free type parameter.  Two sites with different strings are then distinct
// types, so one grep over a grant name enumerates every justification in the
// codebase.  The emptiness check is a size comparison the grant runs on
// itself, so an empty literal is rejected at instantiation, before any
// consumer of the grant sees it.

#include <crucible/Platform.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/grant/Ctrl.h>
#include <crucible/fixy/spawn/JoinPolicy.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::spawn::grant {

// The same parameter type the grants take, re-exported so a caller writes a
// grant here without reaching for the header that declares it.
template <std::size_t N>
using rationale = ::crucible::fixy::grant::ctrl::rationale<N>;

// The size of a fixed-string NTTP counts the trailing NUL, so an empty
// rationale has size 1.
template <::crucible::fixy::grant::ctrl::rationale Reason>
inline constexpr bool rationale_nonempty_v = (Reason.size() > 1);

template <::crucible::fixy::grant::ctrl::rationale Rationale>
struct detach_with final : ::crucible::fixy::grant::grant_base {
    static_assert(rationale_nonempty_v<Rationale>, "grant::detach_with<\"\"> rejected — Rationale must be "
                                                   "non-empty.  Every detach() audit-trail must declare its "
                                                   "non-engagement reason in the type so `grep \"detach_with<\"`"
                                                   " enumerates every legitimate detach across the codebase.  "
                                                   "Replace the empty literal with a descriptive justification "
                                                   "(e.g., \"logger drain outlives container\").");
    static constexpr ::crucible::fixy::grant::ctrl::rationale reason = Rationale;
};

template <::crucible::fixy::grant::ctrl::rationale Rationale>
struct syscall_only final : ::crucible::fixy::grant::grant_base {
    static_assert(rationale_nonempty_v<Rationale>, "grant::syscall_only<\"\"> rejected — Rationale must be "
                                                   "non-empty.  Raw clone(2) bypasses libc thread machinery "
                                                   "and is reserved for perf/bpf/cog loaders that genuinely "
                                                   "need CLONE_VM / CLONE_THREAD / CLONE_FILES semantics; the "
                                                   "audit trail of which loader and why is load-bearing.");
    static constexpr ::crucible::fixy::grant::ctrl::rationale reason = Rationale;
};

template <::crucible::fixy::grant::ctrl::rationale Rationale>
struct subprocess final : ::crucible::fixy::grant::grant_base {
    static_assert(rationale_nonempty_v<Rationale>, "grant::subprocess<\"\"> rejected — Rationale must be "
                                                   "non-empty.  fork(2) / posix_spawn(3) creates a separate "
                                                   "process image that escapes the entire fixy:: type system; "
                                                   "the script-side opt-in CRUCIBLE_SPAWN_ALLOW_PROCESS "
                                                   "guards the call site, this in-type grant complements it "
                                                   "with the audit-trail rationale.");
    static constexpr ::crucible::fixy::grant::ctrl::rationale reason = Rationale;
};

// The last two grants carry a type rather than a rationale.  They thread the
// parent identity and the execution context through the grant pack, so the
// coherence check below reads them without the call site retyping them.
template <typename ParentTag>
struct fork_parent final : ::crucible::fixy::grant::grant_base {
    using parent_tag = ParentTag;
};

template <typename Ctx>
struct exec_ctx final : ::crucible::fixy::grant::grant_base {
    using ctx_type = Ctx;
};

// The coherence check asks whether a pack holds a given grant at any
// rationale, which is a class-template match rather than a type comparison.

namespace detail {

template <typename G>
struct is_detach_with : std::false_type {};
template <::crucible::fixy::grant::ctrl::rationale R>
struct is_detach_with<detach_with<R>> : std::true_type {};

template <typename G>
struct is_syscall_only : std::false_type {};
template <::crucible::fixy::grant::ctrl::rationale R>
struct is_syscall_only<syscall_only<R>> : std::true_type {};

template <typename G>
struct is_subprocess : std::false_type {};
template <::crucible::fixy::grant::ctrl::rationale R>
struct is_subprocess<subprocess<R>> : std::true_type {};

template <template <typename> class Pred, typename... Grants>
inline constexpr bool any_of_v = (Pred<Grants>::value || ...);

}  // namespace detail

// One predicate per family rather than a template-template dispatcher.  The
// dispatcher would be shorter and would report a mismatch far less clearly.
template <typename... Grants>
inline constexpr bool has_detach_with_v = detail::any_of_v<detail::is_detach_with, Grants...>;

template <typename... Grants>
inline constexpr bool has_syscall_only_v = detail::any_of_v<detail::is_syscall_only, Grants...>;

template <typename... Grants>
inline constexpr bool has_subprocess_v = detail::any_of_v<detail::is_subprocess, Grants...>;

}  // namespace crucible::fixy::spawn::grant

// A which_dim specialization must appear syntactically inside namespace
// crucible::fixy::grant.  A nested namespace does not satisfy that rule.

namespace crucible::fixy::grant {

template <::crucible::fixy::grant::ctrl::rationale R>
struct which_dim<::crucible::fixy::spawn::grant::detach_with<R>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

template <::crucible::fixy::grant::ctrl::rationale R>
struct which_dim<::crucible::fixy::spawn::grant::syscall_only<R>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

template <::crucible::fixy::grant::ctrl::rationale R>
struct which_dim<::crucible::fixy::spawn::grant::subprocess<R>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

template <typename ParentTag>
struct which_dim<::crucible::fixy::spawn::grant::fork_parent<ParentTag>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

template <typename Ctx>
struct which_dim<::crucible::fixy::spawn::grant::exec_ctx<Ctx>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

}  // namespace crucible::fixy::grant

namespace crucible::fixy::spawn {

namespace join = ::crucible::fixy::spawn::join;

// The pack must carry a grant of the family the chosen mechanism demands.
// The two joining mechanisms demand nothing, because choosing the tag is
// itself the acknowledgement.
template <typename Mechanism, typename... Grants>
concept JoinPolicyGrantsCoherent =
    join::IsJoinMechanismTag<Mechanism>
    && (std::is_same_v<Mechanism, join::AutoJoin>  // default — no grant required
        || std::is_same_v<Mechanism, join::ManualJoin>  // tag-acknowledged join site
        || (std::is_same_v<Mechanism, join::Detached>
            && grant::detail::any_of_v<grant::detail::is_detach_with, Grants...>)
        || (std::is_same_v<Mechanism, join::Cloned>
            && grant::detail::any_of_v<grant::detail::is_syscall_only, Grants...>)
        || (std::is_same_v<Mechanism, join::Forked> && grant::detail::any_of_v<grant::detail::is_subprocess, Grants...>)
        || (std::is_same_v<Mechanism, join::PosixSpawn>
            && grant::detail::any_of_v<grant::detail::is_subprocess, Grants...>));

}  // namespace crucible::fixy::spawn

namespace crucible::fixy::spawn::grant::detail::v204_self_test {

namespace dim = ::crucible::fixy::dim;
namespace join = ::crucible::fixy::spawn::join;
namespace fg = ::crucible::fixy::grant;
namespace ctrl = ::crucible::fixy::grant::ctrl;

static_assert(rationale_nonempty_v<ctrl::rationale{"x"}>);
static_assert(rationale_nonempty_v<ctrl::rationale{"reason"}>);
static_assert(!rationale_nonempty_v<ctrl::rationale{""}>);

static_assert(std::is_base_of_v<fg::grant_base, detach_with<ctrl::rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, syscall_only<ctrl::rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, subprocess<ctrl::rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, fork_parent<struct dummy_parent_tag>>);
static_assert(std::is_base_of_v<fg::grant_base, exec_ctx<struct dummy_ctx>>);

static_assert(fg::which_dim_v<detach_with<ctrl::rationale{"x"}>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<syscall_only<ctrl::rationale{"x"}>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<subprocess<ctrl::rationale{"x"}>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<fork_parent<struct dummy_parent_tag2>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<exec_ctx<struct dummy_ctx2>> == dim::DimensionAxis::Protocol);

static_assert(std::is_empty_v<detach_with<ctrl::rationale{"x"}>>);
static_assert(std::is_empty_v<syscall_only<ctrl::rationale{"x"}>>);
static_assert(std::is_empty_v<subprocess<ctrl::rationale{"x"}>>);
static_assert(std::is_empty_v<fork_parent<struct dummy3>>);
static_assert(std::is_empty_v<exec_ctx<struct dummy4>>);
static_assert(sizeof(detach_with<ctrl::rationale{"x"}>) == 1);

static_assert(detach_with<ctrl::rationale{"audit"}>::reason.size() == 6);  // "audit" + NUL

static_assert(!std::is_same_v<detach_with<ctrl::rationale{"reason_a"}>, detach_with<ctrl::rationale{"reason_b"}>>);

static_assert(any_of_v<is_detach_with, detach_with<ctrl::rationale{"x"}>>);
static_assert(!any_of_v<is_detach_with, syscall_only<ctrl::rationale{"x"}>>);
static_assert(any_of_v<is_detach_with, syscall_only<ctrl::rationale{"x"}>,
                       detach_with<ctrl::rationale{"y"}>>);  // anywhere in pack
static_assert(!any_of_v<is_detach_with>);  // empty pack

static_assert(::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::AutoJoin>);
static_assert(::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::ManualJoin>);
static_assert(::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::AutoJoin,
                                                                detach_with<ctrl::rationale{"unused but allowed"}>>);

static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Detached>);
static_assert(::crucible::fixy::spawn::JoinPolicyGrantsCoherent<
              join::Detached, detach_with<ctrl::rationale{"logger drain outlives container"}>>);
static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Detached,
                                                                 subprocess<ctrl::rationale{"wrong grant family"}>>);

static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Cloned>);
static_assert(::crucible::fixy::spawn::JoinPolicyGrantsCoherent<
              join::Cloned, syscall_only<ctrl::rationale{"perf bpf loader needs CLONE_VM"}>>);

static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Forked>);
static_assert(::crucible::fixy::spawn::JoinPolicyGrantsCoherent<
              join::Forked, subprocess<ctrl::rationale{"CLI launcher fork-then-exec"}>>);

static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::PosixSpawn>);
static_assert(::crucible::fixy::spawn::JoinPolicyGrantsCoherent<
              join::PosixSpawn, subprocess<ctrl::rationale{"test-harness fork-exec helper"}>>);

static_assert(
    !::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Detached, syscall_only<ctrl::rationale{"wrong family"}>>);
static_assert(
    !::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Cloned, detach_with<ctrl::rationale{"wrong family"}>>);
static_assert(
    !::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Forked, detach_with<ctrl::rationale{"wrong family"}>>);

static_assert(
    ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Detached, fork_parent<struct dummy_pt>,
                                                      detach_with<ctrl::rationale{"r"}>, exec_ctx<struct dummy_cx>>);

static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<int>);

}  // namespace crucible::fixy::spawn::grant::detail::v204_self_test
