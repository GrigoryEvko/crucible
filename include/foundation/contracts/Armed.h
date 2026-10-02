#pragma once

// A predicate that answers "no" for every type is indistinguishable, at
// every call site, from a predicate nobody armed.  Both let the caller
// through.  These two helpers are the witness that a predicate still has
// arms: the author names, beside the predicate, the types it must accept
// and the types it must refuse, and the build fails on the day an arm is
// deleted rather than on the day someone notices a gate stopped gating.
//
// The failure it catches: a gate reads a table whose primary answers
// false and which has no specialization.  The table answers "no" to every
// question, and each static assertion over it becomes a tautology, so
// Affine<Linear<int>> compiles and an exactly-once obligation becomes
// at-most-once.  A table with no arms and a table whose arms all refuse
// are the same text, so only a witness of the arms can tell them apart.
//
// Pred is a class trait, which is the form a template template argument
// can name.  A predicate that exists only as a concept or as a variable
// template gets a one-line trait beside it, and that trait is then what
// these helpers read.
//
// Both directions are needed.  A predicate that answers "yes" for
// everything is as unarmed as one that answers "no", and only one of the
// two is caught by acceptance alone.

#include <meta>
#include <type_traits>

namespace foundation::contracts {

// True when Pred accepts every witness, and there is at least one
// witness.  An empty pack proves nothing and answers false.
template <template <class> class Pred, class... Witnesses>
[[nodiscard]] consteval bool predicate_accepts() noexcept {
    return sizeof...(Witnesses) > 0 && (static_cast<bool>(Pred<Witnesses>::value) && ...);
}

// True when Pred refuses every witness, and there is at least one
// witness.  An empty pack proves nothing and answers false.
template <template <class> class Pred, class... Witnesses>
[[nodiscard]] consteval bool predicate_refuses() noexcept {
    return sizeof...(Witnesses) > 0 && (!static_cast<bool>(Pred<Witnesses>::value) && ...);
}

// ---------------------------------------------------------------------
// The armed cell, and the roster that asks for one.
//
// A static assertion beside a predicate arms it, but no walk can find a
// static assertion, so no walk can say which predicates are armed.  The
// cell is the same two witness lists in a form a walk can find: an
// explicit specialization of armed_cell for the predicate.
//
//   template <>
//   struct foundation::contracts::armed_cell<is_already_linear> {
//       using accepts = witnesses<Linear<int>, Linear<char>>;
//       using refuses = witnesses<int, void*, Affine<int>>;
//   };
//
// The primary is declared and never defined, so a predicate with no cell
// has an incomplete armed_cell, and a walk reads that as unarmed.

template <class... Types>
struct witnesses {};

template <template <class> class Pred>
struct armed_cell;

namespace detail {

template <template <class> class Pred, class Witnesses>
struct accepts_every;
template <template <class> class Pred, class... Types>
struct accepts_every<Pred, witnesses<Types...>> : std::bool_constant<predicate_accepts<Pred, Types...>()> {};

template <template <class> class Pred, class Witnesses>
struct refuses_every;
template <template <class> class Pred, class... Types>
struct refuses_every<Pred, witnesses<Types...>> : std::bool_constant<predicate_refuses<Pred, Types...>()> {};

}  // namespace detail

// True when the cell for Pred holds in both directions.  Each list must
// be non-empty, for the reason predicate_accepts states.
template <template <class> class Pred>
inline constexpr bool armed_cell_holds_v = detail::accepts_every<Pred, typename armed_cell<Pred>::accepts>::value
                                        && detail::refuses_every<Pred, typename armed_cell<Pred>::refuses>::value;

// The instance cell.
//
// A cell names a template of one type parameter.  A predicate over two
// arguments, or over a value and a type, cannot take one.  The instance
// cell names the predicate by its reflection instead, and its witnesses
// are whole specializations of that predicate:
//
//   template <>
//   struct foundation::contracts::armed_instances<^^is_at_or_above_> {
//       using accepts = witnesses<is_at_or_above_<Tier::Low, high_grade>>;
//       using refuses = witnesses<is_at_or_above_<Tier::High, low_grade>>;
//   };
//
// Each witness must be a specialization of the predicate the cell names.
// Otherwise a list of std::true_type, or of another predicate, would arm
// any predicate at all.  The primary is declared and never defined, for
// the reason the primary of armed_cell states.

template <std::meta::info Pred>
struct armed_instances;

namespace detail {

// True when W is a specialization of the class template that Pred
// reflects.
template <std::meta::info Pred, class W>
[[nodiscard]] consteval bool is_instance_of_predicate() noexcept {
    return std::meta::has_template_arguments(^^W) && std::meta::template_of(^^W) == Pred;
}

template <std::meta::info Pred, class Witnesses>
struct instances_answer;
template <std::meta::info Pred, class... Instances>
struct instances_answer<Pred, witnesses<Instances...>> {
    static constexpr bool is_all_true =
        sizeof...(Instances) > 0
        && ((is_instance_of_predicate<Pred, Instances>() && static_cast<bool>(Instances::value)) && ...);
    static constexpr bool is_all_false =
        sizeof...(Instances) > 0
        && ((is_instance_of_predicate<Pred, Instances>() && !static_cast<bool>(Instances::value)) && ...);
};

}  // namespace detail

// True when the instance cell for Pred holds in both directions.  Each
// list must be non-empty, and each witness must instantiate Pred.
template <std::meta::info Pred>
inline constexpr bool armed_instances_hold_v =
    detail::instances_answer<Pred, typename armed_instances<Pred>::accepts>::is_all_true
    && detail::instances_answer<Pred, typename armed_instances<Pred>::refuses>::is_all_false;

// The walk that finds each predicate and asks whether its cell holds is
// in foundation/contracts/ArmedRoster.h.

}  // namespace foundation::contracts
