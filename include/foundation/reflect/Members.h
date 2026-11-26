#pragma once

// Walk of a scope: one walk of the members of a class or a namespace
// through std::meta::members_of, so each reader of a scope uses one loop.
//
//   find_member(scope, access, matches)    the first member that the access
//                                          context sees and for which matches
//                                          gives true, or a null reflection
//   member_named_in(scope, access, name)   the first member with the
//                                          identifier name, or a null
//                                          reflection
//   has_identifier_named(entity, name)     true when the entity has the
//                                          identifier name, with no walk
//   find_parameter(function, matches)      the first parameter of the
//                                          function for which matches gives
//                                          true, or a null reflection
//   is_class_of_the_base_(type)            true when the class is declared
//                                          in the namespace foundation or
//                                          fixy, or in a namespace inside
//                                          them
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

// The name is a text that compares with the identifier of an entity, such as
// a string literal or the name that a caller gives on.  Complexity: linear in
// the length of the name.
template <class Name>
[[nodiscard]] consteval bool has_identifier_named(std::meta::info entity, Name const& name) {
    return std::meta::has_identifier(entity) && std::meta::identifier_of(entity) == name;
}

template <class Name>
[[nodiscard]] consteval std::meta::info member_named_in(std::meta::info scope, std::meta::access_context access,
                                                        Name const& name) {
    return find_member(scope, access, [&name](std::meta::info member) { return has_identifier_named(member, name); });
}

// True when the class of type is declared in the namespace foundation or
// fixy, or in a namespace inside them: the outermost scope of the class below
// the global namespace is one of the two.  The walk goes up through each
// enclosing class and function, so a local class and a closure of a function
// of the base are classes of the base too.  A specialization gives the scope
// of its template.  The walk stops at a scope with no parent: GCC gives the
// function main no parent, so a class of main is no class of the base.  The
// layer foundation declares no namespace fixy, so the test reads the name of
// the namespace.
//
// A gate can trust a mark of a class of the base, because the rules
// base-namespace and specialization-home keep the declarations of a file
// outside the base out of the two namespaces.  They admit two kinds: a
// declaration in a PROBE: opt-out region of a test, and a specialization of
// a template with the open mark and a row of its ledger.  base-namespace does
// not see a forward declaration of a class that a file of the base defines
// later, and the definition of the base then carries the marks.  Complexity:
// the depth of the scopes.
[[nodiscard]] consteval bool is_class_of_the_base_(std::meta::info type) {
    std::meta::info outermost = std::meta::dealias(type);
    while (std::meta::has_parent(outermost) && std::meta::parent_of(outermost) != ^^::) {
        outermost = std::meta::parent_of(outermost);
    }
    return std::meta::is_namespace(outermost)
        && (has_identifier_named(outermost, "foundation") || has_identifier_named(outermost, "fixy"));
}

// The walk of the parameters of a function, in their order.  Complexity:
// linear in the parameters of the function.
template <class Matches>
[[nodiscard]] consteval std::meta::info find_parameter(std::meta::info function, Matches&& matches) {
    for (const std::meta::info parameter : std::meta::parameters_of(function)) {
        if (matches(parameter)) return parameter;
    }
    return std::meta::info{};
}

}  // namespace foundation::reflect
