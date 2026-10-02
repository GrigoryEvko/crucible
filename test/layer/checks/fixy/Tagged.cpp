// The compile-time checks of fixy/Tagged.h.

#include <fixy/Tagged.h>

namespace fixy {

static_assert(sizeof(Tagged<int, tags::source::FromUser>) == sizeof(int));
static_assert(sizeof(Tagged<void*, tags::trust::Verified>) == sizeof(void*));
static_assert(sizeof(Tagged<long, tags::access::AppendOnly>) == sizeof(long));

namespace detail::tagged_self_test {

struct LookalikeTagged {
    using value_type = int;
    using tag_type = tags::source::FromUser;
};

using T_int_user = Tagged<int, tags::source::FromUser>;
using T_int_db = Tagged<int, tags::source::FromDb>;
using T_double_user = Tagged<double, tags::source::FromUser>;

static_assert(is_tagged_v<T_int_user>);
static_assert(is_tagged_v<T_int_db>);
static_assert(is_tagged_v<T_double_user>);
static_assert(is_tagged_v<T_int_user&>);
static_assert(is_tagged_v<T_int_user&&>);
static_assert(is_tagged_v<T_int_user const>);
static_assert(is_tagged_v<T_int_user const&>);
static_assert(is_tagged_v<T_int_user volatile>);
static_assert(!is_tagged_v<int>);
static_assert(!is_tagged_v<int*>);
static_assert(!is_tagged_v<T_int_user*>);
static_assert(!is_tagged_v<void>);
static_assert(!is_tagged_v<LookalikeTagged>);
static_assert(IsTagged<T_int_user>);
static_assert(IsTagged<T_int_db const&>);
static_assert(!IsTagged<int>);

}  // namespace detail::tagged_self_test

// The sentinel pair, rather than a production pair, is what makes the
// assertions below witness a structural property.  A production pair
// would only witness that the catalog has not yet grown that edge, and
// would red the day it does.
static_assert(RetagAllowed<tags::source::FromUser, tags::source::FromUser>,
              "RetagAllowed must admit identity (X -> X)");
static_assert(!RetagAllowed<detail::retag_sentinel::NeverFrom, detail::retag_sentinel::NeverTo>,
              "RetagAllowed MUST be fail-closed for any (From -> To) pair without an edge in "
              "fixy::tags::admitted_retags");
static_assert(retag_policy<tags::source::FromUser, tags::source::FromUser>::allowed,
              "retag_policy must agree with RetagAllowed on identity");
static_assert(!retag_policy<detail::retag_sentinel::NeverFrom, detail::retag_sentinel::NeverTo>::allowed,
              "retag_policy must agree with RetagAllowed on the sentinel pair");

// Which tags are earned, read off the catalog.  Each earned tag names a
// check, and the factory refuses it; each mintable tag names a source, a
// parameter of a pin family, or a tag that no edge enters.
static_assert(!MintableTag<tags::source::Sanitized> && !MintableTag<tags::source::IntegrityVerified>
                  && !MintableTag<tags::source::Loaded> && !MintableTag<tags::trust::Verified>
                  && !MintableTag<tags::trust::Tested> && !MintableTag<tags::trust::Assumed>
                  && !MintableTag<tags::vessel_trust::Validated>,
              "a tag that names a check must come from retag along its discharge edge, never from mint_tagged");
static_assert(MintableTag<tags::source::External> && MintableTag<tags::source::FromUser>
                  && MintableTag<tags::trust::Unverified> && MintableTag<tags::vessel_trust::FromPytorch>
                  && MintableTag<tags::source::CipherPath> && MintableTag<tags::access::RO>
                  && MintableTag<tags::version::V<3>>,
              "a tag that names where a value came from, or that no edge enters, is mintable");
static_assert(MintableTag<tags::source::X86Pinned> && MintableTag<tags::source::ArmPinned>
                  && MintableTag<tags::source::PortablePinned>,
              "an edge inside one pin family changes a parameter and records no check, so every pin is mintable");
static_assert(!std::is_default_constructible_v<Tagged<int, tags::trust::Verified>>
                  && std::is_default_constructible_v<Tagged<int, tags::source::FromUser>>,
              "an earned tag has no empty-slot door: a default value under it would claim a check that never ran");
static_assert(!std::is_trivially_copyable_v<Tagged<int, tags::source::Sanitized>>
                  && !std::is_trivially_copyable_v<Tagged<void*, tags::trust::Verified>>,
              "std::bit_cast must not build a value under an earned tag from bytes");
static_assert(std::is_trivially_copy_constructible_v<Tagged<int, tags::source::Sanitized>>
                  && std::is_trivially_move_constructible_v<Tagged<int, tags::source::Sanitized>>
                  && std::is_trivially_destructible_v<Tagged<int, tags::source::Sanitized>>,
              "the constructors stay trivial, so an earned value still passes in a register");
static_assert(std::is_trivially_copyable_v<Tagged<int, tags::source::External>>,
              "a tag that names a source keeps a trivially copyable wrapper for byte transport");
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Tagged<int, tags::source::Sanitized>>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<Tagged<int, tags::source::External>>,
              "the checked lifetime start refuses a Tagged over bytes");

// The catalog's discipline, derived from the namespace.  The count is
// the one place a new edge must be acknowledged by hand: an edge is
// admitted the moment it is declared, and this pin is what makes the
// declaration a reviewed two-place edit.
static_assert(admitted_retag_count == ::foundation::fail_closed::edge_count<^^tags::admitted_retags>(),
              "fixy::tags::admitted_retags holds a different number of edges than admitted_retag_count.  An edge "
              "was added or removed: review it as the security change it is, then write the new count in the "
              "initializer of admitted_retag_count.");
static_assert(::foundation::fail_closed::every_edge_is_admitted<^^tags::admitted_retags>(),
              "every edge declared in fixy::tags::admitted_retags must be admitted by the "
              "fail-closed check that reads the same namespace");
static_assert(::foundation::fail_closed::is_antisymmetric<^^tags::admitted_retags>(),
              "fixy::tags::admitted_retags is a one-way ratchet: no edge may have its inverse "
              "admitted.  An inverse edge would let a proof, a sanitize pass or an integrity check "
              "be erased by relabelling.");
static_assert(::foundation::fail_closed::is_intra_namespace<^^tags::admitted_retags>(),
              "fixy::tags::admitted_retags must not cross tag families: laundering across "
              "orthogonal axes (source::* to trust::*, source::* to access::*) is never safe");

}  // namespace fixy
