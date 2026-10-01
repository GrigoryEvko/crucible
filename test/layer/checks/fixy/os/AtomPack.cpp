// The compile-time checks of fixy/os/AtomPack.h.

#include <fixy/os/AtomPack.h>

namespace fixy::atom_pack::detail::atom_pack_self_test {

template <typename T>
struct probe_atom final
    : ::fixy::atom::lifting_atom_of<::fixy::Axis::SyscallSurface, eff::Row<eff::Effect::IO, eff::Effect::Block>> {};
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
