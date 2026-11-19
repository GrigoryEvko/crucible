#pragma once

// THE WALK OF A SCOPE: one walk of the members of a class or a namespace
// through std::meta::members_of, so each reader of a scope uses one loop.
//
//   find_member(scope, access, matches)    the first member that the access
//                                          context sees and for which matches
//                                          gives true, or a null reflection
//   member_named_in(scope, access, name)   the first member with the
//                                          identifier name, or a null
//                                          reflection
//
// A visitor that gives false for each member visits each member, so a reader
// that collects reads the scope through find_member too.  A namespace has no
// access control, so an access context sees each member of a namespace.
// Complexity: linear in the members of the scope.

#include <meta>

namespace foundation::reflect {

template <class Matches>
[[nodiscard]] consteval std::meta::info find_member(std::meta::info scope, std::meta::access_context access,
                                                    Matches&& matches) {
    for (const std::meta::info member : std::meta::members_of(scope, access)) {
        if (matches(member)) return member;
    }
    return std::meta::info{};
}

// The name is a text that compares with the identifier of a member, such as
// a string literal or the name that a caller gives on.
template <class Name>
[[nodiscard]] consteval std::meta::info member_named_in(std::meta::info scope, std::meta::access_context access,
                                                        Name const& name) {
    return find_member(scope, access, [&name](std::meta::info member) {
        return std::meta::has_identifier(member) && std::meta::identifier_of(member) == name;
    });
}

}  // namespace foundation::reflect
