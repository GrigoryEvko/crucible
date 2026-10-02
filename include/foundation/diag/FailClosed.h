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
// The check reads the namespace where the check stands.  Declare every
// edge of a relation before the first check against it.
//
// The check is the function admits, which is not a template, and the
// concept Admitted reads it.  A translation unit cannot specialize either
// one to admit a pair that the namespace does not declare.
//
// ── The seal ──────────────────────────────────────────────────────────
//
// A namespace can be opened again from any file, so a translation unit
// can add an edge to a shipped relation after its header, and a pair
// first checked after that point is admitted.  A seal closes that door.
// It is one variable of type seal in the relation's namespace, and its
// count is a literal: the number of members the namespace declares
// other than the seal.  The count takes every member, an edge, an alias,
// a class or anything else, because a relation can read more than its
// edges: a rule family or a second kind of edge admits pairs too.
//
//     namespace retag {
//     inline constexpr foundation::fail_closed::edge<FromUser, Sanitized> user_to_sanitized{};
//     inline constexpr foundation::fail_closed::seal sealed{.members = 1};
//     }  // namespace retag
//
// Every read of a sealed relation counts the members again.  A count
// that differs from the seal, before or after the header, stops the
// build, and so does a second seal.  A late member is then a compile
// error and never a different answer.  A relation with no seal stays
// open, and the census in test/fixy/test_armed_roster.cpp names every
// open relation under foundation and fixy with the reason it is open.
//
// ── One walk for each state of a relation ─────────────────────────────
//
// GCC keeps no answer of a call that walks a namespace, because the
// answer depends on the point where the call stands.  A namespace only
// grows, so the number of its members names its state.  Each query below
// counts the members and reads the relation_state of that count.  The
// walk is the value of the variable template relation_cells_at, so GCC
// walks the namespace one time for each state.  Each later query at that
// state reads the walk through a pointer.  A member added after a read
// changes the count, so the next read walks again and finds the member.
//
// A translation unit can specialize a variable template, and two facts
// make a specialization harmless.  The constructors of the cells are
// private, and the walk is their one friend, so no specialization builds
// or copies cells.  A specialization of relation_state_at can only view
// the cells of another walk, and each read compares the namespace and the
// count of the view with its own, so that view stops the build.

#include <foundation/Platform.h>

#include <array>
#include <cstddef>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace foundation::fail_closed {

// An admitted pair.  The declaration `inline constexpr edge<From, To>
// name{};` inside the relation's namespace is the whole opt-in.
template <class From, class To>
struct edge {};

// The closure of one relation.  `members` is the number of members the
// namespace declares other than the seal, written as a literal.
struct seal {
    std::size_t members = 0;
};

enum class seal_fault : unsigned char {
    none,
    count_differs,  // the namespace holds a different number of members than its seal states
    sealed_twice,  // the namespace holds two seals
};

struct seal_reading {
    bool is_sealed = false;
    seal_fault fault = seal_fault::none;
    std::size_t sealed = 0;
    std::size_t found = 0;
};

// True when m reflects a variable of type seal.
[[nodiscard]] consteval bool is_seal(std::meta::info m) noexcept {
    return std::meta::is_variable(m) && std::meta::remove_cvref(std::meta::type_of(m)) == ^^seal;
}

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

// The edges of one reading of a relation, in the order of declaration.
// Edge i has its From at ends[2 i] and its To at ends[2 i + 1], each read
// through its aliases.  The list views one array of reflections, which
// the cells of a walk keep in static storage.
struct edge_list {
    std::span<const std::meta::info> ends{};

    [[nodiscard]] consteval std::size_t size() const noexcept { return ends.size() / 2; }
    [[nodiscard]] consteval edge_ends operator[](std::size_t index) const noexcept {
        return edge_ends{ends.data()[2 * index], ends.data()[(2 * index) + 1]};
    }
};

namespace detail {

// Each function below has no constant definition.  A call to one stops
// the constant evaluation, and the diagnostic names it.
void a_member_stands_outside_the_seal_of_its_relation() noexcept;
void a_relation_holds_two_seals() noexcept;
void admits_reads_the_reflection_of_a_namespace() noexcept;
void a_relation_state_stands_for_another_reading() noexcept;

template <std::size_t Capacity>
class relation_cells;

// Walks the namespace one time.  It reads the seal, counts the members
// and keeps each edge.  A namespace with no seal is open and has no
// fault.  Capacity is the member count of the key.  A walk that finds
// another count keeps no edge, and the read refuses its state.
// Complexity: linear in the members of the namespace.
template <std::size_t Capacity>
[[nodiscard]] consteval relation_cells<Capacity> walk_relation(std::meta::info ns);

// What one walk of a relation found, at one count of its members.  The
// cells are the value of relation_cells_at, so they stay in static
// storage, and no template parameter object holds them.  The two
// constructors are private and user-provided, and the walk is their one
// friend.  So a specialization of relation_cells_at can only hold the
// cells of a walk of another relation, which the read refuses.  No
// std::bit_cast or std::start_lifetime_as builds cells.
template <std::size_t Capacity>
class relation_cells {
private:
    constexpr relation_cells() noexcept {}
    constexpr relation_cells(const relation_cells& other) noexcept
        : relation_{other.relation_},
          members_{other.members_},
          reading_{other.reading_},
          edge_count_{other.edge_count_},
          ends_{other.ends_},
          types_{other.types_} {}

    std::meta::info relation_{};
    std::size_t members_ = 0;
    seal_reading reading_{};
    std::size_t edge_count_ = 0;
    // Edge i has its From at ends_[2 i] and its To at ends_[2 i + 1].  One
    // cell more than the capacity keeps each array above size zero.
    std::array<std::meta::info, (2 * Capacity) + 1> ends_{};
    std::array<std::meta::info, Capacity + 1> types_{};

    template <std::size_t C>
    friend consteval relation_cells<C> walk_relation(std::meta::info ns);
    friend class relation_state;
};

template <std::size_t Capacity>
[[nodiscard]] consteval relation_cells<Capacity> walk_relation(std::meta::info ns) {
    const std::vector<std::meta::info> members = std::meta::members_of(ns, std::meta::access_context::unchecked());
    relation_cells<Capacity> cells{};
    cells.relation_ = ns;
    cells.members_ = members.size();
    if (members.size() != Capacity) return cells;
    std::size_t seals = 0;
    for (const std::meta::info* member = members.data(); member != members.data() + members.size(); ++member) {
        if (is_seal(*member)) {
            ++seals;
            cells.reading_.is_sealed = true;
            cells.reading_.sealed = std::meta::extract<seal>(*member).members;
            continue;
        }
        ++cells.reading_.found;
        if (!is_edge(*member)) continue;
        const edge_ends pair = ends_of(*member);
        cells.ends_.data()[2 * cells.edge_count_] = pair.from;
        cells.ends_.data()[(2 * cells.edge_count_) + 1] = pair.to;
        cells.types_.data()[cells.edge_count_] = std::meta::remove_cvref(std::meta::type_of(*member));
        ++cells.edge_count_;
    }
    if (seals > 1) {
        cells.reading_.fault = seal_fault::sealed_twice;
    } else if (cells.reading_.is_sealed && cells.reading_.found != cells.reading_.sealed) {
        cells.reading_.fault = seal_fault::count_differs;
    }
    return cells;
}

// A view of the cells of one walk, so that the queries read each count
// through one type.  A view names the cells of a variable, so it has no
// constructor that takes a span from a caller.  The copy is private and
// user-provided, so a specialization of relation_state_at can only view
// the cells of another walk, and the read refuses that view.
class relation_state {
public:
    [[nodiscard]] consteval std::meta::info relation() const noexcept { return relation_; }
    [[nodiscard]] consteval std::size_t members() const noexcept { return members_; }
    [[nodiscard]] consteval seal_reading reading() const noexcept { return reading_; }
    [[nodiscard]] consteval edge_list edges() const noexcept { return edge_list{ends_}; }
    // The type edge<From, To> of each edge, at the index of its ends.
    [[nodiscard]] consteval std::span<const std::meta::info> edge_types() const noexcept { return edge_types_; }

    // The view of cells that a walk made.  Only the walk makes cells, so
    // a view of some walk is all that a caller can build here.
    template <std::size_t Capacity>
    [[nodiscard]] static consteval relation_state of(const relation_cells<Capacity>& cells) noexcept {
        return relation_state{cells.relation_, cells.members_, cells.reading_,
                              std::span<const std::meta::info>{cells.ends_.data(), 2 * cells.edge_count_},
                              std::span<const std::meta::info>{cells.types_.data(), cells.edge_count_}};
    }

private:
    consteval relation_state(std::meta::info relation, std::size_t members, seal_reading reading,
                             std::span<const std::meta::info> ends,
                             std::span<const std::meta::info> edge_types) noexcept
        : relation_{relation}, members_{members}, reading_{reading}, ends_{ends}, edge_types_{edge_types} {}
    consteval relation_state(const relation_state& other) noexcept
        : relation_{other.relation_},
          members_{other.members_},
          reading_{other.reading_},
          ends_{other.ends_},
          edge_types_{other.edge_types_} {}

    std::meta::info relation_{};
    std::size_t members_ = 0;
    seal_reading reading_{};
    std::span<const std::meta::info> ends_{};
    std::span<const std::meta::info> edge_types_{};
};

// The cells and the view of the relation Ns when it has Members members.
// GCC evaluates each initializer one time for each key.
template <std::meta::info Ns, std::size_t Members>
inline constexpr relation_cells<Members> relation_cells_at = walk_relation<Members>(Ns);

template <std::meta::info Ns, std::size_t Members>
inline constexpr relation_state relation_state_at = relation_state::of(relation_cells_at<Ns, Members>);

// The state of ns at its member count now.  A state that names another
// namespace or another count views the walk of another relation, which a
// specialization put in the place of this walk, and the read stops the
// build.
[[nodiscard]] consteval const relation_state& read_relation_(std::meta::info ns) {
    const std::size_t members = std::meta::members_of(ns, std::meta::access_context::unchecked()).size();
    const relation_state& state = std::meta::extract<const relation_state&>(std::meta::substitute(
        ^^relation_state_at, {std::meta::reflect_constant(ns), std::meta::reflect_constant(members)}));
    if (state.relation() != ns || state.members() != members) a_relation_state_stands_for_another_reading();
    return state;
}

// The state of ns, after a check that a seal holds.  Each query below
// reads its relation here.
[[nodiscard]] consteval const relation_state& checked_relation_(std::meta::info ns) {
    const relation_state& state = read_relation_(ns);
    if (state.reading().fault == seal_fault::sealed_twice) a_relation_holds_two_seals();
    if (state.reading().fault == seal_fault::count_differs) a_member_stands_outside_the_seal_of_its_relation();
    return state;
}

// A query below that reads a namespace refuses another reflection by a
// throw, and the diagnostic prints the message.
consteval void require_a_namespace(std::meta::info ns, std::string_view query) {
    if (std::meta::is_namespace(ns)) return;
    std::string text{"fail_closed::"};
    text += query;
    text += ": the first argument must be the reflection of a namespace, written ^^name.";
    throw std::meta::exception(text, ns);
}

}  // namespace detail

// Reads the seal of a relation and counts its members now.  A namespace
// with no seal is open and has no fault.
[[nodiscard]] consteval seal_reading read_seal(std::meta::info ns) { return detail::read_relation_(ns).reading(); }

// Stops the build when a read of a sealed relation finds a fault.  Every
// query below does this check first.  A header whose own walk reads a
// sealed relation calls it at the start of each query.  A static assertion
// on Sealed does not do that work: a condition that names no template
// parameter is checked one time, where the template is defined.
consteval void require_seal_holds(std::meta::info ns) { (void)detail::checked_relation_(ns); }

// The edges of the relation ns in the order of declaration, each read
// through its aliases, after a check that a seal holds.  A header that
// reads the edges of a relation reads them here, so GCC walks the
// namespace one time for each state of the namespace.
[[nodiscard]] consteval edge_list edges_of(std::meta::info ns) {
    detail::require_a_namespace(ns, "edges_of");
    return detail::checked_relation_(ns).edges();
}

// True when Ns holds one seal and exactly the members it counts.
template <std::meta::info Ns>
concept Sealed = read_seal(Ns).is_sealed && read_seal(Ns).fault == seal_fault::none;

// True when ns declares a variable of type edge<from, to>.  Every other
// member of ns is skipped: a function, a nested type, a nested namespace,
// a template, or a variable of any other type.  A nested namespace is not
// opened, so an edge one level down does not count.  Complexity: linear
// in the edges of ns.
[[nodiscard]] consteval bool admits(std::meta::info ns, std::meta::info from, std::meta::info to) {
    if (!std::meta::is_namespace(ns)) detail::admits_reads_the_reflection_of_a_namespace();
    const std::span<const std::meta::info> types = detail::checked_relation_(ns).edge_types();
    const std::meta::info admitted = std::meta::substitute(^^edge, {from, to});
    for (const std::meta::info* type = types.data(); type != types.data() + types.size(); ++type) {
        if (*type == admitted) return true;
    }
    return false;
}

template <std::meta::info Ns, class From, class To>
concept Admitted = admits(Ns, ^^From, ^^To);

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
    return detail::checked_relation_(Ns).edges().size();
}

// True when Ns admits (A, B) and (B, A) only for A == B.  A relation
// that is a one-way ratchet holds no inverse of any of its edges, so
// an edge added "by symmetry" reds here instead of quietly admitting a
// downgrade.
template <std::meta::info Ns>
[[nodiscard]] consteval bool is_antisymmetric() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fail_closed::is_antisymmetric<Ns>: Ns must be the reflection of "
                                               "a namespace, written ^^name.");
    const edge_list edges = detail::checked_relation_(Ns).edges();
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const edge_ends edge = edges[index];
        if (edge.from == edge.to) continue;
        for (std::size_t other_index = 0; other_index < edges.size(); ++other_index) {
            const edge_ends other = edges[other_index];
            if (other.from == edge.to && other.to == edge.from) return false;
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
    const edge_list edges = detail::checked_relation_(Ns).edges();
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const edge_ends edge = edges[index];
        if (family_of(edge.from) != family_of(edge.to)) return false;
    }
    return true;
}

// The queries below about one type are functions at namespace scope that
// are not templates, so no translation unit can specialize an answer.  A
// read that the author must see refuses by a throw: the constant
// evaluation that asked stops, and the diagnostic prints the message.
namespace detail {

// The number of edges in ns whose From is type, or whose To is type when
// is_to is true.  Complexity: linear in the edges of ns.
[[nodiscard]] consteval std::size_t edges_at_end(std::meta::info ns, std::meta::info type, bool is_to) {
    const edge_list edges = checked_relation_(ns).edges();
    const std::meta::info wanted = std::meta::dealias(type);
    std::size_t count = 0;
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const edge_ends edge = edges[index];
        if ((is_to ? edge.to : edge.from) == wanted) ++count;
    }
    return count;
}

}  // namespace detail

// True when ns holds an edge whose From is type, for any To.
[[nodiscard]] consteval bool has_edge_from(std::meta::info ns, std::meta::info type) {
    detail::require_a_namespace(ns, "has_edge_from");
    return detail::edges_at_end(ns, type, false) != 0;
}

// True when ns holds an edge whose To is type, for any From.
[[nodiscard]] consteval bool has_edge_to(std::meta::info ns, std::meta::info type) {
    detail::require_a_namespace(ns, "has_edge_to");
    return detail::edges_at_end(ns, type, true) != 0;
}

// The number of edges in ns whose From is type.
[[nodiscard]] consteval std::size_t edge_count_from(std::meta::info ns, std::meta::info type) {
    detail::require_a_namespace(ns, "edge_count_from");
    return detail::edges_at_end(ns, type, false);
}

// The To of the one edge in ns whose From is type, for a relation that is
// a function of its From.  Exactly one such edge must exist.  None is an
// undeclared pair, and two is a relation that answers twice.  Each one
// stops the build here, and no primary template answers for the author.
[[nodiscard]] consteval std::meta::info unique_target(std::meta::info ns, std::meta::info type) {
    detail::require_a_namespace(ns, "unique_target");
    const edge_list edges = detail::checked_relation_(ns).edges();
    const std::meta::info wanted = std::meta::dealias(type);
    std::size_t count = 0;
    std::meta::info target = ^^void;
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const edge_ends edge = edges[index];
        if (edge.from != wanted) continue;
        if (count == 0) target = edge.to;
        ++count;
    }
    if (count == 0) {
        throw std::meta::exception(u8"fail_closed::unique_target: the relation declares no edge from the type.  "
                                   u8"Declare `inline constexpr edge<T, To> name{};` in the namespace of the "
                                   u8"relation before the first check against it.",
                                   type);
    }
    if (count > 1) {
        throw std::meta::exception(u8"fail_closed::unique_target: the relation declares more than one edge from "
                                   u8"the type, so it is not a function of the type.  Remove all but one.",
                                   type);
    }
    return target;
}

template <std::meta::info Ns, class T>
using unique_target_t = typename[:unique_target(Ns, ^^T):];

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
    const edge_list edges = detail::checked_relation_(Ns).edges();
    for (const auto m : std::meta::members_of(TagNs, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(m) || std::meta::is_type_alias(m) || !std::meta::is_class_type(m)) continue;
        if (((m == std::meta::dealias(^^Excluded)) || ... || false)) continue;
        bool found = false;
        for (std::size_t index = 0; index < edges.size(); ++index) {
            const edge_ends edge = edges[index];
            const bool at_from = End != EdgeEnd::To && edge.from == m;
            const bool at_to = End != EdgeEnd::From && edge.to == m;
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
// enumeration reads the two ends of each edge from its template
// arguments, and admits compares the type of each edge with a type that
// it substitutes.  This is the witness that the two routes agree.
template <std::meta::info Ns>
[[nodiscard]] consteval bool every_edge_is_admitted() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fail_closed::every_edge_is_admitted<Ns>: Ns must be the "
                                               "reflection of a namespace, written ^^name.");
    const edge_list edges = detail::checked_relation_(Ns).edges();
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const edge_ends edge = edges[index];
        if (!admits(Ns, edge.from, edge.to)) return false;
    }
    return true;
}

}  // namespace foundation::fail_closed
