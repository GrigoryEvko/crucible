#pragma once

// The tie between an implementation, given as one local type per role,
// and the global type those roles are meant to be playing out.
//
// Two conditions make the tie hold.  The implementation covers exactly
// the roles the global type names, no more and no fewer.  And each
// role's local type is a subtype of what the global type projects onto
// that role.
//
// The reason to establish it is that properties proved of the global
// type then carry to the implementation without being re-established
// there.  Write the global type once, settle its safety and liveness
// once, and every implementation that associates with it inherits the
// result.
//
// The second condition admits subtyping rather than demanding equality,
// and that is not a convenience.  Requiring each local type to equal
// its projection gives an invariant that a protocol step can break: a
// context that matched exactly before a reduction need not match
// exactly after one.  Allowing the local type to refine its projection
// gives an invariant that survives reduction, which is the only kind
// worth carrying properties across.

#include <crucible/Platform.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstddef>
#include <type_traits>

namespace crucible::safety::proto {

// A role list holds each role at most once, so comparing two of them
// is set comparison and the order they were built in does not matter.

namespace detail::assoc {

template <typename RL1, typename RL2>
struct role_list_subset;

template <typename... Rs1, typename RL2>
struct role_list_subset<RoleList<Rs1...>, RL2>
    : std::bool_constant<(detail::global::contains_role_v<Rs1, RL2> && ...)> {};

}  // namespace detail::assoc

template <typename RL1, typename RL2>
inline constexpr bool role_list_subset_v = detail::assoc::role_list_subset<RL1, RL2>::value;

template <typename RL1, typename RL2>
inline constexpr bool role_lists_equal_as_sets_v = role_list_subset_v<RL1, RL2> && role_list_subset_v<RL2, RL1>;

namespace detail::assoc {

template <typename ΓEntries, typename SessionTag>
struct collect_roles_for_session;

template <typename SessionTag>
struct collect_roles_for_session<Context<>, SessionTag> {
    using type = EmptyRoleList;
};

template <typename SessionTag, typename R, typename T, typename... Rest>
struct collect_roles_for_session<Context<Entry<SessionTag, R, T>, Rest...>, SessionTag> {
    using rest_type = typename collect_roles_for_session<Context<Rest...>, SessionTag>::type;
    using type = insert_unique_t<R, rest_type>;
};

// This overlaps the one above and is the looser of the two, so it
// takes only the entries whose session tag did not match.
template <typename S, typename R, typename T, typename... Rest, typename SessionTag>
struct collect_roles_for_session<Context<Entry<S, R, T>, Rest...>, SessionTag> {
    using type = typename collect_roles_for_session<Context<Rest...>, SessionTag>::type;
};

}  // namespace detail::assoc

template <typename Γ, typename SessionTag>
using domain_roles_for_session_t = typename detail::assoc::collect_roles_for_session<Γ, SessionTag>::type;

template <typename Γ, typename G, typename SessionTag>
inline constexpr bool domain_matches_v =
    role_lists_equal_as_sets_v<domain_roles_for_session_t<Γ, SessionTag>, roles_of_t<G>>;

// The per-role check has to stay behind the domain check.  Looking up
// a role the context does not hold is a hard error rather than a failed
// substitution, so running both checks together would report a missing
// entry instead of reporting that the domains disagree.

namespace detail::assoc {

template <bool DomainOK, typename Γ, typename G, typename SessionTag, typename RolesPack>
struct gated_refine_check : std::false_type {};

template <typename Γ, typename G, typename SessionTag, typename... Rs>
struct gated_refine_check<true, Γ, G, SessionTag, RoleList<Rs...>>
    : std::bool_constant<(is_subtype_sync_v<lookup_context_t<Γ, SessionTag, Rs>, project_t<G, Rs>> && ...)> {};

}  // namespace detail::assoc

template <typename Γ, typename G, typename SessionTag>
inline constexpr bool all_entries_refine_projection_v =
    detail::assoc::gated_refine_check<domain_matches_v<Γ, G, SessionTag>, Γ, G, SessionTag, roles_of_t<G>>::value;

template <typename Γ, typename G, typename SessionTag>
inline constexpr bool is_associated_v =
    domain_matches_v<Γ, G, SessionTag> && all_entries_refine_projection_v<Γ, G, SessionTag>;

// One entry per role, each holding that role's projection unrefined.
// Since a local type is a subtype of itself, this context associates
// with its global type for free, which makes it the place to start from
// when writing an implementation.

namespace detail::assoc {

template <typename RolesPack, typename G, typename SessionTag>
struct project_to_context;

template <typename... Rs, typename G, typename SessionTag>
struct project_to_context<RoleList<Rs...>, G, SessionTag> {
    using type = Context<Entry<SessionTag, Rs, project_t<G, Rs>>...>;
};

}  // namespace detail::assoc

template <typename G, typename SessionTag>
using projected_context_t = typename detail::assoc::project_to_context<roles_of_t<G>, G, SessionTag>::type;

template <typename Γ, typename G, typename SessionTag>
concept AssociatedWith = is_associated_v<Γ, G, SessionTag>;

template <typename Γ, typename G, typename SessionTag>
consteval void assert_associated() noexcept {
    static_assert(domain_matches_v<Γ, G, SessionTag>, "crucible::session::diagnostic [Association_Domain_Mismatch]: "
                                                      "assert_associated: condition (1) fails — Γ's domain (for the "
                                                      "given SessionTag) does not match roles_of_t<G>.  Every role "
                                                      "of G must have a corresponding Entry<SessionTag, role, "
                                                      "local_type> in Γ, and Γ must not have EXTRA entries for this "
                                                      "session beyond G's roles.  Common causes: missing an entry "
                                                      "for a participating role; added an entry for a role not in "
                                                      "G; wrong SessionTag.");

    static_assert(all_entries_refine_projection_v<Γ, G, SessionTag>,
                  "crucible::session::diagnostic [SubtypeMismatch]: "
                  "assert_associated: condition (2) fails — at least one Γ "
                  "entry's local_type is NOT a synchronous subtype of its "
                  "projection G ↾ role.  The subtype rules are: Send payload "
                  "covariant + continuation "
                  "covariant; Recv payload contravariant + continuation "
                  "covariant; Select narrows (fewer branches is a subtype); "
                  "Offer widens (more branches is a subtype); Loop bodies "
                  "related coinductively; Stop is bottom.");
}

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::assoc::assoc_self_test {

struct My2PC {};
struct OtherSession {};
struct Coord {};
struct Follower {};
struct Stranger {};

struct Prepare {};
struct Vote {};
struct Commit {};
struct Abort {};

using G_2PC = Transmission<
    Coord, Follower, Prepare,
    Transmission<Follower, Coord, Vote, Choice<Coord, Follower, BranchG<Commit, End_G>, BranchG<Abort, End_G>>>>;

static_assert(is_global_well_formed_v<G_2PC>);
static_assert(roles_of_t<G_2PC>::size == 2);

using RL_CF = RoleList<Coord, Follower>;
using RL_FC = RoleList<Follower, Coord>;
using RL_C = RoleList<Coord>;
using RL_CFS = RoleList<Coord, Follower, Stranger>;
using RL_e = EmptyRoleList;

static_assert(role_list_subset_v<RL_e, RL_CF>);
static_assert(role_list_subset_v<RL_C, RL_CF>);
static_assert(role_list_subset_v<RL_CF, RL_CF>);
static_assert(role_list_subset_v<RL_CF, RL_FC>);
static_assert(!role_list_subset_v<RL_CFS, RL_CF>);
static_assert(!role_list_subset_v<RL_CF, RL_C>);

static_assert(role_lists_equal_as_sets_v<RL_CF, RL_FC>);
static_assert(role_lists_equal_as_sets_v<RL_e, RL_e>);
static_assert(!role_lists_equal_as_sets_v<RL_CF, RL_C>);
static_assert(!role_lists_equal_as_sets_v<RL_CF, RL_CFS>);

using ReflexiveΔ = projected_context_t<G_2PC, My2PC>;

static_assert(role_lists_equal_as_sets_v<domain_roles_for_session_t<ReflexiveΔ, My2PC>, RL_CF>);

static_assert(role_lists_equal_as_sets_v<domain_roles_for_session_t<ReflexiveΔ, OtherSession>, RL_e>);

using ΔMultiSession = Context<Entry<My2PC, Coord, project_t<G_2PC, Coord>>,
                              Entry<My2PC, Follower, project_t<G_2PC, Follower>>, Entry<OtherSession, Coord, End>>;
static_assert(role_lists_equal_as_sets_v<domain_roles_for_session_t<ΔMultiSession, My2PC>, RL_CF>);
static_assert(role_lists_equal_as_sets_v<domain_roles_for_session_t<ΔMultiSession, OtherSession>, RL_C>);

static_assert(domain_matches_v<ReflexiveΔ, G_2PC, My2PC>);
static_assert(all_entries_refine_projection_v<ReflexiveΔ, G_2PC, My2PC>);
static_assert(is_associated_v<ReflexiveΔ, G_2PC, My2PC>);

// Projecting onto the coordinator gives a choice between committing
// and aborting.  This implementation keeps only the commit branch.
using RefinedCoord = Send<Prepare, Recv<Vote, Select<Send<Commit, End>>>>;

static_assert(is_subtype_sync_v<RefinedCoord, project_t<G_2PC, Coord>>);

using ΔRefined = Context<Entry<My2PC, Coord, RefinedCoord>, Entry<My2PC, Follower, project_t<G_2PC, Follower>>>;

static_assert(is_associated_v<ΔRefined, G_2PC, My2PC>);

// Refining the other way, into a wider choice, does not associate.

using WidenedCoord =
    Send<Prepare,
         Recv<Vote, Select<Send<Commit, End>, Send<Abort, End>, Send<Commit, End>>>>;  // duplicate branch = wider

using ΔWidened = Context<Entry<My2PC, Coord, WidenedCoord>, Entry<My2PC, Follower, project_t<G_2PC, Follower>>>;

static_assert(!is_subtype_sync_v<WidenedCoord, project_t<G_2PC, Coord>>);
static_assert(!all_entries_refine_projection_v<ΔWidened, G_2PC, My2PC>);
static_assert(!is_associated_v<ΔWidened, G_2PC, My2PC>);

using ΔMissing = Context<Entry<My2PC, Coord, project_t<G_2PC, Coord>>>;

static_assert(!domain_matches_v<ΔMissing, G_2PC, My2PC>);
// The per-role check answers without looking up the role the context
// is missing, which is what the gate above buys.
static_assert(!all_entries_refine_projection_v<ΔMissing, G_2PC, My2PC>);
static_assert(!is_associated_v<ΔMissing, G_2PC, My2PC>);

using ΔExtra = Context<Entry<My2PC, Coord, project_t<G_2PC, Coord>>, Entry<My2PC, Follower, project_t<G_2PC, Follower>>,
                       Entry<My2PC, Stranger, End>>;

static_assert(!domain_matches_v<ΔExtra, G_2PC, My2PC>);
static_assert(!is_associated_v<ΔExtra, G_2PC, My2PC>);

using ΔWrongSession = Context<Entry<OtherSession, Coord, project_t<G_2PC, Coord>>,
                              Entry<OtherSession, Follower, project_t<G_2PC, Follower>>>;

static_assert(role_lists_equal_as_sets_v<domain_roles_for_session_t<ΔWrongSession, My2PC>, RL_e>);
static_assert(!is_associated_v<ΔWrongSession, G_2PC, My2PC>);
static_assert(is_associated_v<ΔWrongSession, G_2PC, OtherSession>);

static_assert(is_associated_v<ΔMultiSession, G_2PC, My2PC>);

template <typename Γ, typename G, typename SessionTag>
    requires AssociatedWith<Γ, G, SessionTag>
consteval bool requires_associated() {
    return true;
}

static_assert(requires_associated<ReflexiveΔ, G_2PC, My2PC>());
static_assert(requires_associated<ΔRefined, G_2PC, My2PC>());

consteval bool check_assert_associated() {
    assert_associated<ReflexiveΔ, G_2PC, My2PC>();
    assert_associated<ΔRefined, G_2PC, My2PC>();
    return true;
}
static_assert(check_assert_associated());

// A projected context associates with its own global type, whatever
// shape that global type has.

struct Alice {};
struct Bob {};
struct Ping {};

using G_bin = Transmission<Alice, Bob, Ping, End_G>;
static_assert(is_associated_v<projected_context_t<G_bin, My2PC>, G_bin, My2PC>);

using G_loop = Rec_G<Transmission<Alice, Bob, Ping, Var_G>>;
static_assert(is_associated_v<projected_context_t<G_loop, My2PC>, G_loop, My2PC>);

using G_empty = End_G;
static_assert(is_associated_v<projected_context_t<G_empty, My2PC>, G_empty, My2PC>);
static_assert(std::is_same_v<projected_context_t<G_empty, My2PC>, EmptyContext>);

}  // namespace detail::assoc::assoc_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
