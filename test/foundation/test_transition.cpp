// The transition algebra of foundation/algebra/Transition.h, over
// stand-in registries.  Each check names the rule it holds.  A registry
// in its own namespace is one experiment: the coherent registry is the
// baseline, and each incoherent registry breaks exactly one coherence
// rule.

#include <foundation/algebra/Transition.h>

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <meta>
#include <string_view>
#include <type_traits>

namespace tr = ::foundation::algebra::transition;

namespace test_transition_types {

// ── A coherent registry ───────────────────────────────────────────────

template <class T, class K>
struct Put {};
template <class T, class K>
struct Take {};
template <class... Bs>
struct Pick {};
template <class... Bs>
struct Wait {};
template <class B>
struct Again {};
struct Back {};
struct Done {};
struct Halt {};
template <int V, class P>
struct Pin {};
template <int V, class P>
struct Raise {};
template <int V, class P>
struct Lower {};
template <class R>
struct From {};
template <class R>
struct Fault {};
struct Unknown {};

template <int A, int B>
inline constexpr bool at_most_v = A <= B;

template <int V>
inline constexpr bool is_positive_v = V > 0;

namespace good {
inline constexpr tr::combinator put{.shape = ^^Put,
                                    .kind = tr::shape_kind::step,
                                    .direction = tr::polarity::output,
                                    .dual = ^^Take,
                                    .payload_variance = tr::variance::covariant,
                                    .keyed_choice = ^^Pick};
inline constexpr tr::combinator take{.shape = ^^Take,
                                     .kind = tr::shape_kind::step,
                                     .direction = tr::polarity::input,
                                     .dual = ^^Put,
                                     .payload_variance = tr::variance::contravariant,
                                     .keyed_choice = ^^Wait};
inline constexpr tr::combinator pick{.shape = ^^Pick,
                                     .kind = tr::shape_kind::choice,
                                     .direction = tr::polarity::output,
                                     .dual = ^^Wait,
                                     .annotation = ^^From};
inline constexpr tr::combinator wait{.shape = ^^Wait,
                                     .kind = tr::shape_kind::choice,
                                     .direction = tr::polarity::input,
                                     .dual = ^^Pick,
                                     .annotation = ^^From};
inline constexpr tr::combinator again{.shape = ^^Again, .kind = tr::shape_kind::binder, .dual = ^^Again};
inline constexpr tr::combinator back{.shape = ^^Back, .kind = tr::shape_kind::back, .dual = ^^Back};
inline constexpr tr::combinator done{.shape = ^^Done, .kind = tr::shape_kind::terminal, .dual = ^^Done};
inline constexpr tr::combinator halt{
    .shape = ^^Halt, .kind = tr::shape_kind::terminal, .dual = ^^Halt, .absorbs_suffix = true};
inline constexpr tr::combinator pin{
    .shape = ^^Pin, .kind = tr::shape_kind::wrapper, .dual = ^^Pin, .value_admits = ^^is_positive_v};
// A wrapper pair whose value order is covariant on one side and
// contravariant on the other, so refinement stays closed under duality.
inline constexpr tr::combinator raise{.shape = ^^Raise,
                                      .kind = tr::shape_kind::wrapper,
                                      .dual = ^^Lower,
                                      .value_variance = tr::variance::covariant,
                                      .value_order = ^^at_most_v};
inline constexpr tr::combinator lower{.shape = ^^Lower,
                                      .kind = tr::shape_kind::wrapper,
                                      .dual = ^^Raise,
                                      .value_variance = tr::variance::contravariant,
                                      .value_order = ^^at_most_v};
inline constexpr tr::payload_rule fault{.shape = ^^Fault, .is_sendable = false, .is_label = false};
}  // namespace good

constexpr std::meta::info reg = ^^good;

namespace no_axioms {}

consteval bool well_formed(std::meta::info type) { return tr::fold(reg, type, tr::well_formed_algebra{reg}, {}); }
consteval std::meta::info dual(std::meta::info type) { return tr::fold(reg, type, tr::dual_algebra{reg}, 0); }
consteval std::meta::info compose(std::meta::info type, std::meta::info suffix) {
    return tr::fold(reg, type, tr::compose_algebra{reg, suffix}, 0);
}
consteval bool empty_choice(std::meta::info type) { return tr::fold(reg, type, tr::empty_choice_algebra{reg}, 0); }
consteval bool terminal(std::meta::info type) { return tr::fold(reg, type, tr::terminal_algebra{}, 0); }
consteval tr::verdict refine(std::meta::info sub, std::meta::info super) {
    return tr::refines(reg, ^^no_axioms, sub, super);
}
consteval bool refines(std::meta::info sub, std::meta::info super) { return refine(sub, super).holds; }
consteval std::meta::info plain(std::meta::info type) { return std::meta::dealias(type); }

// ── Coherence ────────────────────────────────────────────────────────

static_assert(tr::check_registry(reg).reason == tr::incoherence::none);

namespace not_involutive {
template <class T, class K>
struct A {};
template <class T, class K>
struct B {};
template <class T, class K>
struct C {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^B,
                                  .payload_variance = tr::variance::covariant};
inline constexpr tr::combinator b{.shape = ^^B,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::input,
                                  .dual = ^^C,
                                  .payload_variance = tr::variance::contravariant};
inline constexpr tr::combinator c{.shape = ^^C,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^B,
                                  .payload_variance = tr::variance::covariant};
}  // namespace not_involutive
static_assert(tr::check_combinator(^^not_involutive, ^^not_involutive::A).reason
              == tr::incoherence::dual_not_involutive);

namespace same_variance {
template <class T, class K>
struct A {};
template <class T, class K>
struct B {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^B,
                                  .payload_variance = tr::variance::covariant};
inline constexpr tr::combinator b{.shape = ^^B,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::input,
                                  .dual = ^^A,
                                  .payload_variance = tr::variance::covariant};
}  // namespace same_variance
static_assert(tr::check_registry(^^same_variance).reason == tr::incoherence::payload_variance_not_flipped,
              "a send and a receive that are both covariant break closure under duality");

namespace same_direction {
template <class T, class K>
struct A {};
template <class T, class K>
struct B {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^B,
                                  .payload_variance = tr::variance::covariant};
inline constexpr tr::combinator b{.shape = ^^B,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^A,
                                  .payload_variance = tr::variance::contravariant};
}  // namespace same_direction
static_assert(tr::check_registry(^^same_direction).reason == tr::incoherence::direction_not_flipped);

namespace missing_dual {
template <class T, class K>
struct A {};
template <class T, class K>
struct Nowhere {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^Nowhere,
                                  .payload_variance = tr::variance::covariant};
}  // namespace missing_dual
static_assert(tr::check_registry(^^missing_dual).reason == tr::incoherence::dual_unregistered);

namespace twice {
struct Stop {};
inline constexpr tr::combinator one{.shape = ^^Stop, .kind = tr::shape_kind::terminal, .dual = ^^Stop};
inline constexpr tr::combinator two{.shape = ^^Stop, .kind = tr::shape_kind::terminal, .dual = ^^Stop};
}  // namespace twice
static_assert(tr::check_registry(^^twice).reason == tr::incoherence::registered_twice);

// ── Seals ────────────────────────────────────────────────────────────

namespace sealed_rules {
template <class T>
struct Opaque {};
inline constexpr tr::payload_rule opaque{.shape = ^^Opaque, .is_sendable = false, .is_label = false};
inline constexpr tr::seal rule_seal{.kind = ^^tr::payload_rule, .count = 1};
}  // namespace sealed_rules
static_assert(tr::read_seal(^^sealed_rules, ^^tr::payload_rule).is_sealed
              && tr::read_seal(^^sealed_rules, ^^tr::payload_rule).fault == tr::seal_fault::none);
static_assert(!tr::read_seal(^^sealed_rules, ^^tr::subsort_axiom).is_sealed, "a seal closes one kind only");
static_assert(!tr::lookup_payload_rule(^^sealed_rules, ^^sealed_rules::Opaque<int>).entry.is_sendable);

namespace short_seal {
template <class T>
struct Opaque {};
template <class T>
struct Hidden {};
inline constexpr tr::payload_rule opaque{.shape = ^^Opaque, .is_sendable = false, .is_label = false};
inline constexpr tr::seal rule_seal{.kind = ^^tr::payload_rule, .count = 1};
inline constexpr tr::payload_rule hidden{.shape = ^^Hidden, .is_sendable = false, .is_label = false};
}  // namespace short_seal
static_assert(tr::read_seal(^^short_seal, ^^tr::payload_rule).fault == tr::seal_fault::count_differs,
              "a rule after the seal");

namespace resealed {
template <class T>
struct Opaque {};
inline constexpr tr::payload_rule opaque{.shape = ^^Opaque, .is_sendable = false, .is_label = false};
inline constexpr tr::seal rule_seal{.kind = ^^tr::payload_rule, .count = 1};
inline constexpr tr::seal second_seal{.kind = ^^tr::payload_rule, .count = 1};
}  // namespace resealed
static_assert(tr::read_seal(^^resealed, ^^tr::payload_rule).fault == tr::seal_fault::sealed_twice);

static_assert(!tr::read_seal(reg, ^^tr::payload_rule).is_sealed
                  && tr::read_seal(reg, ^^tr::payload_rule).fault == tr::seal_fault::none,
              "a registry with no seal is open");

namespace ordered_self_dual {
template <int V, class P>
struct W {};
inline constexpr tr::combinator w{.shape = ^^W,
                                  .kind = tr::shape_kind::wrapper,
                                  .dual = ^^W,
                                  .value_variance = tr::variance::covariant,
                                  .value_order = ^^at_most_v};
}  // namespace ordered_self_dual
static_assert(tr::check_registry(^^ordered_self_dual).reason == tr::incoherence::value_variance_not_flipped,
              "a self-dual wrapper with an ordered value is not closed under duality");

namespace order_on_step {
template <class T, class K>
struct A {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^A,
                                  .value_order = ^^at_most_v};
}  // namespace order_on_step
static_assert(tr::check_registry(^^order_on_step).reason == tr::incoherence::order_on_non_wrapper);

// Each side flips the variance of the other, so the flip rule admits the
// pair, but the output is contravariant and the input covariant.  The
// subtype could then send a wider payload than the peer receives.
namespace backwards {
template <class T, class K>
struct A {};
template <class T, class K>
struct B {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^B,
                                  .payload_variance = tr::variance::contravariant};
inline constexpr tr::combinator b{.shape = ^^B,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::input,
                                  .dual = ^^A,
                                  .payload_variance = tr::variance::covariant};
}  // namespace backwards
static_assert(tr::check_registry(^^backwards).reason == tr::incoherence::variance_against_direction);

// An invariant payload is safe in each direction.
namespace invariant_step {
template <class T, class K>
struct A {};
template <class T, class K>
struct B {};
inline constexpr tr::combinator a{
    .shape = ^^A, .kind = tr::shape_kind::step, .direction = tr::polarity::output, .dual = ^^B};
inline constexpr tr::combinator b{
    .shape = ^^B, .kind = tr::shape_kind::step, .direction = tr::polarity::input, .dual = ^^A};
}  // namespace invariant_step
static_assert(tr::check_registry(^^invariant_step).reason == tr::incoherence::none);

namespace neutral_step {
template <class T, class K>
struct A {};
inline constexpr tr::combinator a{.shape = ^^A, .kind = tr::shape_kind::step, .dual = ^^A};
}  // namespace neutral_step
static_assert(tr::check_registry(^^neutral_step).reason == tr::incoherence::direction_on_neutral_kind);

namespace mixed_absorption {
struct S {};
struct T {};
inline constexpr tr::combinator s{.shape = ^^S, .kind = tr::shape_kind::terminal, .dual = ^^T};
inline constexpr tr::combinator t{.shape = ^^T, .kind = tr::shape_kind::terminal, .dual = ^^S, .absorbs_suffix = true};
}  // namespace mixed_absorption
static_assert(tr::check_registry(^^mixed_absorption).reason == tr::incoherence::absorption_differs);

namespace mixed_kind {
struct S {};
struct T {};
inline constexpr tr::combinator s{.shape = ^^S, .kind = tr::shape_kind::terminal, .dual = ^^T};
inline constexpr tr::combinator t{.shape = ^^T, .kind = tr::shape_kind::back, .dual = ^^S};
}  // namespace mixed_kind
static_assert(tr::check_registry(^^mixed_kind).reason == tr::incoherence::dual_kind_differs);

// A choice whose dual names no note, or another note template, loses the
// note under duality, and the round trip does not return the choice.
namespace one_sided_note {
template <class... Bs>
struct Out {};
template <class... Bs>
struct In {};
inline constexpr tr::combinator out{
    .shape = ^^Out, .kind = tr::shape_kind::choice, .direction = tr::polarity::output, .dual = ^^In};
inline constexpr tr::combinator in{.shape = ^^In,
                                   .kind = tr::shape_kind::choice,
                                   .direction = tr::polarity::input,
                                   .dual = ^^Out,
                                   .annotation = ^^From};
}  // namespace one_sided_note
static_assert(tr::check_registry(^^one_sided_note).reason == tr::incoherence::annotation_differs);

template <class R>
struct By {};
namespace other_note {
template <class... Bs>
struct Out {};
template <class... Bs>
struct In {};
inline constexpr tr::combinator out{.shape = ^^Out,
                                    .kind = tr::shape_kind::choice,
                                    .direction = tr::polarity::output,
                                    .dual = ^^In,
                                    .annotation = ^^By};
inline constexpr tr::combinator in{.shape = ^^In,
                                   .kind = tr::shape_kind::choice,
                                   .direction = tr::polarity::input,
                                   .dual = ^^Out,
                                   .annotation = ^^From};
}  // namespace other_note
static_assert(tr::check_registry(^^other_note).reason == tr::incoherence::annotation_differs);

// ── Decomposition and the refusal of an unknown node ──────────────────

static_assert(tr::is_registered(reg, ^^Put<int, Done>));
static_assert(!tr::is_registered(reg, ^^Unknown));
static_assert(!tr::is_registered(reg, ^^int));
static_assert(tr::first_unregistered(reg, ^^Put<int, Pick<Done, Wait<Unknown>>>) == ^^Unknown);
static_assert(tr::first_unregistered(reg, ^^Put<Unknown, Done>) == std::meta::info{},
              "a payload is not a node, so an unknown payload is not refused");
static_assert(tr::head_shape(reg, ^^Pin<3, Put<int, Done>>) == ^^Put, "a recognizer looks under wrappers");
static_assert(tr::head_shape(reg, ^^int) == ^^int);

// A registered shape with the wrong layout is not registered.
template <class T>
struct Single {};
namespace wrong_layout {
inline constexpr tr::combinator single{
    .shape = ^^Single, .kind = tr::shape_kind::step, .direction = tr::polarity::output, .dual = ^^Single};
}  // namespace wrong_layout
static_assert(!tr::is_registered(^^wrong_layout, ^^Single<int>));

// ── Duality ──────────────────────────────────────────────────────────

using Ping = Put<int, Take<char, Done>>;
using Pong = Take<int, Put<char, Done>>;
static_assert(dual(^^Ping) == plain(^^Pong));
static_assert(dual(dual(^^Ping)) == plain(^^Ping), "duality is an involution");
static_assert(dual(^^Wait<From<int>, Done>) == ^^Pick<From<int>, Done>, "the dual keeps the note");
static_assert(dual(dual(^^Wait<From<int>, Done>)) == ^^Wait<From<int>, Done>, "duality is an involution with notes");
static_assert(dual(^^Pick<Done>) == ^^Wait<Done>);
static_assert(dual(^^Raise<3, Done>) == ^^Lower<3, Done>);
static_assert(dual(^^Again<Put<int, Back>>) == ^^Again<Take<int, Back>>);

// ── Composition ──────────────────────────────────────────────────────

static_assert(compose(^^Put<int, Done>, ^^Ping) == ^^Put<int, Ping>);
static_assert(compose(^^Pick<Done, Halt>, ^^Ping) == ^^Pick<Ping, Halt>, "Halt absorbs the suffix");
static_assert(compose(^^Again<Pick<Put<int, Back>, Done>>, ^^Ping) == ^^Again<Pick<Put<int, Back>, Ping>>);
static_assert(compose(^^Wait<From<int>, Done>, ^^Ping) == ^^Wait<From<int>, Ping>, "composition keeps the note");
static_assert(tr::fold(reg, ^^Pin<2, Put<int, Pick<Done, Done>>>, tr::compose_at_choice_algebra{reg, 0, ^^Ping}, 0)
              == ^^Pin<2, Put<int, Pick<Ping, Done>>>);
static_assert(tr::fold(reg, ^^Pick<Done>, tr::compose_at_choice_algebra{reg, 1, ^^Ping}, 0) == std::meta::info{},
              "an index past the last branch has no answer");
static_assert(tr::first_stop_of_spine(reg, ^^Put<int, Again<Take<int, Back>>>).entry.kind == tr::shape_kind::back);

// Capture: a back node that no binder of its protocol binds is open, and
// a replaced terminal under a binder is bound.  A composition that puts
// an open suffix under a binder is a capture.
static_assert(tr::has_open_back(reg, ^^Back) && tr::has_open_back(reg, ^^Pick<Done, Put<int, Back>>));
static_assert(!tr::has_open_back(reg, ^^Again<Pick<Done, Put<int, Back>>>) && !tr::has_open_back(reg, ^^Ping));
static_assert(tr::has_open_back(reg, ^^Unknown), "a node the probe cannot read may hide a back node");
static_assert(tr::has_bound_terminal(reg, ^^Put<int, Again<Pick<Put<int, Back>, Done>>>));
static_assert(!tr::has_bound_terminal(reg, ^^Put<int, Pick<Done, Done>>));
static_assert(!tr::has_bound_terminal(reg, ^^Again<Pick<Put<int, Back>, Halt>>),
              "a terminal that absorbs the suffix stays");
static_assert(tr::has_bound_terminal(reg, ^^Pick<Done>, 1), "the binders above the type count");
static_assert(tr::binders_above_first_stop(reg, ^^Again<Put<int, Again<Pick<Done>>>>) == 2);

// A composition without capture keeps terminability: each node of the
// result can end when each node of the prefix and of the suffix can.
using Exiting = Again<Pick<Put<int, Back>, Done>>;
using Endless = Again<Put<int, Back>>;
static_assert(tr::is_terminable(reg, ^^Exiting) && tr::is_terminable(reg, ^^Ping)
              && !tr::is_terminable(reg, ^^Endless));
static_assert(tr::is_terminable(reg, compose(^^Exiting, ^^Ping)));
static_assert(tr::is_terminable(reg, compose(^^Pick<Done, Put<int, Done>>, ^^Exiting)));
static_assert(!tr::is_terminable(reg, compose(^^Exiting, ^^Endless)), "a suffix that never ends stays one");
static_assert(!tr::is_terminable(reg, compose(^^Exiting, ^^Back)),
              "the capture that composition refuses is what makes the result lose its exit");

// ── Well-formedness ──────────────────────────────────────────────────

static_assert(well_formed(^^Ping) && well_formed(^^Pin<1, Ping>));
static_assert(well_formed(^^Again<Put<int, Back>>));
static_assert(well_formed(^^Again<Pick<Put<int, Back>, Done>>));
static_assert(well_formed(^^Again<Put<int, Again<Take<int, Back>>>>));
static_assert(!well_formed(^^Back), "a back node needs a binder");
static_assert(!well_formed(^^Put<int, Back>));
static_assert(!well_formed(^^Again<Back>), "a back node needs a guard");
static_assert(!well_formed(^^Again<Pin<1, Back>>), "a wrapper is not a guard");
static_assert(well_formed(^^Again<Again<Put<int, Back>>>),
              "the inner back node binds the inner binder, which is guarded");
static_assert(!well_formed(^^Again<Put<int, Again<Back>>>), "the inner binder is not guarded");
static_assert(!well_formed(^^Again<Done>) && !well_formed(^^Again<Halt>), "a binder body is not a terminal");
static_assert(!well_formed(^^Pin<0, Done>) && !well_formed(^^Pin<-4, Done>), "the value filter refuses");
static_assert(!well_formed(^^Put<Fault<int>, Done>), "a payload that is not sendable");
static_assert(well_formed(^^Take<Fault<int>, Done>));
static_assert(!well_formed(^^Put<int, Unknown>), "an unknown node is refused");

// The three rules of branches and labels.
static_assert(!well_formed(^^Pick<>) && !well_formed(^^Wait<>) && !well_formed(^^Wait<From<int>>),
              "a choice has one label branch or more, and a note is not a branch");
static_assert(!well_formed(^^Wait<Take<Fault<int>, Done>>), "branches that are no label only");
static_assert(!well_formed(^^Put<int, Pick<Done, Wait<>>>), "an empty choice below the head");
static_assert(!well_formed(^^Wait<Take<Fault<int>, Done>, Done>), "a label branch after a branch that is no label");
static_assert(!well_formed(^^Wait<Again<Take<Fault<int>, Back>>, Done>),
              "a binder at the head of a branch is looked through");
static_assert(well_formed(^^Wait<Done, Again<Take<Fault<int>, Back>>>));
static_assert(!well_formed(^^Pick<Done, Take<Fault<int>, Done>>), "an internal choice with a branch that is no label");
static_assert(!well_formed(^^Wait<Done, Take<Fault<int>, Done>, Pin<1, Take<Fault<int>, Put<int, Done>>>>),
              "two branches that are no label receive one payload, under a wrapper too");
static_assert(well_formed(^^Wait<Done, Take<Fault<int>, Done>, Take<Fault<char>, Done>>));
static_assert(tr::first_faulty_choice(reg, ^^Put<int, Wait<Take<Fault<int>, Done>, Done>>).fault
              == tr::choice_fault::label_after_non_label);
static_assert(tr::first_faulty_choice(reg, ^^Put<int, Wait<Take<Fault<int>, Done>, Done>>).choice
              == ^^Wait<Take<Fault<int>, Done>, Done>);

// ── Terminality and empty choices ────────────────────────────────────

static_assert(terminal(^^Done) && terminal(^^Halt) && terminal(^^Pin<1, Done>));
static_assert(!terminal(^^Back) && !terminal(^^Ping) && !terminal(^^Pick<>));
static_assert(empty_choice(^^Pick<>) && empty_choice(^^Wait<From<int>>));
static_assert(empty_choice(^^Put<int, Pick<Done, Wait<>>>), "the walk covers the whole spine");
static_assert(empty_choice(^^Wait<Take<Fault<int>, Done>, Pin<1, Take<Fault<char>, Done>>>),
              "no branch is a label, under wrappers too");
static_assert(!empty_choice(^^Wait<Take<int, Done>, Take<Fault<int>, Done>>));

// ── No entry point for a specialization ───────────────────────────────
//
// The fold recurses in place.  A specialization of a template of the
// layer changes what that template says for its one node, and no answer
// of the fold at any depth.

template <class P>
inline constexpr bool wf_entry_v = tr::fold(reg, ^^P, tr::well_formed_algebra{reg}, {});
template <>
inline constexpr bool wf_entry_v<Halt> = false;

static_assert(!wf_entry_v<Halt>, "the specialization answers for its own node");
static_assert(wf_entry_v<Put<int, Pick<Done, Halt>>>, "and for no node below the head of another");

// ── The payload preorder ─────────────────────────────────────────────

template <class T>
struct Boxed {};
template <class T>
struct Shed {
    using type = T;
};
template <class T>
struct Shed<Boxed<T>> {
    using type = T;
};
template <class T>
inline constexpr bool is_boxed_v = false;
template <class T>
inline constexpr bool is_boxed_v<Boxed<T>> = true;
template <class T>
using shed_t = typename Shed<T>::type;
template <class T, class U>
inline constexpr bool small_to_wide_v = std::is_same_v<T, short> && std::is_same_v<U, long>;

namespace axioms {
inline constexpr tr::subsort_axiom boxed{.drops = ^^is_boxed_v, .inner = ^^shed_t};
inline constexpr tr::subsort_axiom widen{.weakens = ^^small_to_wide_v};
}  // namespace axioms

static_assert(tr::subsorts(^^axioms, ^^int, ^^int), "the order is reflexive");
static_assert(tr::subsorts(^^axioms, ^^Boxed<Boxed<int>>, ^^int), "drops chain");
static_assert(tr::subsorts(^^axioms, ^^Boxed<short>, ^^long), "a drop, then a weakening");
static_assert(!tr::subsorts(^^axioms, ^^int, ^^Boxed<int>), "nothing rises");
static_assert(!tr::subsorts(^^axioms, ^^long, ^^short));

// A congruence lifts the order through one position of a class
// template and asks for identity at every other position.  This is the
// shape of a message that names its peer, its label and its payload.
template <class To, class Label, class Payload>
struct Envelope {};
struct Alice {};
struct Bob {};
struct Hello {};
struct Bye {};
namespace envelope_axioms {
inline constexpr tr::subsort_axiom widen{.weakens = ^^small_to_wide_v};
inline constexpr tr::subsort_axiom boxed{.drops = ^^is_boxed_v, .inner = ^^shed_t};
inline constexpr tr::subsort_axiom envelope{.congruence = ^^Envelope, .covariant = 0b100};
}  // namespace envelope_axioms
static_assert(tr::subsorts(^^envelope_axioms, ^^Envelope<Alice, Hello, short>, ^^Envelope<Alice, Hello, long>));
static_assert(tr::subsorts(^^envelope_axioms, ^^Envelope<Alice, Hello, Boxed<short>>, ^^Envelope<Alice, Hello, long>),
              "the covariant position recurs into the whole order");
static_assert(!tr::subsorts(^^envelope_axioms, ^^Envelope<Alice, Hello, long>, ^^Envelope<Alice, Hello, short>));
static_assert(!tr::subsorts(^^envelope_axioms, ^^Envelope<Alice, Hello, int>, ^^Envelope<Bob, Hello, int>),
              "the peer is compared for identity");
static_assert(!tr::subsorts(^^envelope_axioms, ^^Envelope<Alice, Hello, int>, ^^Envelope<Alice, Bye, int>),
              "the label is compared for identity");
static_assert(!tr::subsorts(^^envelope_axioms, ^^Envelope<Alice, Hello, short>, ^^Envelope<Alice, Bye, long>));
static_assert(!tr::subsorts(^^axioms, ^^Envelope<Alice, Hello, short>, ^^Envelope<Alice, Hello, long>),
              "without the congruence, two envelopes are in the order only when they are the same");

// An envelope names a label: its peer and its label, without the payload.
// Two label branches of one choice that name the same label are not
// well-formed.
template <class T>
struct envelope_label;
template <class To, class Label, class Payload>
struct envelope_label<Envelope<To, Label, Payload>> {
    using type = Envelope<To, Label, void>;
};
template <class T>
using envelope_label_t = typename envelope_label<T>::type;
using HelloAlias = Hello;
namespace good {
inline constexpr tr::payload_rule envelope{.shape = ^^Envelope, .label_key = ^^envelope_label_t};
}  // namespace good
static_assert(!well_formed(^^Pick<Put<Envelope<Alice, Hello, int>, Done>, Put<Envelope<Alice, Hello, long>, Done>>),
              "the payload is not part of the label");
static_assert(!well_formed(^^Pick<Put<Envelope<Alice, Hello, int>, Done>, Put<Envelope<Alice, HelloAlias, int>, Done>>),
              "an alias names the same label");
static_assert(well_formed(^^Pick<Put<Envelope<Alice, Hello, int>, Done>, Put<Envelope<Bob, Hello, int>, Done>>),
              "the peer is part of the label");
static_assert(well_formed(^^Pick<Put<Envelope<Alice, Hello, int>, Done>, Put<Envelope<Alice, Bye, int>, Done>>));
static_assert(tr::first_faulty_choice(
                  reg, ^^Pick<Put<Envelope<Alice, Hello, int>, Done>, Put<Envelope<Alice, Hello, long>, Done>>)
                  .fault
              == tr::choice_fault::repeated_label_key);

// ── Label words ──────────────────────────────────────────────────────

// Each label branch of this choice names a key, so the choice is keyed.
// The word of a branch is the label word of its key, and the position of
// the branch is not on the wire.
using EnvelopePick = Pick<Put<Envelope<Alice, Hello, int>, Done>, Put<Envelope<Alice, Bye, int>, Done>>;
using EnvelopePickSwapped = Pick<Put<Envelope<Alice, Bye, int>, Done>, Put<Envelope<Alice, Hello, int>, Done>>;
static_assert(tr::is_keyed_choice_type(reg, ^^EnvelopePick) && !tr::is_keyed_choice_type(reg, ^^Pick<Done, Ping>));
static_assert(tr::wire_word_of(reg, ^^EnvelopePick, 0).value == tr::wire_word_of(reg, ^^EnvelopePickSwapped, 1).value,
              "a label keeps its word in another position");
static_assert(tr::wire_word_of(reg, ^^EnvelopePick, 0).value == tr::label_word_of(^^Envelope<Alice, Hello, void>));
static_assert(tr::is_keyed_choice_type(reg, ^^Wait<Take<Envelope<Alice, Hello, int>, Done>, Take<Fault<int>, Done>>),
              "a branch that is no label does not count");

// The word is the stable type id of the key with the top bit set, so it is
// pinned where foundation/reflect/Hash.h pins the id.  A position never
// has the top bit, so a word and a position cannot be equal.
static_assert(tr::label_word_of(^^int) == (0x038bf5d93760ba14ULL | tr::label_word_bit));
static_assert(tr::wire_word_of(reg, ^^Pick<Done, Ping>, 1).value == 1);

// Rule 4: a choice that mixes a keyed and a positional label branch.
static_assert(tr::first_faulty_choice(reg, ^^Pick<Put<Envelope<Alice, Hello, int>, Done>, Put<int, Done>>).fault
              == tr::choice_fault::mixed_label_keys);

// Rule 5: two distinct closure types print one name, so they would share
// a label word, and the peer could not tell the two labels apart.  No word
// exists for such a key: every stable id refuses a type that names a
// closure, so the collision never reaches the wire.
// test/foundation/neg/neg_label_word_of_a_closure_key.cpp is the witness.
using ClosureLabelA = decltype([] {});
using ClosureLabelB = decltype([] {});
static_assert(!std::is_same_v<ClosureLabelA, ClosureLabelB>);
static_assert(!::foundation::reflect::HasStableIdentity<Envelope<Alice, ClosureLabelA, void>>);
static_assert(!::foundation::reflect::HasStableIdentity<Envelope<Alice, ClosureLabelB, void>>);

// An axiom that breaks the contract and sheds to the same type stops at
// the depth bound instead of a recursion without end.
template <class T>
inline constexpr bool always_v = true;
template <class T>
using same_t = T;
namespace looping_axioms {
inline constexpr tr::subsort_axiom loops{.drops = ^^always_v, .inner = ^^same_t};
}  // namespace looping_axioms
static_assert(!tr::subsorts(^^looping_axioms, ^^int, ^^long));

// ── The type graph ───────────────────────────────────────────────────

consteval bool graph_links_back_to_binder() {
    const tr::graph_view graph = tr::graph_of(reg, ^^Again<Put<int, Back>>);
    return graph.nodes.size() == 3 && graph.nodes[0].next == 1 && graph.nodes[1].next == 2 && graph.nodes[2].next == 0
        && tr::settle(graph, 2) == 1;
}
static_assert(graph_links_back_to_binder());

consteval bool graph_records_the_unknown() {
    const tr::graph_view graph = tr::graph_of(reg, ^^Pick<Done, Unknown>);
    return graph.unregistered == ^^Unknown;
}
static_assert(graph_records_the_unknown());

consteval bool graph_marks_labels_and_payloads() {
    const tr::graph_view graph = tr::graph_of(reg, ^^Wait<Take<Fault<int>, Done>, Put<Fault<int>, Done>>);
    return graph.nodes[1].is_label == false && graph.nodes[1].has_restricted_payload
        && graph.nodes[1].head_payload == (^^Fault<int>) && graph.nodes[3].has_restricted_payload
        && graph.nodes[3].is_label;
}
static_assert(graph_marks_labels_and_payloads());

// ── Refinement ───────────────────────────────────────────────────────

// Reflexive on every combinator, the note and the wrappers included.
static_assert(refines(^^Done, ^^Done) && refines(^^Halt, ^^Halt));
static_assert(refines(^^Ping, ^^Ping) && refines(^^Pong, ^^Pong));
static_assert(refines(^^Wait<From<int>, Done, Put<int, Done>>, ^^Wait<From<int>, Done, Put<int, Done>>));
static_assert(refines(^^Again<Put<int, Back>>, ^^Again<Put<int, Back>>));
static_assert(refines(^^Pin<3, Ping>, ^^Pin<3, Ping>) && refines(^^Raise<2, Ping>, ^^Raise<2, Ping>));

// Width: an output choice narrows, an input choice widens.  An empty
// choice is refused also when the layer does not check well-formedness
// first: under the branch rule it refines each larger internal choice,
// and a substitute of that type never sends.
static_assert(!refines(^^Pick<>, ^^Pick<Done>) && refines(^^Pick<Done>, ^^Pick<Done, Ping>));
static_assert(refine(^^Pick<>, ^^Pick<Done>).reason == tr::mismatch::ill_formed);
static_assert(!refines(^^Pick<Done, Ping>, ^^Pick<Done>));
static_assert(refines(^^Wait<Done, Ping>, ^^Wait<Done>) && !refines(^^Wait<Done>, ^^Wait<Done, Ping>));
static_assert(refine(^^Pick<Done, Done>, ^^Pick<Done>).reason == tr::mismatch::branch_count);

// Variance: a covariant value and a contravariant value.
static_assert(refines(^^Raise<1, Done>, ^^Raise<2, Done>) && !refines(^^Raise<2, Done>, ^^Raise<1, Done>));
static_assert(refines(^^Lower<2, Done>, ^^Lower<1, Done>) && !refines(^^Lower<1, Done>, ^^Lower<2, Done>));
static_assert(refines(dual(^^Raise<2, Done>), dual(^^Raise<1, Done>)), "closed under duality");
static_assert(refine(^^Pin<1, Done>, ^^Pin<2, Done>).reason == tr::mismatch::value);

// Shape, note and payload mismatches name the pair.
static_assert(refine(^^Put<int, Done>, ^^Take<int, Done>).reason == tr::mismatch::shape);
static_assert(refine(^^Wait<From<int>, Done>, ^^Wait<From<char>, Done>).reason == tr::mismatch::annotation);
static_assert(refine(^^Wait<From<int>, Done>, ^^Wait<Done>).reason == tr::mismatch::annotation);
static_assert(refine(^^Put<int, Done>, ^^Put<long, Done>).sub == ^^int);
static_assert(refine(^^Take<int, Done>, ^^Take<long, Done>).sub == ^^long,
              "for an input the pair reads supplied against expected");

// The leftmost failure is the one reported.
static_assert(refine(^^Pick<Put<int, Done>, Put<char, Done>>, ^^Pick<Put<long, Done>, Put<short, Done>>).sub == ^^int);

// Unfolds: a loop and the loop unfolded once refine each other.
static_assert(refines(^^Again<Put<int, Back>>, ^^Put<int, Again<Put<int, Back>>>));
static_assert(refines(^^Put<int, Again<Put<int, Back>>>, ^^Again<Put<int, Back>>));
static_assert(refines(^^Again<Put<int, Put<int, Back>>>, ^^Again<Put<int, Back>>),
              "a loop over two sends is an unfolding of a loop over one");
static_assert(!refines(^^Again<Put<int, Back>>, ^^Put<int, Done>));

// The payload rules of rule Sub-&.
static_assert(refine(^^Wait<Done, Take<Fault<int>, Done>>, ^^Wait<Done>).reason == tr::mismatch::non_label_branch);
static_assert(refines(^^Wait<Take<Fault<int>, Done>, Done>, ^^Wait<Take<Fault<int>, Done>, Done>));
static_assert(refine(^^Wait<Take<Fault<int>, Done>, Done>, ^^Wait<Take<Fault<int>, Done>>).reason
              == tr::mismatch::pure_non_label_choice);

// A label branch of a positional choice pairs by position, because its
// position is its wire word.  A branch that is no label pairs by the
// payload it receives.
static_assert(refines(^^Wait<Done, Ping, Take<Fault<int>, Done>>, ^^Wait<Done, Take<Fault<int>, Done>>),
              "a label branch added before the branch that is no label (rule Sub-&)");
static_assert(refines(^^Wait<Done, Take<Fault<char>, Done>, Take<Fault<int>, Done>>,
                      ^^Wait<Done, Take<Fault<int>, Done>, Take<Fault<char>, Done>>),
              "branches that are no label pair by payload, in any order");
static_assert(refine(^^Wait<Done>, ^^Wait<Done, Take<Fault<int>, Done>>).reason
              == tr::mismatch::missing_non_label_branch);
static_assert(refine(^^Wait<Done, Take<Fault<char>, Done>>, ^^Wait<Done, Take<Fault<int>, Done>>).reason
                  == tr::mismatch::non_label_branch,
              "a branch that is no label with no partner of the same payload");
static_assert(!refines(^^Wait<Ping, Done>, ^^Wait<Done, Ping>),
              "another order of positional label branches is another choice");

// A label branch of a keyed choice pairs by label, wherever it stands.
// An output choice of the subtype sends only labels of the supertype, and
// an input choice of the supertype receives only labels of the subtype.
using SendHelloBye = Pick<Put<Envelope<Alice, Hello, int>, Done>, Put<Envelope<Alice, Bye, int>, Done>>;
using SendByeHello = Pick<Put<Envelope<Alice, Bye, int>, Done>, Put<Envelope<Alice, Hello, int>, Done>>;
using SendBye = Pick<Put<Envelope<Alice, Bye, int>, Done>>;
using HearHelloBye = Wait<Take<Envelope<Alice, Hello, int>, Done>, Take<Envelope<Alice, Bye, int>, Done>>;
using HearByeHello = Wait<Take<Envelope<Alice, Bye, int>, Done>, Take<Envelope<Alice, Hello, int>, Done>>;
using HearBye = Wait<Take<Envelope<Alice, Bye, int>, Done>>;
static_assert(refines(^^SendHelloBye, ^^SendByeHello) && refines(^^SendByeHello, ^^SendHelloBye),
              "another order of keyed label branches is the same choice");
static_assert(refines(^^HearHelloBye, ^^HearByeHello) && refines(^^HearByeHello, ^^HearHelloBye));
static_assert(refines(^^SendBye, ^^SendHelloBye) && refines(^^SendBye, ^^SendByeHello));
static_assert(refine(^^SendHelloBye, ^^SendBye).reason == tr::mismatch::label_set,
              "an output choice sends a label that the supertype does not send");
static_assert(refines(^^HearByeHello, ^^HearBye) && refines(^^HearHelloBye, ^^HearBye));
static_assert(refine(^^HearBye, ^^HearHelloBye).reason == tr::mismatch::label_set,
              "an input choice of the supertype receives a label that the subtype does not receive");
static_assert(refines(dual(^^SendHelloBye), dual(^^SendBye)) && refines(dual(^^HearBye), dual(^^HearByeHello)),
              "closed under duality");
static_assert(dual(dual(^^SendByeHello)) == plain(^^SendByeHello), "duality keeps the order of the branches");

// A keyed step outside a choice is the choice of that one branch, so it
// pairs with a choice by label.  A step whose payload names no label
// stays a step.
struct Hi {};
using SendByeStep = Put<Envelope<Alice, Bye, int>, Done>;
using HearByeStep = Take<Envelope<Alice, Bye, int>, Done>;
static_assert(refines(^^SendByeStep, ^^SendHelloBye) && refines(^^SendByeStep, ^^SendBye)
                  && refines(^^SendBye, ^^SendByeStep),
              "a keyed send step sends one label of the choice");
static_assert(refines(^^HearHelloBye, ^^HearByeStep) && refines(^^HearByeStep, ^^HearBye)
                  && refines(^^HearBye, ^^HearByeStep),
              "a choice that receives more labels stands for a keyed receive step");
static_assert(refine(^^HearByeStep, ^^HearHelloBye).reason == tr::mismatch::label_set);
static_assert(refine(^^Put<Envelope<Alice, Hi, int>, Done>, ^^SendHelloBye).reason == tr::mismatch::label_set);
static_assert(refine(^^HearHelloBye, ^^Take<Envelope<Alice, Hi, int>, Done>).reason == tr::mismatch::label_set);
static_assert(refines(dual(^^SendHelloBye), dual(^^SendByeStep)), "closed under duality");
static_assert(refine(^^Put<int, Done>, ^^Pick<Put<int, Done>>).reason == tr::mismatch::shape,
              "a plain step is not a choice");
static_assert(tr::wire_word_of_step(reg, ^^SendByeStep).value == tr::wire_word_of(reg, ^^SendHelloBye, 1).value,
              "a keyed step sends the word of its branch");

// Two labels pair by label word and label key, and a keyed choice never
// pairs with a positional one.  One word with two keys is refused as well.
// Only a collision of two stable ids reaches that case, because a closure
// key takes no label word.
static_assert(refine(^^Pick<Put<Envelope<Alice, Hello, int>, Done>>, ^^Pick<Put<int, Done>>).reason
              == tr::mismatch::label_discipline);
static_assert(
    refine(^^Wait<Take<Envelope<Alice, Hello, int>, Done>>, ^^Wait<Take<Envelope<Bob, Hello, int>, Done>>).reason
        == tr::mismatch::label_set,
    "a label to another peer is another label");

// A keyed choice that repeats a label word is not well-formed.  The
// relation refuses it also when the layer does not check well-formedness
// first, because the peer could not tell the two branches apart.
static_assert(refine(^^Wait<Take<Envelope<Alice, Hello, int>, Done>, Take<Envelope<Alice, Hello, long>, Done>>,
                     ^^Wait<Take<Envelope<Alice, Hello, int>, Done>>)
                      .reason
                  == tr::mismatch::ill_formed,
              "an input choice of the subtype repeats a label");

// A paired label compares its payloads, and a crash branch still pairs by
// the payload it receives.
static_assert(tr::refines(reg, ^^envelope_axioms, ^^Pick<Put<Envelope<Alice, Bye, short>, Done>>,
                          ^^Pick<Put<Envelope<Alice, Hello, int>, Done>, Put<Envelope<Alice, Bye, long>, Done>>)
                  .holds);
static_assert(!tr::refines(reg, ^^envelope_axioms, ^^Pick<Put<Envelope<Alice, Bye, long>, Done>>,
                           ^^Pick<Put<Envelope<Alice, Hello, int>, Done>, Put<Envelope<Alice, Bye, short>, Done>>)
                   .holds);
static_assert(refines(^^Wait<Take<Envelope<Alice, Bye, int>, Done>, Take<Envelope<Alice, Hello, int>, Done>,
                             Take<Fault<int>, Done>>,
                      ^^Wait<Take<Envelope<Alice, Hello, int>, Done>, Take<Fault<int>, Done>>),
              "an input choice adds a label before its crash branch");

// Exit preservation reads the pairs by label: a subtype that drops the
// only exit of a keyed loop loses an exit, in whichever position it stands.
static_assert(refine(^^Again<Pick<Put<Envelope<Alice, Hello, int>, Back>>>,
                     ^^Again<Pick<Put<Envelope<Alice, Bye, int>, Done>, Put<Envelope<Alice, Hello, int>, Back>>>)
                  .reason
              == tr::mismatch::loses_termination);

// An unknown node is named.
static_assert(refine(^^Pick<Done, Unknown>, ^^Pick<Done>).reason == tr::mismatch::unregistered);

// Exit preservation.  A subtype that drops the only exit of a loop cannot
// end where the supertype can.  A stream refines a stream, and a subtype
// that keeps the exit refines.
static_assert(refine(^^Again<Pick<Put<int, Back>>>, ^^Exiting).reason == tr::mismatch::loses_termination);
static_assert(refines(^^Endless, ^^Endless) && refines(^^Exiting, ^^Again<Pick<Put<int, Back>, Done, Done>>));
static_assert(refine(^^Pick<Done, Put<int, Pick<Put<int, Endless>>>>,
                     ^^Pick<Done, Put<int, Pick<Put<int, Endless>, Done>>>)
                      .reason
                  == tr::mismatch::loses_termination,
              "the exit is lost at an inner position while the root can still end");
// Exit preservation is not closed under duality: the dual of the pair
// refines, because a receiver that never ends keeps no exit.
static_assert(refines(dual(^^Exiting), dual(^^Again<Pick<Put<int, Back>>>)));

// Transitivity on a chain of width steps.
static_assert(refines(^^Pick<Done>, ^^Pick<Done, Done>) && refines(^^Pick<Done, Done>, ^^Pick<Done, Done, Done>)
              && refines(^^Pick<Done>, ^^Pick<Done, Done, Done>));

// ── Values that reach the program ────────────────────────────────────

constexpr bool runtime_facts[] = {
    tr::check_registry(reg).reason == tr::incoherence::none,
    refines(^^Pick<Done>, ^^Pick<Done, Ping>),
    !refines(^^Pick<Done, Ping>, ^^Pick<Done>),
    well_formed(^^Again<Put<int, Back>>),
    !well_formed(^^Again<Back>),
    tr::subsorts(^^axioms, ^^Boxed<short>, ^^long),
    tr::refines(reg, ^^envelope_axioms, ^^Put<Envelope<Alice, Hello, short>, Done>,
                ^^Put<Envelope<Alice, Hello, long>, Done>)
        .holds,
    !tr::refines(reg, ^^envelope_axioms, ^^Put<Envelope<Alice, Hello, short>, Done>,
                 ^^Put<Envelope<Bob, Hello, long>, Done>)
         .holds,
    tr::wire_word_of(reg, ^^EnvelopePick, 0).value == tr::wire_word_of(reg, ^^EnvelopePickSwapped, 1).value,
    !::foundation::reflect::HasStableIdentity<Envelope<Alice, ClosureLabelA, void>>,
    refines(^^SendByeHello, ^^SendHelloBye),
    refine(^^HearBye, ^^HearHelloBye).reason == tr::mismatch::label_set,
};

constexpr std::string_view names[] = {
    "coherent registry",
    "narrow output choice",
    "wide output choice refused",
    "guarded loop",
    "unguarded loop refused",
    "drop then weaken",
    "congruent payload",
    "other peer refused",
    "label word by label",
    "closure label key refused",
    "keyed branches pair by label",
    "input label set refused",
};

}  // namespace test_transition_types

using namespace test_transition_types;

int main() {
    int failures = 0;
    for (std::size_t index = 0; index < std::size(runtime_facts); ++index) {
        if (!runtime_facts[index]) {
            std::fprintf(stderr, "test_transition: %.*s does not hold\n", static_cast<int>(names[index].size()),
                         names[index].data());
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
