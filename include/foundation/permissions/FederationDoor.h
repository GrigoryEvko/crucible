#pragma once

// The federation door.  A token for a federation peer comes from one
// place: a handshake whose tag authenticates under the key of the local
// organization.  FederationAdmission holds that key and does the check.
// This header names the class, and fixy/Federation.h defines it, because
// the check needs fixy::Secret and fixy::siphash.
//
// The key below is narrower than perm_mint_key of
// foundation/permissions/Permission.h.  The only constructor that takes it
// builds a token whose tag is a FederatedPeer, so an admission cannot mint
// a token for any other region.
//
// C++ lets a file specialize a class template, or define one member of
// it for a new argument, and that definition gets the access of the
// friend.  A reflection check cannot refuse it: GCC gives the location
// of the primary template for an instance of a partial specialization,
// and a #line directive moves any location.  The rule
// home of the quarantine plugin refuses each definition
// and each specialization of FederationAdmission, and each definition of
// one of its members, in a file other than fixy/Federation.h
// (utils/tools/quarantine/home.cpp).  The plugin also refuses a #line
// directive in a file of the tree.

#include <foundation/Platform.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Instance.h>

namespace foundation::permissions {

namespace tag {

// A peer of another organization.  No mint makes a token for a peer
// from nothing.  The root mint refuses the tag, and the one constructor
// that builds the token takes federation_admission_key, which only an
// admission makes, after a handshake verifies.  A peer's authority is
// over the traffic with that peer, so the row is IO.
template <typename Org>
struct FederatedPeer {
    using org_type = Org;
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

}  // namespace tag

// True when Tag is a federation peer tag.  The test is a concept that
// reads the template of the tag by reflection, so no specialization can
// widen or narrow the set of tags that it matches.
template <typename Tag>
concept IsFederatedPeerTag = ::foundation::reflect::IsInstanceOf<Tag, ^^tag::FederatedPeer>;

template <typename Org>
class FederationAdmission;

// Both constructors are user-provided, for the reason perm_mint_key
// gives: a key with a trivial constructor is a type that std::bit_cast or
// std::start_lifetime_as builds with no access check.
class federation_admission_key {
    constexpr federation_admission_key() noexcept {}

public:
    constexpr federation_admission_key(const federation_admission_key&) noexcept {}

private:
    template <typename Org>
    friend class FederationAdmission;
};

}  // namespace foundation::permissions
