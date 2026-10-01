// The compile-time checks of foundation/algebra/Transition.h.

#include <foundation/algebra/Transition.h>

namespace foundation::algebra::transition {

// ── Self-test over a stand-in registry ────────────────────────────────

namespace detail::transition_self_test {

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
template <class R>
struct From {};
template <class R>
struct Fault {};
struct Undeclared {};
template <class L, class P>
struct Named {};
struct LabelA {};
struct LabelB {};

template <class T>
struct named_label;
template <class L, class P>
struct named_label<Named<L, P>> {
    using type = L;
};
template <class T>
using named_label_t = typename named_label<T>::type;

template <class T>
struct named_note;
template <class L, class P>
struct named_note<Named<L, P>> {
    using type = From<L>;
};
template <class T>
using named_note_t = typename named_note<T>::type;

template <int V>
inline constexpr bool pin_is_named_v = V != 0;

template <class T, class K>
struct Drop {};
template <class T, class K>
struct Grab {};
template <class K>
struct Mark {};
struct Stall {};
template <class T, class K>
struct Give {};
template <class T, class K>
struct Get {};

namespace registry {
inline constexpr combinator put{.shape = ^^Put,
                                .kind = shape_kind::step,
                                .direction = polarity::output,
                                .dual = ^^Take,
                                .payload_variance = variance::covariant,
                                .keyed_choice = ^^Pick};
inline constexpr combinator take{.shape = ^^Take,
                                 .kind = shape_kind::step,
                                 .direction = polarity::input,
                                 .dual = ^^Put,
                                 .payload_variance = variance::contravariant,
                                 .keyed_choice = ^^Wait};
// A step pair with no keyed choice: it cannot carry a payload that names
// a label.
inline constexpr combinator drop{.shape = ^^Drop,
                                 .kind = shape_kind::step,
                                 .direction = polarity::output,
                                 .dual = ^^Grab,
                                 .payload_variance = variance::covariant};
inline constexpr combinator grab{.shape = ^^Grab,
                                 .kind = shape_kind::step,
                                 .direction = polarity::input,
                                 .dual = ^^Drop,
                                 .payload_variance = variance::contravariant};
inline constexpr combinator pick{
    .shape = ^^Pick, .kind = shape_kind::choice, .direction = polarity::output, .dual = ^^Wait, .annotation = ^^From};
inline constexpr combinator wait{
    .shape = ^^Wait, .kind = shape_kind::choice, .direction = polarity::input, .dual = ^^Pick, .annotation = ^^From};
inline constexpr combinator again{.shape = ^^Again, .kind = shape_kind::binder, .dual = ^^Again};
inline constexpr combinator back{.shape = ^^Back, .kind = shape_kind::back, .dual = ^^Back};
inline constexpr combinator done{.shape = ^^Done, .kind = shape_kind::terminal, .dual = ^^Done};
inline constexpr combinator halt{.shape = ^^Halt, .kind = shape_kind::terminal, .dual = ^^Halt, .absorbs_suffix = true};
inline constexpr combinator pin{
    .shape = ^^Pin, .kind = shape_kind::wrapper, .dual = ^^Pin, .value_admits = ^^pin_is_named_v};
// A marker and a terminal that no plain protocol holds.
inline constexpr combinator mark{.shape = ^^Mark, .kind = shape_kind::marker, .dual = ^^Mark, .is_plain = false};
inline constexpr combinator stall{
    .shape = ^^Stall, .kind = shape_kind::terminal, .dual = ^^Stall, .absorbs_suffix = true, .is_plain = false};
// A step pair whose payload is a protocol.
inline constexpr combinator give{.shape = ^^Give,
                                 .kind = shape_kind::step,
                                 .direction = polarity::output,
                                 .dual = ^^Get,
                                 .payload_is_protocol = true};
inline constexpr combinator get{.shape = ^^Get,
                                .kind = shape_kind::step,
                                .direction = polarity::input,
                                .dual = ^^Give,
                                .payload_is_protocol = true};
inline constexpr payload_rule fault{.shape = ^^Fault, .is_sendable = false, .is_label = false};
inline constexpr payload_rule named{.shape = ^^Named, .label_key = ^^named_label_t, .input_note = ^^named_note_t};
}  // namespace registry

// Two registries whose keyed choices break the coherence rules.
namespace keyed_backwards {
inline constexpr combinator put{.shape = ^^Put,
                                .kind = shape_kind::step,
                                .direction = polarity::output,
                                .dual = ^^Take,
                                .payload_variance = variance::covariant,
                                .keyed_choice = ^^Wait};
inline constexpr combinator take{.shape = ^^Take,
                                 .kind = shape_kind::step,
                                 .direction = polarity::input,
                                 .dual = ^^Put,
                                 .payload_variance = variance::contravariant,
                                 .keyed_choice = ^^Pick};
inline constexpr combinator pick{
    .shape = ^^Pick, .kind = shape_kind::choice, .direction = polarity::output, .dual = ^^Wait};
inline constexpr combinator wait{
    .shape = ^^Wait, .kind = shape_kind::choice, .direction = polarity::input, .dual = ^^Pick};
}  // namespace keyed_backwards

namespace keyed_one_sided {
inline constexpr combinator put{.shape = ^^Put,
                                .kind = shape_kind::step,
                                .direction = polarity::output,
                                .dual = ^^Take,
                                .payload_variance = variance::covariant,
                                .keyed_choice = ^^Pick};
inline constexpr combinator take{.shape = ^^Take,
                                 .kind = shape_kind::step,
                                 .direction = polarity::input,
                                 .dual = ^^Put,
                                 .payload_variance = variance::contravariant};
inline constexpr combinator pick{
    .shape = ^^Pick, .kind = shape_kind::choice, .direction = polarity::output, .dual = ^^Wait};
inline constexpr combinator wait{
    .shape = ^^Wait, .kind = shape_kind::choice, .direction = polarity::input, .dual = ^^Pick};
}  // namespace keyed_one_sided

static_assert(check_combinator(^^keyed_backwards, ^^Put).reason == incoherence::keyed_choice_not_a_choice,
              "the keyed choice of an output step is an output choice");
static_assert(check_combinator(^^keyed_one_sided, ^^Put).reason == incoherence::keyed_choice_not_dual
                  && check_combinator(^^keyed_one_sided, ^^Take).reason == incoherence::keyed_choice_not_dual,
              "a keyed step and its dual stand for dual choices, or neither is keyed");

namespace no_axioms {}

inline constexpr std::meta::info reg = ^^registry;

[[nodiscard]] consteval bool well_formed(std::meta::info type) { return fold(reg, type, well_formed_algebra{reg}, {}); }
[[nodiscard]] consteval std::meta::info dual(std::meta::info type) { return fold(reg, type, dual_algebra{reg}, 0); }
[[nodiscard]] consteval bool refines_plain(std::meta::info sub, std::meta::info super) {
    return refines(reg, ^^no_axioms, sub, super).holds;
}

static_assert(check_registry(reg).reason == incoherence::none);

using Ping = Put<int, Take<char, Done>>;
using Pong = Take<int, Put<char, Done>>;
static_assert(dual(^^Ping) == std::meta::dealias(^^Pong) && dual(^^Pong) == std::meta::dealias(^^Ping));
static_assert(dual(^^Wait<From<int>, Done, Back>) == ^^Pick<From<int>, Done, Back>);
static_assert(dual(dual(^^Wait<From<int>, Done, Back>)) == ^^Wait<From<int>, Done, Back>, "the dual keeps the note");
static_assert(dual(^^Pin<3, Ping>) == ^^Pin<3, Pong>);

static_assert(well_formed(^^Ping));
static_assert(well_formed(^^Again<Put<int, Back>>));
static_assert(!well_formed(^^Back));
static_assert(!well_formed(^^Again<Back>), "a back node with no step above it is not guarded");
static_assert(!well_formed(^^Again<Pin<1, Back>>), "a wrapper is not a guard");
static_assert(!well_formed(^^Again<Put<int, Again<Back>>>), "the inner back node binds the inner binder");
static_assert(!well_formed(^^Again<Done>), "a binder body is not a terminal");
static_assert(!well_formed(^^Pin<0, Done>), "the value filter refuses the value 0");
static_assert(!well_formed(^^Put<Fault<int>, Done>), "a payload that is not sendable is not sent");
static_assert(well_formed(^^Take<Fault<int>, Done>));
static_assert(!well_formed(^^Undeclared), "an unregistered combinator is refused");
static_assert(!well_formed(^^Pick<>) && !well_formed(^^Wait<>), "a choice has one label branch or more");
static_assert(!well_formed(^^Wait<From<int>>), "a note is not a branch");
static_assert(!well_formed(^^Wait<Take<Fault<int>, Done>>), "a choice of branches that are no label is empty");
static_assert(!well_formed(^^Wait<Take<Fault<int>, Done>, Take<int, Done>>),
              "a label branch after a branch that is no label has no wire label");
static_assert(!well_formed(^^Pick<Put<int, Done>, Take<Fault<int>, Done>>),
              "an internal choice has no branch that is no label");
static_assert(!well_formed(^^Wait<Take<int, Done>, Take<Fault<int>, Done>, Take<Fault<int>, Put<int, Done>>>),
              "two branches that are no label receive the same payload");
static_assert(well_formed(^^Wait<Take<int, Done>, Take<Fault<int>, Done>, Take<Fault<char>, Done>>));
static_assert(first_unregistered(reg, ^^Put<int, Pick<Done, Undeclared>>) == ^^Undeclared);

using KeyedPick = Pick<Put<Named<LabelA, int>, Done>, Put<Named<LabelB, int>, Done>>;
static_assert(well_formed(^^KeyedPick));
static_assert(well_formed(^^Wait<Take<Named<LabelA, int>, Done>, Take<Fault<int>, Done>>),
              "a branch that is no label does not make a keyed choice mixed");
static_assert(first_faulty_choice(reg, ^^Pick<Put<Named<LabelA, int>, Done>, Put<int, Done>>).fault
                  == choice_fault::mixed_label_keys,
              "a choice mixes a keyed and a positional label branch");
static_assert(first_faulty_choice(reg, ^^Pick<Put<Named<LabelA, int>, Done>, Put<Named<LabelA, char>, Done>>).fault
              == choice_fault::repeated_label_key);
static_assert(is_keyed_choice_type(reg, ^^KeyedPick) && !is_keyed_choice_type(reg, ^^Pick<Put<int, Done>>));
static_assert((label_word_of(^^LabelA) & label_word_bit) != 0 && label_word_of(^^LabelA) != label_word_of(^^LabelB));
static_assert(wire_word_of(reg, ^^KeyedPick, 1).is_wired
                  && wire_word_of(reg, ^^KeyedPick, 1).value == label_word_of(^^LabelB),
              "the word of a keyed branch is the label word of its key, not its position");
static_assert(wire_word_of(reg, ^^Pick<Put<int, Done>, Put<char, Done>>, 1).value == 1,
              "the word of a positional branch is its position");
static_assert(!wire_word_of(reg, ^^Wait<Take<int, Done>, Take<Fault<int>, Done>>, 1).is_wired,
              "a branch that is no label has no word");
static_assert(!wire_word_of(reg, ^^KeyedPick, 2).is_wired, "an index past the last branch has no word");

// A keyed step is a choice with one branch.
struct LabelC {};
using SendA = Put<Named<LabelA, int>, Done>;
using RecvA = Take<Named<LabelA, int>, Done>;
using KeyedWait = Wait<From<LabelA>, Take<Named<LabelA, int>, Done>, Take<Named<LabelB, int>, Done>>;
static_assert(is_keyed_step_type(reg, ^^SendA) && !is_keyed_step_type(reg, ^^Put<int, Done>));
static_assert(wire_word_of_step(reg, ^^SendA).is_wired
                  && wire_word_of_step(reg, ^^SendA).value == label_word_of(^^LabelA),
              "a keyed step sends the label word of its key");
static_assert(!wire_word_of_step(reg, ^^Put<int, Done>).is_wired, "a plain step has no word");
static_assert(refines_plain(^^SendA, ^^KeyedPick), "a keyed step sends one label of the larger choice");
static_assert(refines(reg, ^^no_axioms, ^^Put<Named<LabelC, int>, Done>, ^^KeyedPick).reason == mismatch::label_set);
static_assert(refines_plain(^^KeyedWait, ^^RecvA), "a choice that receives more labels stands for a keyed step");
static_assert(refines(reg, ^^no_axioms, ^^RecvA, ^^KeyedWait).reason == mismatch::label_set);
static_assert(refines(reg, ^^no_axioms,
                      ^^Wait<From<LabelC>, Take<Named<LabelA, int>, Done>, Take<Named<LabelB, int>, Done>>,
                      ^^Take<Named<LabelC, int>, Done>)
                  .reason
              == mismatch::label_set);
static_assert(refines(reg, ^^no_axioms, ^^Wait<Take<Named<LabelA, int>, Done>>, ^^RecvA).reason == mismatch::annotation,
              "an input keyed step takes the note of its payload rule");
static_assert(refines_plain(^^SendA, ^^Pick<SendA>) && refines_plain(^^Pick<SendA>, ^^SendA),
              "a keyed step and the choice of that one branch are one type");
static_assert(refines_plain(^^Again<Put<Named<LabelA, int>, Back>>, ^^Again<Pick<Put<Named<LabelA, int>, Back>>>),
              "the entry of a loop reads as the choice it stands for");
static_assert(first_faulty_choice(reg, ^^Pick<Again<Put<Named<LabelA, int>, Back>>>).fault
                  == choice_fault::keyed_label_below_root,
              "a keyed label branch is its label step, not a loop entry");
static_assert(first_faulty_choice(reg, ^^Pick<Pin<1, SendA>>).fault == choice_fault::keyed_label_below_root);
static_assert(first_faulty_choice(reg, ^^Pick<Again<Put<int, Back>>>).fault == choice_fault::none,
              "a positional label branch may start with a binder");

static_assert(fold(reg, ^^Put<int, Pick<Done, Halt>>, compose_algebra{reg, ^^Ping}, 0) == ^^Put<int, Pick<Ping, Halt>>);
static_assert(fold(reg, ^^Put<int, Pick<Done, Done>>, compose_at_choice_algebra{reg, 1, ^^Ping}, 0)
              == ^^Put<int, Pick<Done, Ping>>);
static_assert(fold(reg, ^^Put<int, Done>, compose_at_choice_algebra{reg, 0, ^^Ping}, 0) == std::meta::info{});

static_assert(fold(reg, ^^Pick<>, empty_choice_algebra{reg}, 0));
static_assert(fold(reg, ^^Wait<Take<Fault<int>, Done>>, empty_choice_algebra{reg}, 0),
              "a choice made only of branches that are no labels is empty");
static_assert(!fold(reg, ^^Wait<Take<int, Done>, Take<Fault<int>, Done>>, empty_choice_algebra{reg}, 0));

static_assert(refines_plain(^^Ping, ^^Ping));
static_assert(refines_plain(^^Pick<Done>, ^^Pick<Done, Done>) && !refines_plain(^^Pick<Done, Done>, ^^Pick<Done>));
static_assert(refines_plain(^^Wait<Done, Done>, ^^Wait<Done>) && !refines_plain(^^Wait<Done>, ^^Wait<Done, Done>));
static_assert(refines_plain(^^Again<Put<int, Back>>, ^^Put<int, Again<Put<int, Back>>>),
              "the relation is on the unfoldings, so one unfold refines its loop");
static_assert(!refines_plain(^^Wait<From<int>, Done>, ^^Wait<Done>), "the notes differ");
static_assert(!refines_plain(^^Pin<1, Done>, ^^Pin<2, Done>), "an invariant value is compared for equality");
static_assert(refines(reg, ^^no_axioms, ^^Wait<Done, Take<Fault<int>, Done>>, ^^Wait<Done>).reason
              == mismatch::non_label_branch);
static_assert(refines(reg, ^^no_axioms, ^^Put<int, Done>, ^^Put<long, Done>).reason == mismatch::payload);
static_assert(refines_plain(^^Wait<Take<int, Done>, Take<char, Done>, Take<Fault<int>, Done>>,
                            ^^Wait<Take<int, Done>, Take<Fault<int>, Done>>),
              "a label branch added before the trailing branch that is no label (rule Sub-&)");
static_assert(refines(reg, ^^no_axioms, ^^Wait<Take<int, Done>>, ^^Wait<Take<int, Done>, Take<Fault<int>, Done>>).reason
              == mismatch::missing_non_label_branch);
static_assert(!refines_plain(^^Wait<Take<char, Done>, Take<int, Done>>, ^^Wait<Take<int, Done>, Take<char, Done>>),
              "the position of a label branch is its wire label, so another order is another choice");

// Exit preservation: a subtype that drops the only exit of a loop loses
// termination, and a stream refines a stream.
using Exiting = Again<Pick<Put<int, Back>, Done>>;
using Endless = Again<Pick<Put<int, Back>>>;
static_assert(refines_plain(^^Exiting, ^^Exiting) && refines_plain(^^Endless, ^^Endless));
static_assert(refines(reg, ^^no_axioms, ^^Endless, ^^Exiting).reason == mismatch::loses_termination);
static_assert(is_terminable(reg, ^^Exiting) && !is_terminable(reg, ^^Endless));

// Capture: a bare back node is open, and a terminal under a binder is
// bound unless it absorbs the suffix.
static_assert(has_open_back(reg, ^^Back) && has_open_back(reg, ^^Put<int, Back>));
static_assert(!has_open_back(reg, ^^Again<Put<int, Back>>));
static_assert(has_bound_terminal(reg, ^^Exiting) && !has_bound_terminal(reg, ^^Pick<Done>));
static_assert(!has_bound_terminal(reg, ^^Again<Pick<Put<int, Back>, Halt>>),
              "a terminal that absorbs the suffix stays");
static_assert(has_bound_terminal(reg, ^^Pick<Done>, 1), "a binder above the type binds its terminals");

// ── Markers, plainness and payload protocols ──────────────────────────

// A marker carries no message.  Duality, composition and refinement pass
// it and keep its place, and it is no terminal.
static_assert(dual(^^Mark<Ping>) == ^^Mark<Pong>);
static_assert(fold(reg, ^^Put<int, Mark<Done>>, compose_algebra{reg, ^^Ping}, 0) == ^^Put<int, Mark<Ping>>);
static_assert(fold(reg, ^^Put<int, Pick<Mark<Done>, Done>>, compose_at_choice_algebra{reg, 0, ^^Ping}, 0)
              == ^^Put<int, Pick<Mark<Ping>, Done>>);
static_assert(!fold(reg, ^^Mark<Done>, terminal_algebra{}, 0), "a marker is no terminal");
static_assert(fold(reg, ^^Mark<Pick<>>, empty_choice_algebra{reg}, 0), "an empty choice below a marker is found");
static_assert(refines_plain(^^Mark<Ping>, ^^Mark<Ping>) && !refines_plain(^^Mark<Ping>, ^^Ping));
static_assert(has_bound_terminal(reg, ^^Again<Pick<Put<int, Back>, Mark<Done>>>));
static_assert(is_terminable(reg, ^^Mark<Done>));
static_assert(first_stop_of_spine(reg, ^^Mark<Put<int, Pick<Done>>>).entry.kind == shape_kind::choice,
              "the walk to the first stop passes a marker");

// A combinator that no plain protocol holds is refused by
// well-formedness at every depth, and every other algebra reads it.
static_assert(!well_formed(^^Mark<Done>) && !well_formed(^^Put<int, Mark<Done>>),
              "no plain protocol holds a marker that is not plain");
static_assert(!well_formed(^^Stall) && !well_formed(^^Put<int, Pick<Stall, Done>>),
              "no plain protocol holds a terminal that is not plain");
static_assert(fold(reg, ^^Put<int, Pick<Stall, Done>>, compose_algebra{reg, ^^Ping}, 0)
                  == ^^Put<int, Pick<Stall, Ping>>,
              "a terminal that absorbs the suffix keeps its place");

// A step whose payload is a protocol: that protocol is well-formed
// outside every binder, and an empty choice in it is found.  Duality
// keeps it as it is.
static_assert(well_formed(^^Give<Ping, Done>) && !well_formed(^^Give<Back, Done>)
              && !well_formed(^^Give<Pick<>, Done>));
static_assert(well_formed(^^Again<Give<Ping, Back>>) && !well_formed(^^Again<Give<Put<int, Back>, Back>>),
              "a back node of the payload protocol binds no binder of the carrier");
static_assert(!well_formed(^^Give<Mark<Done>, Done>), "the payload protocol is plain too");
static_assert(fold(reg, ^^Give<Pick<>, Done>, empty_choice_algebra{reg}, 0));
static_assert(dual(^^Give<Ping, Done>) == ^^Get<Ping, Done>, "the payload protocol travels as it is");

// ── One registry for each coherence rule ──────────────────────────────
//
// A layer that seals its registry refuses a registration outside its own
// header.  So each rule below is witnessed here, on a stand-in registry
// that breaks that rule alone, and check_combinator names the rule.

template <class T, class K>
struct Push {};
template <class T, class K>
struct Pull {};
template <class T, class K>
struct Shove {};
template <class... Bs>
struct Ask {};
template <class... Bs>
struct Answer {};
template <class R>
struct Other {};
template <class K>
struct Unmark {};

namespace registered_twice {
inline constexpr combinator push{.shape = ^^Push,
                                 .kind = shape_kind::step,
                                 .direction = polarity::output,
                                 .dual = ^^Pull,
                                 .payload_variance = variance::covariant};
inline constexpr combinator push_again{.shape = ^^Push,
                                       .kind = shape_kind::step,
                                       .direction = polarity::output,
                                       .dual = ^^Pull,
                                       .payload_variance = variance::covariant};
inline constexpr combinator pull{.shape = ^^Pull,
                                 .kind = shape_kind::step,
                                 .direction = polarity::input,
                                 .dual = ^^Push,
                                 .payload_variance = variance::contravariant};
}  // namespace registered_twice
static_assert(check_combinator(^^registered_twice, ^^Push).reason == incoherence::registered_twice,
              "a second registration of a shape is refused");

namespace not_involutive {
inline constexpr combinator push{.shape = ^^Push,
                                 .kind = shape_kind::step,
                                 .direction = polarity::output,
                                 .dual = ^^Pull,
                                 .payload_variance = variance::covariant};
inline constexpr combinator pull{.shape = ^^Pull,
                                 .kind = shape_kind::step,
                                 .direction = polarity::input,
                                 .dual = ^^Shove,
                                 .payload_variance = variance::contravariant};
inline constexpr combinator shove{.shape = ^^Shove,
                                  .kind = shape_kind::step,
                                  .direction = polarity::output,
                                  .dual = ^^Pull,
                                  .payload_variance = variance::covariant};
}  // namespace not_involutive
static_assert(check_combinator(^^not_involutive, ^^Push).reason == incoherence::dual_not_involutive,
              "the dual of the dual of Push is Shove, so duality is no involution");

namespace variance_kept {
inline constexpr combinator push{.shape = ^^Push,
                                 .kind = shape_kind::step,
                                 .direction = polarity::output,
                                 .dual = ^^Pull,
                                 .payload_variance = variance::covariant};
inline constexpr combinator pull{.shape = ^^Pull,
                                 .kind = shape_kind::step,
                                 .direction = polarity::input,
                                 .dual = ^^Push,
                                 .payload_variance = variance::covariant};
}  // namespace variance_kept
static_assert(check_combinator(^^variance_kept, ^^Push).reason == incoherence::payload_variance_not_flipped,
              "the dual payload variance is the opposite");

namespace variance_backwards {
inline constexpr combinator push{.shape = ^^Push,
                                 .kind = shape_kind::step,
                                 .direction = polarity::output,
                                 .dual = ^^Pull,
                                 .payload_variance = variance::contravariant};
inline constexpr combinator pull{.shape = ^^Pull,
                                 .kind = shape_kind::step,
                                 .direction = polarity::input,
                                 .dual = ^^Push,
                                 .payload_variance = variance::covariant};
}  // namespace variance_backwards
static_assert(check_combinator(^^variance_backwards, ^^Push).reason == incoherence::variance_against_direction,
              "the pair flips its variance, and each side has the variance of the other direction");

namespace note_dropped {
inline constexpr combinator ask{
    .shape = ^^Ask, .kind = shape_kind::choice, .direction = polarity::output, .dual = ^^Answer};
inline constexpr combinator answer{
    .shape = ^^Answer, .kind = shape_kind::choice, .direction = polarity::input, .dual = ^^Ask, .annotation = ^^From};
}  // namespace note_dropped
static_assert(check_combinator(^^note_dropped, ^^Answer).reason == incoherence::annotation_differs,
              "the dual of a choice with a note names no note template, so the dual drops the note");

namespace note_differs {
inline constexpr combinator ask{
    .shape = ^^Ask, .kind = shape_kind::choice, .direction = polarity::output, .dual = ^^Answer, .annotation = ^^Other};
inline constexpr combinator answer{
    .shape = ^^Answer, .kind = shape_kind::choice, .direction = polarity::input, .dual = ^^Ask, .annotation = ^^From};
}  // namespace note_differs
static_assert(check_combinator(^^note_differs, ^^Ask).reason == incoherence::annotation_differs,
              "a choice and its dual name one note template");

// A marker and its dual disagree on plainness, from each side.
namespace plainness_differs {
inline constexpr combinator mark{.shape = ^^Mark, .kind = shape_kind::marker, .dual = ^^Unmark, .is_plain = false};
inline constexpr combinator unmark{.shape = ^^Unmark, .kind = shape_kind::marker, .dual = ^^Mark};
}  // namespace plainness_differs
static_assert(check_combinator(^^plainness_differs, ^^Mark).reason == incoherence::plainness_differs
                  && check_combinator(^^plainness_differs, ^^Unmark).reason == incoherence::plainness_differs,
              "a combinator and its dual agree on whether a plain protocol may hold them");

namespace protocol_payload_differs {
inline constexpr combinator give{.shape = ^^Push,
                                 .kind = shape_kind::step,
                                 .direction = polarity::output,
                                 .dual = ^^Pull,
                                 .payload_is_protocol = true};
inline constexpr combinator get{
    .shape = ^^Pull, .kind = shape_kind::step, .direction = polarity::input, .dual = ^^Push};
}  // namespace protocol_payload_differs
static_assert(check_combinator(^^protocol_payload_differs, ^^Push).reason == incoherence::protocol_payload_differs,
              "a step and its dual agree on whether the payload is a protocol");

// A step with no keyed choice cannot carry a payload that names a label.
// The same payload is well-formed under a step with a keyed choice, and
// the same step is well-formed with a plain payload, so the refusal comes
// from the keyed choice alone.
static_assert(!well_formed(^^Drop<Named<LabelA, int>, Done>) && well_formed(^^Drop<int, Done>)
                  && well_formed(^^Put<Named<LabelA, int>, Done>),
              "an output step with no keyed choice and a label payload is refused");
static_assert(!well_formed(^^Grab<Named<LabelA, int>, Done>) && well_formed(^^Grab<int, Done>)
                  && well_formed(^^Take<Named<LabelA, int>, Done>),
              "an input step with no keyed choice and a label payload is refused");

// A seal counts the combinators of its namespace.
namespace sealed_short {
inline constexpr combinator push{.shape = ^^Push,
                                 .kind = shape_kind::step,
                                 .direction = polarity::output,
                                 .dual = ^^Pull,
                                 .payload_variance = variance::covariant};
inline constexpr combinator pull{.shape = ^^Pull,
                                 .kind = shape_kind::step,
                                 .direction = polarity::input,
                                 .dual = ^^Push,
                                 .payload_variance = variance::contravariant};
inline constexpr seal combinator_seal{.kind = ^^combinator, .count = 1};
}  // namespace sealed_short
static_assert(read_seal(^^sealed_short, ^^combinator).fault == seal_fault::count_differs,
              "a combinator outside the seal of its namespace is counted");

// A member claim holds for a member that the arguments give, and it
// fails for a lying member, a missing member, a member of the wrong kind
// and a base that the claims do not name.
template <class T, class K>
struct Step {
    using message_type = T;
    using next = K;
};
template <>
struct Step<int, Done> {
    using message_type = int;
    using next = Halt;
};
template <>
struct Step<long, Done> {
    using message_type = long;
};
template <>
struct Step<char, Done> {
    using message_type = char;
    static constexpr int next = 0;
};
template <>
struct Step<short, Done> : Step<int, Halt> {
    using message_type = short;
    using next = Done;
};
[[nodiscard]] consteval bool step_agrees(std::meta::info type) {
    const auto arguments = std::meta::template_arguments_of(type);
    return members_agree(type, {member_claim{"message_type", arguments[0]}, member_claim{"next", arguments[1]}}, {});
}
static_assert(step_agrees(^^Step<bool, Done>), "a member that the arguments give agrees");
static_assert(!step_agrees(^^Step<int, Done>), "a member that names another type is refused");
static_assert(!step_agrees(^^Step<long, Done>), "a missing member is refused");
static_assert(!step_agrees(^^Step<char, Done>), "a data member where the claim names a type is refused");
static_assert(!step_agrees(^^Step<short, Done>), "a base that no claim names is refused");

}  // namespace detail::transition_self_test

}  // namespace foundation::algebra::transition
