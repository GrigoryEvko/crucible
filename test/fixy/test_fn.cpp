// Sentinel TU for fixy/Fn.h and fixy/Reject.h: the binding collapses to
// sizeof(T), an unmentioned axis takes its strict pole silently, one
// atom relaxes exactly its own axis, the gate refuses a duplicate and a
// non-atom, the duplicate diagnostic names the axis, and a context gate
// reads the lifts of the atoms as well as the Effect grade.
//
// The header self-tests prove these for a sample.  This file adds two
// walks.  The first walk takes each atom of fixy/Atom.h that the gate
// admits alone, and it checks the atom against every axis.  An atom that
// relaxes an axis other than its own fails that walk.  The second walk
// takes each axis that carries two atoms.  It checks that the gate refuses
// the two atoms together on each axis that is not a Row axis, and that a
// Row axis takes their union.  The third walk checks that the union is
// exactly the set of the stated atoms.

#include <fixy/Collision.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Predicates.h>
#include <fixy/Reject.h>
#include <fixy/atoms/Stdio.h>
#include <fixy/atoms/Sync.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/check/Check.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <array>
#include <cstddef>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using ::fixy::Axis;
using ::fixy::axis_count;
using ::fixy::strict_pole_t;
using ::fixy::fn;
using ::fixy::IsAccepted;
using ::fixy::mint_fn;

// ---------------------------------------------------------------------
// The shortest binding.

static_assert(sizeof(fn<int>) == sizeof(int));
static_assert(sizeof(fn<double>) == sizeof(double));
static_assert(alignof(fn<int>) == alignof(int));
static_assert(fn<int>::atom_count == 0);
static_assert(std::is_same_v<fn<int>::value_type, int>);

// ---------------------------------------------------------------------
// One atom relaxes its axis and leaves the other thirty-two strict.
//
// The header checks a dozen axes by hand.  This walks the enum, so the
// claim covers the table rather than a sample, and an axis added to the
// enum is covered the day it is declared.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// Whether Binding's grade on every axis but Relaxed is the strict pole
// that axis declares.  Axis::Type is skipped: its grade is the payload,
// which is never a strict pole.
template <class Binding, Axis Relaxed>
[[nodiscard]] consteval bool only_one_axis_moved() noexcept {
    bool all_strict = true;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        if constexpr (axis != Relaxed && axis != Axis::Type) {
            all_strict = all_strict && std::is_same_v<typename Binding::template grade_on<axis>, strict_pole_t<axis>>;
        }
    }
    return all_strict;
}

// Whether every axis resolves to something nameable for this binding.
template <class Binding>
[[nodiscard]] consteval bool every_axis_resolves() noexcept {
    bool all_named = true;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        all_named = all_named && !std::is_void_v<typename Binding::template grade_on<axis>>;
    }
    return all_named;
}

#pragma GCC diagnostic pop

// The walk below checks each atom the same way: its own axis carries it,
// and every other axis stays strict.
template <class Atom>
[[nodiscard]] consteval bool relaxes_exactly_its_own_axis() noexcept {
    using Binding = fn<int, Atom>;
    return std::is_same_v<typename Binding::template grade_on<Atom::axis>, Atom>
        && only_one_axis_moved<Binding, Atom::axis>()
        && Binding::template mentions_axis<Atom::axis> && Binding::atom_count == 1 && every_axis_resolves<Binding>();
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The walks read the public roster of every atom that the tree ships.
inline constexpr auto every_atom = std::define_static_array(
    std::meta::template_arguments_of(std::meta::dealias(^^::fixy::collision::all_atom_roster)));

// True for an atom that fixy/Atom.h declares in fixy::atom itself.  A
// parametric atom is declared where its template is declared.
[[nodiscard]] consteval bool is_declared_in_fixy_atom(std::meta::info atom) {
    const std::meta::info declared = std::meta::dealias(atom);
    const std::meta::info owner = std::meta::has_template_arguments(declared)
                                    ? std::meta::parent_of(std::meta::template_of(declared))
                                    : std::meta::parent_of(declared);
    return owner == ^^::fixy::atom;
}

// The first walk takes the atoms that fixy/Atom.h declares in fixy::atom.
// They sit on many axes, and the points of the Security and Trust lattices
// are among them.  Each family header under fixy/atoms/ puts its atoms on
// one axis, and the roster walk of that header proves it.  The walk does
// not check an atom that a collision rule or the corpus refuses alone,
// because fn<int, Atom> refuses that atom.
struct atom_walk {
    std::size_t admitted_alone = 0;
    std::size_t relaxes_its_own_axis = 0;
};

[[nodiscard]] consteval atom_walk walk_the_atoms() noexcept {
    atom_walk walk{};
    template for (constexpr auto member : every_atom) {
        if constexpr (is_declared_in_fixy_atom(member)) {
            using Atom = [:member:];
            if constexpr (IsAccepted<int, Atom>) {
                ++walk.admitted_alone;
                if (relaxes_exactly_its_own_axis<Atom>()) ++walk.relaxes_its_own_axis;
            }
        }
    }
    return walk;
}

constexpr atom_walk the_atoms = walk_the_atoms();
static_assert(the_atoms.admitted_alone > 0, "the walk admitted no atom, so it proves nothing");
static_assert(the_atoms.relaxes_its_own_axis == the_atoms.admitted_alone,
              "an atom of fixy/Atom.h relaxes an axis other than its own, or leaves its own axis strict");

// ---------------------------------------------------------------------
// Two atoms on one axis, on every axis that carries two.
//
// The walk uses the first two atoms of the whole roster on each axis.  The
// shape of the axis decides the answer of tier 4.  A Row axis takes the
// union of its atoms, so tier 4 admits the pair and refuses one atom of the
// pair stated twice.  An axis of each other shape carries one grade, so
// tier 4 refuses the pair.  Two different atoms of one axis give the same
// answer.

template <Axis A>
[[nodiscard]] consteval std::array<std::meta::info, 2> first_two_atoms_on() noexcept {
    std::array<std::meta::info, 2> found{};
    std::size_t count = 0;
    template for (constexpr auto member : every_atom) {
        using Candidate = [:member:];
        if constexpr (Candidate::axis == A) {
            if (count < found.size()) found[count] = member;
            ++count;
        }
    }
    return found;
}

struct duplicate_walk {
    std::size_t axes_with_two_atoms = 0;
    std::size_t axes_that_refuse_the_pair = 0;
    std::size_t row_axes_with_two_atoms = 0;
    std::size_t row_axes_that_take_the_union = 0;
};

[[nodiscard]] consteval duplicate_walk walk_the_axes() noexcept {
    duplicate_walk walk{};
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        static constexpr auto pair = first_two_atoms_on<axis>();
        if constexpr (pair[1] != std::meta::info{}) {
            using First = [:pair[0]:];
            using Second = [:pair[1]:];
            if constexpr (::fixy::axis_record(axis).shape == ::fixy::Shape::Row) {
                ++walk.row_axes_with_two_atoms;
                if (::fixy::UniqueAtomPerAxis<First, Second> && ::fixy::UniqueAtomPerAxis<Second, First>
                    && std::is_void_v<::fixy::duplicate_tag_or_void_t<First, Second>>
                    && !::fixy::UniqueAtomPerAxis<First, First> && !IsAccepted<int, First, First>
                    && std::is_same_v<::fixy::duplicate_tag_or_void_t<First, First>, ::fixy::duplicate_atom_on<axis>>
                    && std::is_same_v<::fixy::duplicate_tag_or_void_t<Second, First, Second>,
                                      ::fixy::duplicate_atom_on<axis>>) {
                    ++walk.row_axes_that_take_the_union;
                }
            } else {
                ++walk.axes_with_two_atoms;
                if (!::fixy::UniqueAtomPerAxis<First, Second> && !IsAccepted<int, First, Second>
                    && std::is_same_v<::fixy::duplicate_tag_or_void_t<First, Second>,
                                      ::fixy::duplicate_atom_on<axis>>) {
                    ++walk.axes_that_refuse_the_pair;
                }
            }
        }
    }
    return walk;
}

#pragma GCC diagnostic pop

constexpr duplicate_walk the_axes = walk_the_axes();
static_assert(the_axes.axes_with_two_atoms > 0, "no axis carries two atoms, so the walk proves nothing");
static_assert(the_axes.axes_that_refuse_the_pair == the_axes.axes_with_two_atoms,
              "an axis that is not a Row axis admits two atoms together, or the refusal names a different axis");
static_assert(the_axes.row_axes_with_two_atoms == ::fixy::count_axes_of_shape(::fixy::Shape::Row),
              "a Row axis has fewer than two atoms in the roster, so the union walk does not reach it");
static_assert(the_axes.row_axes_that_take_the_union == the_axes.row_axes_with_two_atoms,
              "a Row axis refuses two different atoms, or admits one atom stated twice");

// ---------------------------------------------------------------------
// The union of a Row axis is exactly the set of its stated atoms.
//
// For each pair of different atoms of the roster on a Row axis, in each
// order, the grade of the pair is the atom over the union of the two rows,
// in the order of the bits of the row.  The row of each atom is read in
// the spelling of its axis: the row of the Effect grade, and the row
// member of a surface.

namespace fe = ::foundation::effects;

template <Axis A, class Grade>
[[nodiscard]] consteval std::meta::info row_of_row_atom() noexcept {
    if constexpr (A == Axis::Effect) {
        return std::meta::dealias(^^::fixy::atom::effect_row_of_t<Grade>);
    } else {
        return std::meta::dealias(^^typename Grade::row);
    }
}

template <Axis A, class Row>
[[nodiscard]] consteval std::meta::info row_atom_over() noexcept {
    constexpr std::meta::info canonical = std::meta::dealias(^^fe::canonical_row_t<Row>);
    if constexpr (A == Axis::Effect) {
        return std::meta::dealias(
            std::meta::substitute(^^::fixy::atom::with, std::meta::template_arguments_of(canonical)));
    } else {
        return std::meta::dealias(
            std::meta::substitute(^^::fixy::atom::observe::surface, std::meta::template_arguments_of(canonical)));
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The atom of the roster at a position among the atoms on one axis, or the
// null reflection when the axis has fewer atoms.
template <Axis A, std::size_t Position>
[[nodiscard]] consteval std::meta::info atom_on_at() noexcept {
    std::size_t seen = 0;
    std::meta::info found{};
    template for (constexpr auto member : every_atom) {
        using Candidate = [:member:];
        if constexpr (Candidate::axis == A) {
            if (seen == Position) found = member;
            ++seen;
        }
    }
    return found;
}

// The walk reads the first atoms_per_row_axis atoms of the roster on each
// Row axis, and the pin below holds each Row axis to that count.
inline constexpr std::size_t atoms_per_row_axis = 4;

struct union_walk {
    std::size_t pairs = 0;
    std::size_t pairs_that_take_exactly_the_union = 0;
    bool is_each_row_atom_read = true;
};

template <Axis A, std::size_t FirstPosition, std::size_t SecondPosition>
constexpr void walk_the_union_of(union_walk& walk) noexcept {
    constexpr std::meta::info first = atom_on_at<A, FirstPosition>();
    constexpr std::meta::info second = atom_on_at<A, SecondPosition>();
    if constexpr (FirstPosition != SecondPosition && first != std::meta::info{} && second != std::meta::info{}) {
        using First = [:first:];
        using Second = [:second:];
        using FirstRow = [:row_of_row_atom<A, First>():];
        using SecondRow = [:row_of_row_atom<A, Second>():];
        using Union = fe::row_union_t<FirstRow, SecondRow>;
        using Grade = typename ::fixy::collision::grades<First, Second>::template on<A>;
        ++walk.pairs;
        if (std::meta::dealias(^^Grade) == row_atom_over<A, Union>()
            && row_of_row_atom<A, Grade>() == std::meta::dealias(^^Union) && ::fixy::UniqueAtomPerAxis<First, Second>) {
            ++walk.pairs_that_take_exactly_the_union;
        }
    }
}

[[nodiscard]] consteval union_walk walk_the_unions() noexcept {
    union_walk walk{};
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        if constexpr (::fixy::axis_record(axis).shape == ::fixy::Shape::Row) {
            walk.is_each_row_atom_read =
                walk.is_each_row_atom_read && atom_on_at<axis, atoms_per_row_axis>() == std::meta::info{};
            template for (constexpr std::size_t first : {0UZ, 1UZ, 2UZ, 3UZ}) {
                template for (constexpr std::size_t second : {0UZ, 1UZ, 2UZ, 3UZ}) {
                    walk_the_union_of<axis, first, second>(walk);
                }
            }
        }
    }
    return walk;
}

#pragma GCC diagnostic pop

constexpr union_walk the_unions = walk_the_unions();
static_assert(the_unions.pairs > 0, "no Row axis has two atoms in the roster, so the union walk proves nothing");
static_assert(the_unions.is_each_row_atom_read,
              "a Row axis has more atoms in the roster than the union walk reads, so raise atoms_per_row_axis");
static_assert(the_unions.pairs_that_take_exactly_the_union == the_unions.pairs,
              "the grade of two atoms on a Row axis is not the atom over the union of their rows");

// The cells of the union, by hand.  The bits of the row give the order, so
// with<IO> and with<Bg> make with<IO, Bg> in each order.  with<IO, Bg> and
// with<Bg, IO> are two atoms over one set, so they are no atom stated
// twice, and their union is that set.  One atom keeps the spelling of the
// pack.  A copy of one atom is refused, also through an alias.
using ::fixy::atom::with;
using ::fixy::atom::observe::surface;
using Eff = fe::Effect;
template <class... Atoms>
using grades = ::fixy::collision::grades<Atoms...>;

static_assert(std::is_same_v<grades<with<Eff::Bg>, with<Eff::IO>>::on<Axis::Effect>, with<Eff::IO, Eff::Bg>>);
static_assert(std::is_same_v<grades<with<Eff::IO>, with<Eff::Bg>>::on<Axis::Effect>, with<Eff::IO, Eff::Bg>>);
static_assert(
    std::is_same_v<grades<with<Eff::IO, Eff::Bg>, with<Eff::Bg, Eff::IO>>::on<Axis::Effect>, with<Eff::IO, Eff::Bg>>);
static_assert(::fixy::UniqueAtomPerAxis<with<Eff::IO, Eff::Bg>, with<Eff::Bg, Eff::IO>>);
static_assert(std::is_same_v<grades<with<>, with<Eff::Alloc>, with<Eff::Alloc, Eff::Bg>>::on<Axis::Effect>,
                             with<Eff::Alloc, Eff::Bg>>);
static_assert(std::is_same_v<grades<with<Eff::Bg, Eff::IO>>::on<Axis::Effect>, with<Eff::Bg, Eff::IO>>,
              "one atom keeps the spelling of the pack");
static_assert(
    std::is_same_v<grades<surface<Eff::Bg>, surface<Eff::IO>>::on<Axis::Observability>, surface<Eff::IO, Eff::Bg>>);
static_assert(grades<with<Eff::IO>, with<Eff::Bg>>::mentions<Axis::Effect>);
static_assert(!::fixy::UniqueAtomPerAxis<with<Eff::IO>, with<Eff::IO>>);
static_assert(!::fixy::UniqueAtomPerAxis<::fixy::atom::with_io, with<Eff::IO>>);
static_assert(!::fixy::UniqueAtomPerAxis<surface<Eff::IO>, with<Eff::Bg>, surface<Eff::IO>>);
static_assert(std::is_same_v<::fixy::duplicate_tag_or_void_t<surface<Eff::IO>, surface<Eff::IO>>,
                             ::fixy::duplicate_atom_on<Axis::Observability>>);

// The two pairs of the roster that tier 4 refused before the union stay
// refused, at tier 5.  with<IO> states IO at the classified pole of
// Security, and the corpus refuses that.  The two surfaces observe IO and
// Bg in a pack whose row is empty, and B002 refuses that.
static_assert(::fixy::UniqueAtomPerAxis<with<>, with<Eff::IO>> && !IsAccepted<int, with<>, with<Eff::IO>>);
static_assert(::fixy::UniqueAtomPerAxis<surface<Eff::IO>, surface<Eff::Bg>>
              && !IsAccepted<int, surface<Eff::IO>, surface<Eff::Bg>>);
static_assert(
    !::fixy::collision::pack_verdict_v<surface<Eff::IO>, surface<Eff::Bg>>.admits(::fixy::collision::RuleCode::B002));

// The row of the binding joins the union, and a context reads that row.
static_assert(std::is_same_v<::fixy::binding_row_t<fn<int, with<Eff::IO>, with<Eff::Alloc>, ::fixy::atom::as_public>>,
                             fe::Row<Eff::Alloc, Eff::IO>>);
static_assert(IsAccepted<int, with<Eff::Bg>, with<Eff::Alloc>, ::fixy::atom::as_public>);
static_assert(fn<int, with<Eff::Bg>, with<Eff::Alloc>, ::fixy::atom::as_public>::mentions_axis<Axis::Effect>);

// B002 reads the union of the surfaces against the row of the binding.  The
// union of IO and Bg is not inside an IO row, and it is inside an IO and Bg
// row.
static_assert(!::fixy::collision::pack_verdict_v<surface<Eff::IO>, surface<Eff::Bg>, with<Eff::IO>>.admits(
                  ::fixy::collision::RuleCode::B002),
              "a pack must not hide an observed effect outside its row behind a second surface");
static_assert(::fixy::collision::pack_verdict_v<surface<Eff::IO>, surface<Eff::Bg>, with<Eff::IO, Eff::Bg>>.admits(
    ::fixy::collision::RuleCode::B002));

// The empty pack leaves every axis but Type strict, which is the same
// claim with nothing relaxed.  Axis::Type is not an axis any atom can
// name, so passing it as Relaxed excludes nothing the walk would check.
static_assert(only_one_axis_moved<fn<int>, Axis::Type>());
static_assert(every_axis_resolves<fn<int>>());

// ---------------------------------------------------------------------
// What the gate refuses.

struct not_an_atom {};

static_assert(IsAccepted<int>);
static_assert(IsAccepted<int, ::fixy::atom::copy>);
static_assert(IsAccepted<int, ::fixy::atom::copy, ::fixy::atom::mut_append, ::fixy::atom::reentrant>);

static_assert(!IsAccepted<int, ::fixy::atom::copy, ::fixy::atom::affine>);
static_assert(!IsAccepted<int, ::fixy::atom::mut_append, ::fixy::atom::mut_monotonic>);
static_assert(!IsAccepted<int, not_an_atom>);
static_assert(!IsAccepted<int, ::fixy::atom::copy, not_an_atom>);
static_assert(!IsAccepted<void>);
static_assert(!IsAccepted<int&>);
static_assert(!IsAccepted<int[4]>);
static_assert(!IsAccepted<const int>);

// A cv-qualified or reference-qualified atom is refused rather than
// stripped, because such a type comes from a decltype on a variable.
static_assert(!IsAccepted<int, const ::fixy::atom::copy>);
static_assert(!IsAccepted<int, ::fixy::atom::copy&>);

// ---------------------------------------------------------------------
// What the refusal says.

static_assert(std::is_same_v<::fixy::duplicate_tag_or_void_t<::fixy::atom::copy, ::fixy::atom::affine>,
                             ::fixy::duplicate_atom_on<Axis::Usage>>);
static_assert(::fixy::duplicate_tag_or_void_t<::fixy::atom::copy, ::fixy::atom::affine>::name
              == "DuplicateAtomOnUsage");
static_assert(::fixy::duplicate_tag_or_void_t<::fixy::atom::mut_append, ::fixy::atom::mut_monotonic>::name
              == "DuplicateAtomOnMutation");
static_assert(std::is_same_v<::fixy::malformed_tag_or_void_t<not_an_atom>, ::fixy::malformed_atom<not_an_atom>>);
static_assert(::fixy::malformed_atom<not_an_atom>::name.size() != 0);
static_assert(std::is_same_v<::fixy::payload_tag_or_void_t<void>, ::fixy::unholdable_payload<void>>);

// The name carries the axis identifier, so renaming an axis renames its
// tag rather than leaving a stale string behind.
static_assert(::fixy::duplicate_atom_on<Axis::Effect>::name == "DuplicateAtomOnEffect");
static_assert(::fixy::duplicate_atom_on<Axis::Staleness>::name == "DuplicateAtomOnStaleness");

// ---------------------------------------------------------------------
// The door.

static_assert(std::is_same_v<decltype(mint_fn<int, ::fixy::atom::copy>(0)), fn<int, ::fixy::atom::copy>>);
static_assert(!std::is_constructible_v<fn<int, ::fixy::atom::copy>, int>);

// The private value constructor is only half the door.  The other half
// is that no accessor hands out a mutable lvalue reference: with a
// mutable value(), `binding.value() = x` would put any value under the
// pack of the binding without naming a mint.  A non-const lvalue binding
// reads back as const.
static_assert(std::is_same_v<decltype(std::declval<fn<int, ::fixy::atom::copy>&>().value()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<const fn<int, ::fixy::atom::copy>&>().value()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<fn<int, ::fixy::atom::copy>&&>().value()), int&&>);
static_assert(!std::is_assignable_v<decltype(std::declval<fn<int, ::fixy::atom::copy>&>().value()), int>,
              "a minted binding cannot be written into after the fact");

// A default binding holds Type{}, which no mint admitted.  The empty pack
// states no fact and grants no right, so it keeps its default constructor,
// and a binding can be a member of an aggregate.  A pack that names an atom
// has no default constructor: a fact such as a refinement, and a right such
// as a copy, each need the mint.
struct holds_a_binding {
    fn<int> binding{};
};

static_assert(std::is_default_constructible_v<fn<int>>);
static_assert(std::is_default_constructible_v<holds_a_binding>);
static_assert(!std::is_default_constructible_v<fn<int, ::fixy::atom::copy>>);
static_assert(!std::is_default_constructible_v<fn<int, ::fixy::atom::refined_with<::fixy::IsPositive>>>);

// ---------------------------------------------------------------------
// The row a binding requires of its context.
//
// The row joins the Effect grade with the lifts of the atoms.  Each
// context below holds the row the binding lifts to and admits it.  The
// two negative fixtures neg_fn_futex_row_under_compile_ctx and
// neg_fn_io_syscall_row_under_drain_ctx take the same bindings to a
// context that lacks one effect of the row.

namespace fe = ::foundation::effects;
using SyscallId = ::fixy::atom::syscall::SyscallId;
using FutexBinding = fn<int, ::fixy::atom::syscall::per<SyscallId::futex>>;
using ProcessStateBinding = fn<int, ::fixy::atom::syscall::per<SyscallId::getpid>, ::fixy::atom::as_public>;
using ParkBinding = fn<int, ::fixy::atom::sync::park>;

static_assert(std::is_same_v<::fixy::binding_row_t<FutexBinding>, fe::Row<fe::Effect::Block, fe::Effect::Nondet>>);
static_assert(std::is_same_v<::fixy::binding_row_t<ProcessStateBinding>, fe::Row<fe::Effect::IO, fe::Effect::Nondet>>);
static_assert(std::is_same_v<::fixy::binding_row_t<ParkBinding>, fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<
              ::fixy::binding_row_t<fn<int, ::fixy::atom::with_io, ::fixy::atom::as_public, ::fixy::atom::sync::park>>,
              fe::Row<fe::Effect::IO, fe::Effect::Block>>);

static_assert(::fixy::CtxAdmitsBinding<::fixy::BgLoadCtx, FutexBinding>, "the load context holds Block and Nondet");
static_assert(!::fixy::CtxAdmitsBinding<fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Block>>, FutexBinding>,
              "the return of a futex wait depends on the schedule, so a context without Nondet refuses it");
static_assert(::fixy::CtxAdmitsBinding<::fixy::TestRunnerCtx, ParkBinding>);
static_assert(::fixy::CtxAdmitsBinding<::fixy::BgLoadCtx, ProcessStateBinding>, "the load context holds IO and Nondet");
static_assert(!::fixy::CtxAdmitsBinding<::fixy::BgCompileCtx, ProcessStateBinding>,
              "the id of a process changes from run to run, so a context without Nondet refuses it");
static_assert(!::fixy::CtxAdmitsBinding<::fixy::BgCompileCtx, FutexBinding>, "the compile context lacks Block");
static_assert(!::fixy::CtxAdmitsBinding<::fixy::BgDrainCtx, ProcessStateBinding>, "the drain context lacks IO");
static_assert(!::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, ParkBinding>);
static_assert(!::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, fn<int, ::fixy::atom::sync::spin_pause>>,
              "a spin on a pause has no exit, so it lifts Div, and the foreground context refuses it");
static_assert(::fixy::CtxAdmitsBinding<::fixy::BgLoadCtx, fn<int, ::fixy::atom::sync::spin_pause>>,
              "the load context holds Div, so it admits a spin with no exit");
static_assert(!::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, fn<int, ::fixy::atom::sync::bounded_spin>>,
              "a bounded spin reports a give-up, so it lifts Nondet, and the foreground context refuses it");

// A stdio write lifts IO and Block.  The fixture
// neg_fn_stdio_row_under_block_ctx takes the same binding to a context
// that holds Block and not IO.
using StderrBinding =
    fn<int, ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stderr>, ::fixy::atom::as_public>;
static_assert(std::is_same_v<::fixy::binding_row_t<StderrBinding>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(::fixy::CtxAdmitsBinding<::fixy::BgLoadCtx, StderrBinding>, "the load context holds IO and Block");
static_assert(::fixy::CtxAdmitsBinding<::fixy::TestRunnerCtx, StderrBinding>);
static_assert(!::fixy::CtxAdmitsBinding<::fixy::BgCompileCtx, StderrBinding>, "the compile context lacks Block");

// A static_assert proves the constant-evaluated path only.  These run.
[[nodiscard]] int check_runtime_paths() {
    volatile int seed = 7;

    const auto plain = mint_fn<int>(static_cast<int>(seed));
    if (plain.value() != 7) return 1;

    const auto graded = mint_fn<int, ::fixy::atom::copy>(static_cast<int>(seed) + 1);
    if (graded.value() != 8) return 2;

    // The wrapper does not override the payload's copy semantics.
    auto copied = graded;
    if (copied.value() != 8) return 3;

    // The rvalue overload consumes, so a temporary's payload moves out
    // rather than being copied back through a const reference.
    if (mint_fn<int, ::fixy::atom::affine>(static_cast<int>(seed) + 2).value() != 9) return 4;

    // A default binding of the empty pack carries the payload's own default,
    // also as a member of an aggregate.
    const holds_a_binding defaulted{};
    if (defaulted.binding.value() != 0) return 5;

    if (sizeof(plain) != sizeof(int)) return 6;
    if (fn<int>::atom_count != 0) return 7;
    if (graded.atom_count != 1) return 8;

    // The generated tag strings exist at runtime, not only in a
    // constant expression.
    ::fixy::TextView const dup = ::fixy::duplicate_atom_on<Axis::Usage>::name;
    if (dup != "DuplicateAtomOnUsage") return 9;
    if (::fixy::duplicate_atom_on<Axis::Usage>::description.size() == 0) return 10;
    if (::fixy::duplicate_atom_on<Axis::Usage>::remediation.size() == 0) return 11;

    if (axis_count != 33) return 12;
    return 0;
}

// ---------------------------------------------------------------------
// The door checks the claim of the Refinement grade.
//
// refined_with<Pred> states a fact about the value, and the Refinement
// axis says that a contract discharges it.  So each mint runs the
// predicate on the value, and a value that fails it ends the process.  The
// role form goes through the same door.  A refused mint runs in a child
// through expect_dies of the Check family.

template <class T>
using positive_count = fn<T, ::fixy::atom::refined_with<::fixy::IsPositive>>;

[[nodiscard]] int check_refinement_doors() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    int volatile seed = -1;
    int const refused = seed;

    ::fixy::check::expect_dies(
        test, [refused] noexcept { (void)mint_fn<int, ::fixy::atom::refined_with<::fixy::IsPositive>>(refused); },
        ::fixy::TextView{"foundation: contract violation: Predicate{}(value_)"});
    ::fixy::check::expect_dies(
        test, [refused] noexcept { (void)::fixy::mint_fn_for<positive_count>(refused); },
        ::fixy::TextView{"foundation: contract violation: Predicate{}(value_)"});

    int const admitted = -refused;
    if (mint_fn<int, ::fixy::atom::refined_with<::fixy::IsPositive>>(admitted).value() != 1) return 20;
    if (::fixy::mint_fn_for<positive_count>(admitted).value() != 1) return 21;
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    if (int rc = check_refinement_doors(); rc != 0) return rc;
    return 0;
}
