#pragma once

#include <crucible/sessions/SessionAssoc.h>

#include <cstddef>
#include <type_traits>

// A typing context is associated with a global type under one session
// tag when two conditions hold. The domain of the context, restricted
// to that session, is exactly the set of roles of the global type. And
// each entry is a synchronous subtype of the global type projected onto
// that entry's role. The two conditions are exported separately so a
// failure can be attributed to one of them.
namespace crucible::fixy::sess::assoc {

// Only the value form of the subset test is public. The class template
// behind it stays internal to the layer that defines it.
using ::crucible::safety::proto::role_list_subset_v;
using ::crucible::safety::proto::role_lists_equal_as_sets_v;

using ::crucible::safety::proto::domain_roles_for_session_t;

using ::crucible::safety::proto::domain_matches_v;
using ::crucible::safety::proto::all_entries_refine_projection_v;

using ::crucible::safety::proto::is_associated_v;
using ::crucible::safety::proto::AssociatedWith;
using ::crucible::safety::proto::assert_associated;

// The context this builds is associated with its global type for every
// well-formed global type and every session tag.
using ::crucible::safety::proto::projected_context_t;

}  // namespace crucible::fixy::sess::assoc

namespace crucible::fixy::sess::assoc::v059_self_test {

namespace proto = ::crucible::safety::proto;

struct MySess {};
struct OtherSess {};
struct Alice {};
struct Bob {};
struct Stranger {};
struct Ping {};

using G_bin = proto::Transmission<Alice, Bob, Ping, proto::End_G>;

using GammaRefl = projected_context_t<G_bin, MySess>;

static_assert(role_list_subset_v<proto::RoleList<Alice>, proto::RoleList<Alice, Bob>>
                  == proto::role_list_subset_v<proto::RoleList<Alice>, proto::RoleList<Alice, Bob>>,
              "role_list_subset_v must reach identically through fixy::");
static_assert(role_list_subset_v<proto::RoleList<Alice>, proto::RoleList<Alice, Bob>>,
              "{Alice} ⊆ {Alice, Bob} must hold (subset is reflexive on each Rs).");
static_assert(!role_list_subset_v<proto::RoleList<Stranger>, proto::RoleList<Alice, Bob>>,
              "Stranger ∉ {Alice, Bob} — subset must reject.");
static_assert(role_lists_equal_as_sets_v<proto::RoleList<Alice, Bob>, proto::RoleList<Bob, Alice>>,
              "Set equality is order-insensitive (Alice,Bob == Bob,Alice).");

static_assert(role_lists_equal_as_sets_v<domain_roles_for_session_t<GammaRefl, MySess>, proto::RoleList<Alice, Bob>>,
              "GammaRefl's domain (restricted to MySess) must be {Alice, Bob}.");
static_assert(std::is_same_v<domain_roles_for_session_t<GammaRefl, OtherSess>, proto::EmptyRoleList>,
              "GammaRefl's domain for OtherSess is empty (no entries tagged OtherSess).");

static_assert(domain_matches_v<GammaRefl, G_bin, MySess> == proto::domain_matches_v<GammaRefl, G_bin, MySess>,
              "domain_matches_v must reach identically through fixy::");
static_assert(all_entries_refine_projection_v<GammaRefl, G_bin, MySess>
                  == proto::all_entries_refine_projection_v<GammaRefl, G_bin, MySess>,
              "all_entries_refine_projection_v must reach identically.");
static_assert(domain_matches_v<GammaRefl, G_bin, MySess>);
static_assert(all_entries_refine_projection_v<GammaRefl, G_bin, MySess>);

static_assert(is_associated_v<GammaRefl, G_bin, MySess> == proto::is_associated_v<GammaRefl, G_bin, MySess>,
              "is_associated_v must reach identically through fixy::");
static_assert(is_associated_v<GammaRefl, G_bin, MySess>,
              "Reflexive association: projected_context_t<G, S> ⊑_s G always holds.");

template <typename G_arg, typename G_, typename S>
    requires AssociatedWith<G_arg, G_, S>
consteval bool requires_associated_witness() {
    return true;
}
static_assert(requires_associated_witness<GammaRefl, G_bin, MySess>());

consteval bool check_fixy_assert_associated() {
    assert_associated<GammaRefl, G_bin, MySess>();
    return true;
}
static_assert(check_fixy_assert_associated());

static_assert(std::is_same_v<projected_context_t<G_bin, MySess>, proto::projected_context_t<G_bin, MySess>>,
              "projected_context_t must reach identically through fixy::");

static_assert(std::is_same_v<projected_context_t<proto::End_G, MySess>, proto::EmptyContext>,
              "projected_context_t<End_G, S> must be EmptyContext (no roles).");

// The count is one per re-exported name.
constexpr int v059_surface_cardinality = 9;
static_assert(v059_surface_cardinality == 9, "the re-exported surface of fixy::sess::assoc has changed. Update the "
                                             "using-declarations and this count together.");

}  // namespace crucible::fixy::sess::assoc::v059_self_test

namespace crucible::fixy::sess::assoc {

// A static assertion can be discharged without ever instantiating an
// inline body. Naming the results in a real function puts every
// metafunction below through a full instantiation.
inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    struct S {};
    struct OS {};
    struct RA {};
    struct RB {};
    struct M {};

    using G = proto::Transmission<RA, RB, M, proto::End_G>;
    using Gamma = projected_context_t<G, S>;

    [[maybe_unused]] constexpr bool dom = domain_matches_v<Gamma, G, S>;
    [[maybe_unused]] constexpr bool refine = all_entries_refine_projection_v<Gamma, G, S>;
    [[maybe_unused]] constexpr bool assoc = is_associated_v<Gamma, G, S>;
    [[maybe_unused]] constexpr bool subset = role_list_subset_v<proto::RoleList<RA>, proto::RoleList<RA, RB>>;
    [[maybe_unused]] constexpr bool equal =
        role_lists_equal_as_sets_v<proto::RoleList<RA, RB>, proto::RoleList<RB, RA>>;
    using DomRoles = domain_roles_for_session_t<Gamma, S>;
    using EmptyDom = domain_roles_for_session_t<Gamma, OS>;
    [[maybe_unused]] constexpr bool dr_ok = role_lists_equal_as_sets_v<DomRoles, proto::RoleList<RA, RB>>;
    [[maybe_unused]] constexpr bool ed_ok = std::is_same_v<EmptyDom, proto::EmptyRoleList>;

    (void)dom;
    (void)refine;
    (void)assoc;
    (void)subset;
    (void)equal;
    (void)dr_ok;
    (void)ed_ok;
}

}  // namespace crucible::fixy::sess::assoc
