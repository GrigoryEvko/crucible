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

#include <cstddef>
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

// ── Properties of a whole relation ──────────────────────────────────
//
// A relation's author states a property of the relation once, and the
// checks below derive it from the namespace instead of restating it
// per edge.  A catalog that was once four hand-kept lists (the edges,
// a positive witness per edge, an inverse witness per edge, and a
// count with a roster) is then one list: the edges.
//
// Every helper reads the namespace the way admits does: a variable
// whose type is edge<From, To> is an edge, and every other member is
// skipped.  From and To are read through their aliases, so an edge
// written against `using Alias = Concrete;` is the same edge as one
// written against Concrete.

// True when m reflects a variable of type edge<From, To> for some
// From and To.  The kind is settled before the type is read, because
// type_of is defined for a variable and not for every member kind.
[[nodiscard]] consteval bool is_edge(std::meta::info m) noexcept {
    if (!std::meta::is_variable(m)) return false;
    const auto type = std::meta::remove_cvref(std::meta::type_of(m));
    return std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^edge;
}

// The two ends of an edge variable, each read through its aliases.
struct edge_ends {
    std::meta::info from;
    std::meta::info to;
};

[[nodiscard]] consteval edge_ends ends_of(std::meta::info edge_variable) noexcept {
    const auto args = std::meta::template_arguments_of(std::meta::remove_cvref(std::meta::type_of(edge_variable)));
    return edge_ends{std::meta::dealias(args[0]), std::meta::dealias(args[1])};
}

// The namespace that declares a type.  A template specialization is
// placed where its template is declared, so `Pinned<X86>` and
// `Pinned<Arm>` share a family with the template `Pinned`.
[[nodiscard]] consteval std::meta::info family_of(std::meta::info type) noexcept {
    const auto dealiased = std::meta::dealias(type);
    if (std::meta::has_template_arguments(dealiased)) {
        return std::meta::parent_of(std::meta::template_of(dealiased));
    }
    return std::meta::parent_of(dealiased);
}

// The number of edges in Ns.  A pin against this count makes a new
// edge a two-place edit that a reviewer sees.
template <std::meta::info Ns>
[[nodiscard]] consteval std::size_t edge_count() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fail_closed::edge_count<Ns>: Ns must be the reflection of a "
                                               "namespace, written ^^name.");
    std::size_t count = 0;
    for (const auto m : std::meta::members_of(Ns, std::meta::access_context::unchecked())) {
        if (is_edge(m)) ++count;
    }
    return count;
}

// True when Ns admits (A, B) and (B, A) only for A == B.  A relation
// that is a one-way ratchet holds no inverse of any of its edges, so
// an edge added "by symmetry" reds here instead of quietly admitting a
// downgrade.
template <std::meta::info Ns>
[[nodiscard]] consteval bool is_antisymmetric() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fail_closed::is_antisymmetric<Ns>: Ns must be the reflection of "
                                               "a namespace, written ^^name.");
    const auto members = std::meta::members_of(Ns, std::meta::access_context::unchecked());
    for (const auto m : members) {
        if (!is_edge(m)) continue;
        const auto ends = ends_of(m);
        if (ends.from == ends.to) continue;
        for (const auto other : members) {
            if (!is_edge(other)) continue;
            const auto other_ends = ends_of(other);
            if (other_ends.from == ends.to && other_ends.to == ends.from) return false;
        }
    }
    return true;
}

// True when every edge in Ns joins two types of one family, where a
// family is the namespace that declares the type.  A relation over
// orthogonal tag axes never crosses an axis: laundering a provenance
// tag into a trust tag would confound what the phantom means.
template <std::meta::info Ns>
[[nodiscard]] consteval bool is_intra_namespace() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fail_closed::is_intra_namespace<Ns>: Ns must be the reflection "
                                               "of a namespace, written ^^name.");
    for (const auto m : std::meta::members_of(Ns, std::meta::access_context::unchecked())) {
        if (!is_edge(m)) continue;
        const auto ends = ends_of(m);
        if (family_of(ends.from) != family_of(ends.to)) return false;
    }
    return true;
}

// True when Ns holds an edge whose From is T, for any To.
template <std::meta::info Ns, class T>
[[nodiscard]] consteval bool has_edge_from() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fail_closed::has_edge_from<Ns, T>: Ns must be the reflection of "
                                               "a namespace, written ^^name.");
    for (const auto m : std::meta::members_of(Ns, std::meta::access_context::unchecked())) {
        if (is_edge(m) && ends_of(m).from == std::meta::dealias(^^T)) return true;
    }
    return false;
}

// True when Ns holds an edge whose To is T, for any From.
template <std::meta::info Ns, class T>
[[nodiscard]] consteval bool has_edge_to() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fail_closed::has_edge_to<Ns, T>: Ns must be the reflection of a "
                                               "namespace, written ^^name.");
    for (const auto m : std::meta::members_of(Ns, std::meta::access_context::unchecked())) {
        if (is_edge(m) && ends_of(m).to == std::meta::dealias(^^T)) return true;
    }
    return false;
}

// Which end of an edge a type must occupy for every_class_in_has_edge.
enum class EdgeEnd : unsigned char {
    From,
    To,
    Either,
};

// True when every class declared directly in TagNs, other than the
// Excluded ones, is the named end of at least one edge in Ns.  A tag
// that is declared and never admitted is a gap in the audit trail this
// check closes.  A class template, an enumeration, and a type alias
// are not classes declared in TagNs and are skipped.
template <std::meta::info Ns, std::meta::info TagNs, EdgeEnd End, class... Excluded>
[[nodiscard]] consteval bool every_class_in_has_edge() noexcept {
    static_assert(std::meta::is_namespace(Ns) && std::meta::is_namespace(TagNs),
                  "fail_closed::every_class_in_has_edge<Ns, TagNs, End, Excluded...>: Ns and TagNs "
                  "must be reflections of namespaces, written ^^name.");
    for (const auto m : std::meta::members_of(TagNs, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(m) || std::meta::is_type_alias(m) || !std::meta::is_class_type(m)) continue;
        if (((m == std::meta::dealias(^^Excluded)) || ... || false)) continue;
        bool found = false;
        for (const auto candidate : std::meta::members_of(Ns, std::meta::access_context::unchecked())) {
            if (!is_edge(candidate)) continue;
            const auto ends = ends_of(candidate);
            const bool at_from = End != EdgeEnd::To && ends.from == m;
            const bool at_to = End != EdgeEnd::From && ends.to == m;
            if (at_from || at_to) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

// True when admits answers yes for every edge declared in Ns.  The
// enumeration above and the admission check read the namespace by two
// different routes, and this is the witness that the two agree.
template <std::meta::info Ns>
[[nodiscard]] consteval bool every_edge_is_admitted() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fail_closed::every_edge_is_admitted<Ns>: Ns must be the "
                                               "reflection of a namespace, written ^^name.");
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()));
    // -Wshadow fires on the expansion-statement induction variable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto m : members) {
        if constexpr (is_edge(m)) {
            constexpr auto ends = ends_of(m);
            if (!admits<Ns, typename[:ends.from:], typename[:ends.to:]>()) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace foundation::fail_closed
