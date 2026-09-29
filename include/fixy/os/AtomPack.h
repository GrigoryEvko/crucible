#pragma once

// The folds that each atom-pack surface of fixy/os reads, and the closed
// tables that map a tag to the value that a syscall takes.
//
// Fs.h, Io.h and Mmap.h each take a pack of atoms, and each reads the pack
// the same four ways: the effect row that the pack lifts to, the gate
// that the calling context admits that row, the one type argument of an
// atom of one template, and the count of atoms of one template.  This
// header states each of the four one time.
//
// A tag reaches a syscall only through a row of a closed table.  A table
// is one constant array of rows, and each row holds the reflection of a
// tag.  No other translation unit can add a row to an array, specialize
// it or overload a lookup over it.  So the set of tags that reach a
// syscall is the set that the header of the table wrote.  A map that is
// a class template is open: a caller can specialize it for a class of
// its own, and that specialization brings any bits to the door.
//
// Each query that a gate reads is a concept or a lookup over a closed
// table.  A concept cannot be specialized.  Each header calls its lookups
// in its own checks, so a specialization of a lookup that a caller writes
// after the header comes after an instantiation, and it is an error.  The
// value of a tag is read through a function over a reflection, not a
// variable template that a caller could specialize for one tag.

#include <fixy/Atom.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Instance.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace fixy::atom_pack {

namespace eff = ::foundation::effects;

namespace detail {

template <typename... Atoms>
struct atoms_row_ {
    using type = eff::Row<>;
};
template <typename First, typename... Rest>
struct atoms_row_<First, Rest...> {
    using type = eff::row_union_t<eff::lift_row_t<std::remove_cvref_t<First>>, typename atoms_row_<Rest...>::type>;
};

template <typename Value>
struct tag_row_ final {
    std::meta::info tag{};
    Value value{};
};

}  // namespace detail

// The row that a pack lifts to: the union of the rows of its atoms.  An
// atom whose row is wider widens the gate, so a gate written before the
// atom existed cannot pass it through.
template <typename... Atoms>
using atoms_row_t = typename detail::atoms_row_<Atoms...>::type;

// A context admits a pack when each member is an atom that lifts to a row,
// and the context admits the union of those rows.
template <typename Ctx, typename... Atoms>
concept CtxAdmitsAtomRow = eff::IsExecCtx<Ctx> && (::fixy::atom::IsAtom<std::remove_cvref_t<Atoms>> && ...)
                        && (eff::LiftsToRow<std::remove_cvref_t<Atoms>> && ...)
                        && eff::CtxAdmits<Ctx, atoms_row_t<Atoms...>>;

// True when A is an atom of the class template that Template reflects.
template <typename A, std::meta::info Template>
concept IsAtomOf = ::foundation::reflect::IsInstanceOf<std::remove_cvref_t<A>, Template>;

namespace detail {

template <std::meta::info Template, typename A>
[[nodiscard]] consteval std::meta::info argument_of() noexcept {
    if constexpr (IsAtomOf<A, Template>) {
        return std::meta::template_arguments_of(std::meta::dealias(std::meta::remove_cvref(^^A)))[0];
    } else {
        return ^^void;
    }
}

}  // namespace detail

// The first template argument of an atom of Template, or void for an atom
// of another kind.  The void lets a fold skip the atoms that it does not
// read.
template <std::meta::info Template, typename A>
using argument_of_t = typename[:detail::argument_of<Template, A>():];

// A pack names an atom of Template exactly one time, or at most one time.
// Each fold is written inside its concept, and a concept cannot be
// specialized, so no translation unit can change a count that a gate
// reads.
template <std::meta::info Template, typename... Atoms>
concept HasOneAtomOf = ((std::size_t{IsAtomOf<Atoms, Template>} + ... + std::size_t{0}) == 1);

template <std::meta::info Template, typename... Atoms>
concept HasAtMostOneAtomOf = ((std::size_t{IsAtomOf<Atoms, Template>} + ... + std::size_t{0}) <= 1);

// A pack names one atom two times.  Two different atoms of one template
// are not a repeat, because a flag template folds several bits.
template <typename A, typename... Pack>
concept OccursMoreThanOnce =
    ((std::size_t{std::is_same_v<std::remove_cvref_t<A>, std::remove_cvref_t<Pack>>} + ... + std::size_t{0}) > 1);

template <std::meta::info Template, typename... Atoms>
concept RepeatsAnAtomOf = ((IsAtomOf<Atoms, Template> && OccursMoreThanOnce<Atoms, Atoms...>) || ...);

// One row of a closed table: the reflection of a tag and the value that
// the kernel takes for it.
template <typename Value>
using tag_row = detail::tag_row_<Value>;

// The index of the row of a closed table for a tag, or N when the table
// writes no row for the tag.  The lookup reads the whole table at compile
// time, and a table has a few rows, so it is O(N) in the rows.  The tag is
// dealiased, so an alias of a tag finds the row of the tag.  A tag with a
// cv-qualifier is another type, and it finds no row.  The lookup gives an
// index and not a pointer, because GCC does not fold a comparison of a
// pointer into an array of reflections.
template <typename Row, std::size_t N>
[[nodiscard]] consteval std::size_t row_index(Row const (&table)[N], std::meta::info tag) noexcept {
    const std::meta::info wanted = std::meta::dealias(tag);
    for (std::size_t index = 0; index < N; ++index) {
        if (table[index].tag == wanted) return index;
    }
    return N;
}

template <typename Row, std::size_t N>
[[nodiscard]] consteval bool has_row(Row const (&table)[N], std::meta::info tag) noexcept {
    return row_index(table, tag) < N;
}

// The value of the row of a closed table for a tag.  For a tag with no
// row, the read is past the end of the table.  That read is not a constant
// expression, so a call for a tag with no row fails the build.
template <typename Row, std::size_t N>
[[nodiscard]] consteval auto value_for(Row const (&table)[N], std::meta::info tag) noexcept {
    return table[row_index(table, tag)].value;
}

// Whether a list of tag reflections names the tag.  A set of tags with no
// value, such as the tags that one door admits, is a list of this shape.
template <std::size_t N>
[[nodiscard]] consteval bool names_tag(std::meta::info const (&tags)[N], std::meta::info tag) noexcept {
    const std::meta::info wanted = std::meta::dealias(tag);
    for (std::meta::info const listed : tags) {
        if (listed == wanted) return true;
    }
    return false;
}

// Each class declared directly in the namespace Ns satisfies Predicate,
// which takes the reflection of the class.  A member that is not a type,
// is an alias or is not a class is not a tag, and the walk skips it.  A
// header asks this of each tag namespace that it maps, so a tag with no
// row fails the build at the table.  A tag is a public member of its
// namespace, so the walk of the calling context sees each one.
template <std::meta::info Ns, auto Predicate>
[[nodiscard]] consteval bool every_tag_in_satisfies() noexcept {
    static_assert(std::meta::is_namespace(Ns),
                  "fixy/os/AtomPack.h: every_tag_in_satisfies<Ns, Predicate> takes a reflection of a namespace.");
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::current()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            if (!Predicate(member)) return false;
        }
    }
    return true;
}

}  // namespace fixy::atom_pack

namespace fixy::atom_pack::detail::atom_pack_self_test {

template <typename T>
struct probe_atom final : ::fixy::atom::lifting_atom_of<::fixy::Axis::SyscallSurface,
                                                         eff::Row<eff::Effect::IO, eff::Effect::Block>> {};
struct probe_flag final : ::fixy::atom::lifting_atom_of<::fixy::Axis::SyscallSurface, eff::Row<eff::Effect::IO>> {};
struct probe_tag_a final {};
struct probe_tag_b final {};
using probe_alias_a = probe_tag_a;

using A = probe_atom<probe_tag_a>;
using B = probe_atom<probe_tag_b>;

static_assert(IsAtomOf<A, ^^probe_atom> && !IsAtomOf<probe_flag, ^^probe_atom>);
static_assert(std::is_same_v<argument_of_t<^^probe_atom, A>, probe_tag_a>);
static_assert(std::is_same_v<argument_of_t<^^probe_atom, probe_flag>, void>);
static_assert(HasOneAtomOf<^^probe_atom, A, probe_flag> && !HasOneAtomOf<^^probe_atom, A, B>);
static_assert(!HasOneAtomOf<^^probe_atom> && !HasOneAtomOf<^^probe_atom, probe_flag>);
static_assert(HasAtMostOneAtomOf<^^probe_atom> && HasAtMostOneAtomOf<^^probe_atom, A, probe_flag>
              && !HasAtMostOneAtomOf<^^probe_atom, A, B, probe_flag>);
static_assert(RepeatsAnAtomOf<^^probe_atom, A, probe_flag, A>);
static_assert(!RepeatsAnAtomOf<^^probe_atom, A, B, probe_flag, probe_flag>,
              "two different atoms of one template are not a repeat, and a repeat of another template is not read");
static_assert(std::is_same_v<atoms_row_t<A, probe_flag>, eff::Row<eff::Effect::IO, eff::Effect::Block>>);
static_assert(std::is_same_v<atoms_row_t<>, eff::Row<>>);

inline constexpr tag_row<int> probe_table[] = {
    {^^probe_tag_a, 3},
};
static_assert(has_row(probe_table, ^^probe_tag_a) && value_for(probe_table, ^^probe_tag_a) == 3);
static_assert(row_index(probe_table, ^^probe_tag_b) == 1, "a tag with no row has the index past the end");
static_assert(has_row(probe_table, ^^probe_alias_a), "an alias of a tag finds the row of the tag");
static_assert(!has_row(probe_table, ^^probe_tag_b), "a tag that the table does not write has no row");
static_assert(!has_row(probe_table, ^^const probe_tag_a), "a tag with a cv-qualifier is another type");

inline constexpr std::meta::info probe_list[] = {^^probe_tag_b};
static_assert(names_tag(probe_list, ^^probe_tag_b) && !names_tag(probe_list, ^^probe_tag_a));

}  // namespace fixy::atom_pack::detail::atom_pack_self_test
