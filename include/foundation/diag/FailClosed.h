#pragma once

// A relation whose admitted pairs are the members of one namespace.
//
// A trait states such a relation in the usual way: a primary template
// that answers no, and one specialization for each admitted pair.  A
// specialization can be written wherever the primary is visible, so
// the relation is open.  A caller who wants a pair the author did not
// admit reopens the trait's namespace and adds one, and no concept
// built on the trait can tell the two apart.
//
// Reflection cannot enumerate the specializations of a template.  It
// can enumerate the members of a namespace.  Here a relation is a
// namespace, and an admitted pair is a variable of type edge<From, To>
// declared in it.  The check reads that namespace and nothing else.
// An edge declared in any other namespace is inert, and there is no
// primary template to specialize.  The relation is closed by
// construction: its pairs are exactly the edge variables in the
// namespace the check is given.
//
//     namespace retag {
//     inline constexpr foundation::fail_closed::edge<FromUser, Sanitized> user_to_sanitized{};
//     }  // namespace retag
//     static_assert(foundation::fail_closed::Admitted<^^retag, FromUser, Sanitized>);
//
// The namespace is read when a pair is first checked, and that answer
// holds for the rest of the translation unit.  Declare every edge of a
// relation before the first check against it.

#include <foundation/Platform.h>

#include <meta>

namespace foundation::fail_closed {

// An admitted pair.  The declaration `inline constexpr edge<From, To>
// name{};` inside the relation's namespace is the whole opt-in.
template <class From, class To>
struct edge {};

// True when Ns declares a variable of type edge<From, To>.  Every other
// member of Ns is skipped: a function, a nested type, a nested
// namespace, a template, or a variable of any other type.  A nested
// namespace is not opened, so an edge one level down does not count.
template <std::meta::info Ns, class From, class To>
[[nodiscard]] consteval bool admits() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fail_closed::admits<Ns, From, To>: Ns must be the reflection of "
                                               "a namespace, written ^^name.");
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()));
    // -Wshadow fires on the expansion-statement induction variable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto m : members) {
        // type_of is defined for a variable and not for every member
        // kind, so the kind is settled before the type is read.
        if constexpr (std::meta::is_variable(m)) {
            if constexpr (std::meta::remove_cvref(std::meta::type_of(m)) == ^^edge<From, To>) {
                return true;
            }
        }
    }
#pragma GCC diagnostic pop
    return false;
}

template <std::meta::info Ns, class From, class To>
concept Admitted = admits<Ns, From, To>();

}  // namespace foundation::fail_closed
